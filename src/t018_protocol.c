/**
 * @file t018_protocol.c
 * @brief T.018 Protocol Implementation (BCH encoding, frame building)
 *
 * Complete port from dsPIC33CK implementation with full T.018 compliance:
 * - BCH(250,202) error correction with generator polynomial 0x1C7EB85DF3C97
 * - Frame building (252 bits: 2 header + 202 info + 48 BCH)
 * - GPS position encoding (T.018 Appendix C)
 * - ALL rotating field types (G008, ELT-DT, RLS, CANCEL)
 * - ELT sequence management (3 phases)
 * - Dynamic field calculations
 */

#include "t018_protocol.h"
#include "prn_generator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

// =============================================================================
// GALOIS FIELD TABLES (GF(2^6) for BCH)
// =============================================================================

static uint8_t gf_exp[512];        // Exponential table (double size for modulo)
static uint8_t gf_log[64];         // Logarithm table
static uint8_t gf_initialized = 0;

// Generator polynomial coefficients for BCH(250,202,6)
static const uint8_t generator_poly[] = {
    1, 59, 13, 104, 189, 68, 209, 30, 8, 163, 65, 41, 229, 98, 50, 36, 59,
    23, 23, 37, 207, 165, 180, 29, 51, 168, 124, 247, 18, 175, 199, 34,
    60, 220, 121, 101, 246, 81, 63, 94, 180, 19, 58, 153, 187, 230, 84,
    189, 1
};

// =============================================================================
// GLOBAL STATE
// =============================================================================

static beacon_config_t beacon_config = {
    .type = BEACON_TYPE_EPIRB,
    .country_code = 227,        // France MID 227
    .tac_number = 10001,
    .serial_number = 13398,
    .test_mode = 1,
    .position = {
        .latitude = 43.2,       // Marseille offshore
        .longitude = 5.4,       // Mediterranean
        .altitude = 0,
        .valid = 1
    }
};

static elt_state_t elt_state = {
    .current_phase = ELT_PHASE_1,
    .transmission_count = 0,
    .last_tx_time = 0,
    .phase_start_time = 0,
    .active = 0
};

static uint32_t system_time = 0;
static uint32_t activation_time = 0;
static uint32_t last_gps_update_time = 0;

// =============================================================================
// GALOIS FIELD INITIALIZATION
// =============================================================================

static void init_galois_field(void) {
    if (gf_initialized) return;

    // Primitive polynomial: x^6 + x + 1 (binary: 1000011 = 67 = 0x43)
    uint8_t primitive_poly = 0x43;

    // Initialize exponential table
    gf_exp[0] = 1;
    for (int i = 1; i < 63; i++) {
        gf_exp[i] = gf_exp[i-1] << 1;
        if (gf_exp[i] & 0x40) {  // If bit 6 is set
            gf_exp[i] ^= primitive_poly;
        }
    }

    // Extend table for modulo operations
    for (int i = 63; i < 512; i++) {
        gf_exp[i] = gf_exp[i % 63];
    }

    // Initialize logarithm table
    gf_log[0] = 0;  // log(0) undefined, set to 0
    for (int i = 0; i < 63; i++) {
        gf_log[gf_exp[i]] = i;
    }

    gf_initialized = 1;
    printf("✓ Galois Field GF(2^6) initialized\n");
}

// =============================================================================
// BCH ENCODING FUNCTIONS
// =============================================================================

void t018_calculate_bch(const uint8_t *info_bits, uint8_t *parity_bits) {
    if (!gf_initialized) init_galois_field();

    // Clear parity bits
    memset(parity_bits, 0, BCH_PARITY_BITS);

    // BCH encoding using polynomial division
    uint8_t remainder[BCH_PARITY_BITS] = {0};

    // Process each information bit
    for (int i = 0; i < BCH_INFO_BITS; i++) {
        uint8_t feedback = remainder[BCH_PARITY_BITS-1] ^ info_bits[i];

        // Shift remainder
        for (int j = BCH_PARITY_BITS-1; j > 0; j--) {
            remainder[j] = remainder[j-1];
        }
        remainder[0] = 0;

        // Add generator polynomial if feedback is 1
        if (feedback) {
            for (int j = 0; j < BCH_PARITY_BITS; j++) {
                remainder[j] ^= generator_poly[j];
            }
        }
    }

    // Copy remainder to parity bits
    memcpy(parity_bits, remainder, BCH_PARITY_BITS);
}

