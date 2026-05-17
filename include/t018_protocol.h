/**
 * @file t018_protocol.h
 * @brief T.018 Protocol Layer (BCH encoding, frame building)
 *
 * Handles 2nd generation COSPAS-SARSAT protocol:
 * - BCH(250,202) error correction
 * - Frame formatting (252 bits total)
 * - GPS coordinate encoding
 * - Country codes (MID)
 */

#ifndef T018_PROTOCOL_H
#define T018_PROTOCOL_H

#include <stdint.h>

// T.018 Frame structure
#define T018_INFO_BITS      202         // Information bits
#define T018_BCH_BITS       48          // BCH parity bits
#define T018_DATA_BITS      250         // Total data (info + BCH)
#define T018_HEADER_BITS    2           // Header bits
#define T018_FRAME_BITS     252         // Complete frame

// BCH(250,202) parameters
#define BCH_INFO_BITS       202
#define BCH_PARITY_BITS     48
#define BCH_PADDING_ZEROS   5           // Padding for 255-bit codeword
#define BCH_GENERATOR_POLY  0x1C7EB85DF3C97ULL  // g(x) 49 bits

// Beacon types
// Values match T.018 beacon type field (bits 138-140)
typedef enum {
    BEACON_TYPE_ELT = 0,        // "000" Emergency Locator Transmitter
    BEACON_TYPE_EPIRB = 1,      // "001" Emergency Position Indicating Radio Beacon
    BEACON_TYPE_PLB = 2,        // "010" Personal Locator Beacon
    BEACON_TYPE_ELT_DT = 3      // "011" ELT with homing signal
} beacon_type_t;

// GPS data structure
typedef struct {
    double latitude;            // Degrees (±90)
    double longitude;           // Degrees (±180)
    uint16_t altitude;          // Meters (0-65535)
    uint8_t valid;              // 1=valid, 0=invalid
} gps_data_t;

// Rotating field types
typedef enum {
    RF_TYPE_G008 = 0,       // G.008 Objective Requirements
    RF_TYPE_ELTDT = 1,      // ELT-DT Time/Altitude
    RF_TYPE_RLS = 2,        // RLS Type 1/2 Acknowledgment (T.018 Table 3.5)
    RF_TYPE_CANCEL = 3      // Cancellation
} rotating_field_type_t;

// Beacon configuration
typedef struct {
    beacon_type_t type;         // Beacon type
    uint16_t country_code;      // MID (10 bits, 227=France)
    uint32_t tac_number;        // Type Approval Certificate
    uint32_t serial_number;     // Unique serial
    uint8_t test_mode;          // 0=Normal, 1=Self-test
    gps_data_t position;        // GPS coordinates

    rotating_field_type_t rotating_field;  // Rotating field to emit

    // RLS rotating field (T.018 Table 3.5), used when rotating_field == RF_TYPE_RLS
    uint8_t rls_cap_auto;       // bit 7:  Type-1 automatic ack capability
    uint8_t rls_cap_manual;     // bit 8:  Type-2 manual ack capability
    uint8_t rls_provider;       // bits 13-15: 1=Galileo
    uint8_t rls_fb_type1;       // bit 16: RLM Type-1 received
    uint8_t rls_fb_type2;       // bit 17: RLM Type-2 received
    uint8_t rls_rlm_bits[20];   // bits 18-37: RLM echo (bits 61-80 of short RLM)
} beacon_config_t;

/**
 * @brief Initialize T.018 protocol
 */
void t018_init(void);

/**
 * @brief Build complete 252-bit T.018 frame
 * @param config Beacon configuration
 * @param frame_bits Output buffer (252 bits)
 */
void t018_build_frame(const beacon_config_t *config, uint8_t *frame_bits);

/**
 * @brief Calculate BCH(250,202) parity bits
 * @param info_bits Information bits (202 bits)
 * @param parity_bits Output BCH parity (48 bits)
 */
void t018_calculate_bch(const uint8_t *info_bits, uint8_t *parity_bits);

/**
 * @brief Verify BCH(250,202) codeword
 * @param frame_bits Complete 250-bit frame (202 info + 48 BCH)
 * @return 1 if valid, 0 if errors detected
 */
uint8_t t018_verify_bch(const uint8_t *frame_bits);

/**
 * @brief Encode GPS position (T.018 format)
 * @param position GPS data
 * @param encoded Output buffer (37 bits for lat/lon)
 */
void t018_encode_position(const gps_data_t *position, uint8_t *encoded);

/**
 * @brief Print frame details (for debugging)
 * @param frame_bits 252-bit frame
 */
void t018_print_frame(const uint8_t *frame_bits);

// =============================================================================
// ELT SEQUENCE MANAGEMENT
// =============================================================================

#define ELT_PHASE1_INTERVAL     5000    // 5 seconds
#define ELT_PHASE2_INTERVAL     10000   // 10 seconds
#define ELT_PHASE3_INTERVAL     28500   // 28.5 seconds
#define ELT_PHASE3_RANDOM       1500    // ±1.5s randomization
#define ELT_PHASE1_COUNT        36      // 3 minutes at 5s
#define ELT_PHASE2_COUNT        162     // 27 minutes at 10s

typedef enum {
    ELT_PHASE_1 = 0,
    ELT_PHASE_2 = 1,
    ELT_PHASE_3 = 2
} elt_phase_t;

typedef struct {
    elt_phase_t current_phase;
    uint16_t transmission_count;
    uint32_t last_tx_time;
    uint32_t phase_start_time;
    uint8_t active;
} elt_state_t;

/**
 * @brief Start ELT sequence
 */
void t018_start_elt_sequence(void);

/**
 * @brief Stop ELT sequence
 */
void t018_stop_elt_sequence(void);

/**
 * @brief Get current transmission interval (ms)
 * @return Interval in milliseconds based on current phase
 */
uint32_t t018_get_current_interval(void);

/**
 * @brief Check and perform phase transitions
 */
void t018_check_phase_transition(void);

/**
 * @brief Increment transmission count and check transitions
 */
void t018_increment_transmission_count(void);

#endif // T018_PROTOCOL_H