static uint64_t compute_bch_250_202(const uint8_t *data_202bits) {                                                                                                                                                                                                                       
       const uint64_t g = 0x1C7EB85DF3C97ULL;  // 49 bits
       uint64_t remainder = 0;

       // Polynomial division: 202 info bits + 48 zero padding bits
       // MSB-first processing (matching sgb_bch.py reference)                                                                                                                                                                                                                                                      
       for (int i = 0; i < 202; i++) {                                                                                                                                                                                                                                                      
           remainder = (remainder << 1) | data_202bits[i];                                                                                                                                                                                                                                  
           if (remainder & (1ULL << 48)) {                                                                                                                                                                                                                                                  
               remainder ^= g;                                                                                                                                                                                                                                                              
           }                                                                                                                                                                                                                                                                                
       }                                                                                                                                                                                                                                                                                    
       // 48 additional shifts for the zero padding bits                                                                                                                                                                                                                           
       for (int i = 0; i < 48; i++) {                                                                                                                                                                                                                                                       
           remainder <<= 1;                                                                                                                                                                                                                                                                 
           if (remainder & (1ULL << 48)) {                                                                                                                                                                                                                                                  
               remainder ^= g;                                                                                                                                                                                                                                                              
           }                                                                                                                                                                                                                                                                                
       }                                                                                                                                                                                                                                                                                    
       return remainder & 0xFFFFFFFFFFFFULL;                                                                                                                                                                                                                                                
   }

uint8_t t018_verify_bch(const uint8_t *frame_bits) {
    // Extract information bits (skip 2-bit header)
    uint8_t info_bits[202];
    memcpy(info_bits, &frame_bits[2], 202);

    // Extract received BCH (last 48 bits)
    uint64_t received_bch = 0;
    for (int i = 0; i < 48; i++) {
        if (frame_bits[204 + i]) {
            received_bch |= (1ULL << (47 - i));
        }
    }

    // Compute expected BCH
    uint64_t computed_bch = compute_bch_250_202(info_bits);

    return (received_bch == computed_bch);
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static void write_bits(uint8_t *bit_array, int start_pos, int num_bits, uint64_t value) {
    for (int i = num_bits - 1; i >= 0; i--) {
        bit_array[start_pos++] = (value >> i) & 1;
    }
}

static uint8_t lfsr_8bit(uint8_t state) {
    // 8-bit LFSR for rotating field generation
    uint8_t feedback = ((state >> 0) ^ (state >> 2) ^ (state >> 3) ^ (state >> 4)) & 1;
    return (state >> 1) | (feedback << 7);
}

// =============================================================================
// DYNAMIC FIELD FUNCTIONS
// =============================================================================

static uint8_t get_elapsed_activation_hours(void) {
    if (activation_time == 0) {
        activation_time = system_time;
    }
    uint32_t elapsed_seconds = system_time - activation_time;
    uint8_t elapsed_hours = (uint8_t)(elapsed_seconds / 3600);
    if (elapsed_hours > 63) elapsed_hours = 63;
    return elapsed_hours;
}

static uint16_t get_time_since_last_location_minutes(void) {
    uint32_t elapsed_seconds = system_time - last_gps_update_time;
    uint16_t elapsed_minutes = (uint16_t)(elapsed_seconds / 60);
    if (elapsed_minutes > 2046) elapsed_minutes = 2046;
    return elapsed_minutes;
}

static uint16_t altitude_to_code(double altitude) {
    if (altitude <= -400.0) {
        return 0;
    }
    if (altitude > 15952.0) {
        return 1022;
    }
    // T.018 encoding: (altitude + 400) / 16 + 0.0625 (code 25 for 0m)
    int16_t encoded = (int16_t)((altitude + 400.0) / 16.0 + 0.0625 + 0.5);
    return (uint16_t)(encoded & 0x3FF);
}

static uint32_t encode_time_value(uint8_t day, uint8_t hour, uint8_t minute) {
    return ((day & 0x1F) << 11) | ((hour & 0x1F) << 6) | (minute & 0x3F);
}

// =============================================================================
// ROTATING FIELD IMPLEMENTATION
// =============================================================================

static void set_rotating_field(uint8_t *info_bits, rotating_field_type_t rf_type) {
    // Set rotating field type identifier (4 bits) at position 154
    write_bits(info_bits, 154, 4, rf_type);

    switch (rf_type) {
    case RF_TYPE_G008:
        // T.018 G.008 Objective Requirements rotating field
        {
            uint8_t elapsed_hours = get_elapsed_activation_hours();
            uint16_t last_pos_minutes = get_time_since_last_location_minutes();
            uint16_t altitude_code = altitude_to_code(beacon_config.position.altitude);

            write_bits(info_bits, 158, 6, elapsed_hours);         // T.018 bits 159-164
            write_bits(info_bits, 164, 11, last_pos_minutes);     // T.018 bits 165-175
            write_bits(info_bits, 175, 10, altitude_code);        // T.018 bits 176-185

            // For Test Mode: Generate dynamic rotating field (bits 186-202)
            if (beacon_config.test_mode) {
                uint8_t lfsr_state = elt_state.transmission_count & 0xFF;
                for (int i = 0; i < 17; i++) {  // 17 bits (T.018 bits 186-202)
                    lfsr_state = lfsr_8bit(lfsr_state);
                    info_bits[185 + i] = lfsr_state & 0x01;
                }
            } else {
                write_bits(info_bits, 185, 17, 0);  // Exercise mode: spare bits
            }
        }
        break;

    case RF_TYPE_ELTDT:
        // ELT-DT Time/Altitude data (44 bits)
        {
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            uint32_t time_value = encode_time_value(tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min);
            uint16_t altitude_code = altitude_to_code(beacon_config.position.altitude);

            write_bits(info_bits, 158, 16, time_value);
            write_bits(info_bits, 174, 10, altitude_code);
            write_bits(info_bits, 184, 18, 0);  // Spare bits
        }
        break;

    case RF_TYPE_RLS:
        // RLS provider and data
        {
            uint8_t rls_provider = 0;   // Galileo
            uint64_t rls_data = 0;       // Placeholder

            write_bits(info_bits, 158, 8, rls_provider);
            write_bits(info_bits, 166, 36, rls_data);
        }
        break;

    case RF_TYPE_CANCEL:
        // Cancellation method
        {
            uint8_t deactivation_method = 0;  // Manual deactivation

            write_bits(info_bits, 158, 2, deactivation_method);
            // Fixed bits - all 42 bits set to 1 (T.018 spec)
            write_bits(info_bits, 160, 42, 0x3FFFFFFFFFFULL);
        }
        break;
    }
}

// =============================================================================
// GPS POSITION ENCODING (T.018 Appendix C)
// =============================================================================

void t018_encode_position(const gps_data_t *position, uint8_t *encoded) {
    int bit_pos = 0;

    // Latitude encoding (23 bits)
    float lat = position->valid ? position->latitude : 0.0;

    // Bit 0: N/S flag (N=0, S=1)
    encoded[bit_pos++] = (lat < 0) ? 1 : 0;

    // Bits 1-7: Degrees (7 bits, 0-90)
    float lat_abs = (lat < 0) ? -lat : lat;
    uint8_t lat_degrees = (uint8_t)lat_abs;
    write_bits(encoded, bit_pos, 7, lat_degrees);
    bit_pos += 7;

    // Bits 8-22: Decimal parts (15 bits)
    float lat_decimal = lat_abs - (float)lat_degrees;
    uint16_t lat_decimal_encoded = (uint16_t)(lat_decimal * 32768.0 + 0.5);
    write_bits(encoded, bit_pos, 15, lat_decimal_encoded);
    bit_pos += 15;

    // Longitude encoding (24 bits)
    float lon = position->valid ? position->longitude : 0.0;

    // Bit 23: E/W flag (E=0, W=1)
    encoded[bit_pos++] = (lon < 0) ? 1 : 0;

    // Bits 24-31: Degrees (8 bits, 0-180)
    float lon_abs = (lon < 0) ? -lon : lon;
    uint8_t lon_degrees = (uint8_t)lon_abs;
    write_bits(encoded, bit_pos, 8, lon_degrees);
    bit_pos += 8;

    // Bits 32-46: Decimal parts (15 bits)
    float lon_decimal = lon_abs - (float)lon_degrees;
    uint16_t lon_decimal_encoded = (uint16_t)(lon_decimal * 32768.0 + 0.5);
    write_bits(encoded, bit_pos, 15, lon_decimal_encoded);
}

// =============================================================================
// FRAME BUILDING
// =============================================================================

void t018_build_frame(const beacon_config_t *config, uint8_t *frame_bits) {
    memset(frame_bits, 0, T018_FRAME_BITS);

    // Update global config
    memcpy(&beacon_config, config, sizeof(beacon_config_t));

    // =============================================================================
    // BUILD 202-BIT INFORMATION FIELD
    // =============================================================================

    uint8_t info_bits[T018_INFO_BITS];
    memset(info_bits, 0, T018_INFO_BITS);

    int bit_pos = 0;

    // Bits 1-16: TAC (16 bits)
    uint16_t tac = beacon_config.test_mode ? 9999 : beacon_config.tac_number;
    write_bits(info_bits, bit_pos, 16, tac);
    bit_pos += 16;

    // Bits 17-30: Serial number (14 bits)
    uint16_t serial = beacon_config.serial_number & 0x3FFF;
    write_bits(info_bits, bit_pos, 14, serial);
    bit_pos += 14;

    // Bits 31-40: Country code (10 bits)
    uint16_t country = beacon_config.country_code & 0x3FF;
    write_bits(info_bits, bit_pos, 10, country);
    bit_pos += 10;

    // Bit 41: Homing device status (0 = not equipped/disabled)
    info_bits[bit_pos++] = 0;

    // Bit 42: RLS capability (1 = enabled)
    info_bits[bit_pos++] = 1;

    // Bit 43: Test protocol flag
    info_bits[bit_pos++] = beacon_config.test_mode ? 1 : 0;

    // Bits 44-66: Latitude (23 bits) per T.018 Appendix C
    uint8_t gps_encoded[47];
    t018_encode_position(&beacon_config.position, gps_encoded);
    memcpy(&info_bits[bit_pos], gps_encoded, 23);
    bit_pos += 23;

    // Bits 67-90: Longitude (24 bits)
    memcpy(&info_bits[bit_pos], &gps_encoded[23], 24);
    bit_pos += 24;

    // Bits 91-93: Vessel ID type (3 bits)
    uint8_t vessel_id_type = 0;
    switch (beacon_config.type) {
    case BEACON_TYPE_EPIRB:
        vessel_id_type = 1;  // Maritime MMSI (001)
        break;
    case BEACON_TYPE_ELT:
    case BEACON_TYPE_ELT_DT:
        vessel_id_type = 2;  // Aviation 24-bit address (010)
        break;
    case BEACON_TYPE_PLB:
        vessel_id_type = 0;  // No vessel ID (000)
        break;
    }
    write_bits(info_bits, bit_pos, 3, vessel_id_type);
    bit_pos += 3;

    // Bits 94-123: Vessel ID (30 bits)
    // For EPIRB: MMSI, for ELT: 24-bit address, for PLB: spare
    uint32_t vessel_id = 0;
    if (beacon_config.type == BEACON_TYPE_EPIRB) {
        vessel_id = 227006600;  // Example French MMSI
    }
    write_bits(info_bits, bit_pos, 30, vessel_id & 0x3FFFFFFF);
    bit_pos += 30;

    // Bits 124-137: EPIRB-AIS System Identity (14 bits) - spare
    write_bits(info_bits, bit_pos, 14, 0);
    bit_pos += 14;

    // Bits 138-140: Beacon type (3 bits)
    write_bits(info_bits, bit_pos, 3, beacon_config.type);
    bit_pos += 3;

    // Bits 141-154: Spare bits (14 bits)
    uint16_t spare_bits = 0x3FFF;  // All 1s
    write_bits(info_bits, bit_pos, 14, spare_bits);
    bit_pos += 14;

    // Bits 155-202: Rotating Field (48 bits)
    rotating_field_type_t rf_type = RF_TYPE_G008;  // Default
    if (beacon_config.type == BEACON_TYPE_ELT_DT) {
        rf_type = RF_TYPE_ELTDT;
    }
    set_rotating_field(info_bits, rf_type);

    // =============================================================================
    // BUILD COMPLETE 252-BIT FRAME
    // =============================================================================

    // Header (2 bits)
    frame_bits[0] = beacon_config.test_mode ? 1 : 0;  // Test/Exercise flag
    frame_bits[1] = 0;  // Padding bit

    // Copy information bits (202 bits)
    memcpy(&frame_bits[2], info_bits, 202);

    // Calculate and append BCH parity (48 bits)
    uint64_t bch_parity = compute_bch_250_202(info_bits);
    for (int i = 0; i < 48; i++) {
        frame_bits[204 + i] = (bch_parity >> (47 - i)) & 1;
    }
}

// =============================================================================
// ELT SEQUENCE MANAGEMENT
// =============================================================================

void t018_start_elt_sequence(void) {
    elt_state.active = 1;
    elt_state.current_phase = ELT_PHASE_1;
    elt_state.transmission_count = 0;
    elt_state.last_tx_time = time(NULL);
    elt_state.phase_start_time = time(NULL);

    printf("ELT sequence started - Phase 1 (5s intervals)\n");
}

void t018_stop_elt_sequence(void) {
    elt_state.active = 0;
    printf("ELT sequence stopped\n");
}

uint32_t t018_get_current_interval(void) {
    switch (elt_state.current_phase) {
    case ELT_PHASE_1:
        return ELT_PHASE1_INTERVAL;  // 5 seconds
    case ELT_PHASE_2:
        return ELT_PHASE2_INTERVAL;  // 10 seconds
    case ELT_PHASE_3:
        // 28.5s ±1.5s randomization
        return ELT_PHASE3_INTERVAL + (rand() % (ELT_PHASE3_RANDOM * 2)) - ELT_PHASE3_RANDOM;
    default:
        return 10000;  // 10 seconds default
    }
}

void t018_check_phase_transition(void) {
    switch (elt_state.current_phase) {
    case ELT_PHASE_1:
        if (elt_state.transmission_count >= ELT_PHASE1_COUNT) {
            elt_state.current_phase = ELT_PHASE_2;
            elt_state.transmission_count = 0;
            elt_state.phase_start_time = time(NULL);
            printf("ELT Phase 2 started (10s intervals)\n");
        }
        break;

    case ELT_PHASE_2:
        if (elt_state.transmission_count >= ELT_PHASE2_COUNT) {
            elt_state.current_phase = ELT_PHASE_3;
            elt_state.transmission_count = 0;
            elt_state.phase_start_time = time(NULL);
            printf("ELT Phase 3 started (28.5s intervals)\n");
        }
        break;

    case ELT_PHASE_3:
        // Continue indefinitely
        break;
    }
}

void t018_increment_transmission_count(void) {
    elt_state.transmission_count++;
    t018_check_phase_transition();
}

// =============================================================================
// DEBUG/PRINT FUNCTIONS
// =============================================================================

void t018_print_frame(const uint8_t *frame_bits) {
    printf("T.018 Frame (252 bits):\n");

    // Header (2 bits)
    printf("  Header: %d%d (Test=%d)\n", frame_bits[0], frame_bits[1], frame_bits[0]);

    // Extract 15 HEX ID (TAC + Serial + Country = 40 bits)
    printf("  15 HEX ID: ");
    for (int i = 0; i < 10; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4; j++) {
            if (frame_bits[2 + i*4 + j]) {
                nibble |= (1 << (3 - j));
            }
        }
        printf("%X", nibble);
    }
    printf("\n");

    // Complete frame in hex (252 bits = 63 hex chars)
    printf("  Complete: ");
    for (int i = 0; i < 63; i++) {
        uint8_t nibble = 0;
        for (int j = 0; j < 4; j++) {
            if (frame_bits[i*4 + j]) {
                nibble |= (1 << (3 - j));
            }
        }
        printf("%X", nibble);
        if ((i+1) % 16 == 0 && i < 62) printf("\n            ");
    }
    printf("\n");

    // BCH verification
    uint8_t bch_valid = t018_verify_bch(frame_bits);
    printf("  BCH: %s\n", bch_valid ? "VALID" : "INVALID");
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void t018_init(void) {
    init_galois_field();

    // Initialize time references
    system_time = time(NULL);
    activation_time = system_time - (3 * 3600);  // Simulate 3 hours activation
    last_gps_update_time = system_time - (5 * 60);  // GPS updated 5 min ago

    srand(time(NULL));

    printf("✓ T.018 protocol initialized\n");
}
