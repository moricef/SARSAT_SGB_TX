/**
 * @file main.c
 * @brief COSPAS-SARSAT T.018 (2nd Generation) Beacon Transmitter
 *
 * Main application for transmitting T.018 2nd generation beacon signals
 * via ADALM-PLUTO (PlutoSDR) on Odroid-C4.
 *
 * Features:
 * - Complete T.018 protocol implementation
 * - OQPSK modulation with DSSS spreading
 * - BCH(250,202) error correction
 * - GPS position encoding
 * - ELT sequence management (3 phases)
 * - PlutoSDR transmission via libiio
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "t018_protocol.h"
#include "oqpsk_modulator.h"
#include "pluto_control.h"
#include "prn_generator.h"

// =============================================================================
// GLOBAL VARIABLES
// =============================================================================

static volatile uint8_t running = 1;
static pluto_ctx_t pluto_ctx;

// =============================================================================
// SIGNAL HANDLER
// =============================================================================

void signal_handler(int signum) {
    (void)signum;
    printf("\n\nShutdown signal received...\n");
    running = 0;
}

// =============================================================================
// CONFIGURATION
// =============================================================================

typedef struct {
    // Beacon identity
    beacon_type_t beacon_type;
    uint16_t country_code;
    uint32_t tac_number;
    uint32_t serial_number;
    uint8_t test_mode;

    // GPS position
    double latitude;
    double longitude;
    uint16_t altitude;

    // Transmission parameters
    uint64_t frequency;
    int32_t tx_gain_db;
    uint32_t tx_interval_sec;

    // PlutoSDR
    char pluto_uri[128];

    // File output (optional)
    char output_file[256];
    uint8_t file_mode;
} app_config_t;

// Default configuration (France, EPIRB training)
static app_config_t default_config = {
    .beacon_type = BEACON_TYPE_EPIRB,
    .country_code = 227,            // France MID 227
    .tac_number = 10001,
    .serial_number = 13398,
    .test_mode = 1,                 // Test mode

    .latitude = 43.2,               // Marseille offshore
    .longitude = 5.4,               // Mediterranean
    .altitude = 0,

    .frequency = 403000000ULL,      // 403 MHz (training)
    .tx_gain_db = 0,                // Maximum power for testing (0 dB attenuation)
    .tx_interval_sec = 10,          // 10 seconds for test mode

    .pluto_uri = "ip:192.168.2.1",
    .output_file = "",
    .file_mode = 0
};

// =============================================================================
// CLI FUNCTIONS
// =============================================================================

void print_usage(const char *progname) {
    printf("COSPAS-SARSAT T.018 (2nd Generation) Beacon Transmitter\n");
    printf("Usage: %s [options]\n\n", progname);
    printf("Options:\n");
    printf("  -f <freq>     Frequency in Hz (default: 403000000)\n");
    printf("  -g <gain>     TX gain in dB (default: -10)\n");
    printf("  -t <type>     Beacon type: 0=EPIRB, 1=PLB, 2=ELT, 3=ELT-DT (default: 0)\n");
    printf("  -c <code>     Country code (MID) (default: 227 for France)\n");
    printf("  -s <serial>   Serial number (default: 13398)\n");
    printf("  -m <mode>     Test mode: 0=Exercise, 1=Test (default: 1)\n");
    printf("  -i <sec>      TX interval in seconds (default: 10)\n");
    printf("  -lat <lat>    Latitude in degrees (default: 43.2)\n");
    printf("  -lon <lon>    Longitude in degrees (default: 5.4)\n");
    printf("  -alt <alt>    Altitude in meters (default: 0)\n");
    printf("  -u <uri>      PlutoSDR URI (default: ip:192.168.2.1)\n");
    printf("  -o <file>     Save I/Q to file instead of transmitting\n");
    printf("  -h            Show this help\n\n");
    printf("Beacon Types:\n");
    printf("  0 = EPIRB (Emergency Position Indicating Radio Beacon)\n");
    printf("  1 = PLB (Personal Locator Beacon)\n");
    printf("  2 = ELT (Emergency Locator Transmitter)\n");
    printf("  3 = ELT-DT (ELT with homing signal)\n\n");
    printf("Examples:\n");
    printf("  %s -f 403000000 -g -10 -m 1\n", progname);
    printf("  %s -t 0 -c 227 -lat 43.2 -lon 5.4 -i 120\n", progname);
}

int parse_args(int argc, char *argv[], app_config_t *config) {
    *config = default_config;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            config->frequency = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            config->tx_gain_db = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            config->beacon_type = (beacon_type_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config->country_code = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config->serial_number = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            config->test_mode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            config->tx_interval_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-lat") == 0 && i + 1 < argc) {
            config->latitude = atof(argv[++i]);
        } else if (strcmp(argv[i], "-lon") == 0 && i + 1 < argc) {
            config->longitude = atof(argv[++i]);
        } else if (strcmp(argv[i], "-alt") == 0 && i + 1 < argc) {
            config->altitude = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            strncpy(config->pluto_uri, argv[++i], sizeof(config->pluto_uri) - 1);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            strncpy(config->output_file, argv[++i], sizeof(config->output_file) - 1);
            config->file_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return -1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
}

void print_config(const app_config_t *config) {
    const char *beacon_names[] = {"EPIRB", "PLB", "ELT", "ELT-DT"};

    printf("\n=== T.018 (2G) Beacon Configuration ===\n");
    printf("Beacon Type:  %s\n", beacon_names[config->beacon_type]);
    printf("Country Code: %u (MID)\n", config->country_code);
    printf("TAC Number:   %u\n", config->tac_number);
    printf("Serial:       %u\n", config->serial_number);
    printf("Mode:         %s\n", config->test_mode ? "TEST" : "EXERCISE");
    printf("\nPosition:\n");
    printf("  Latitude:   %.6f°\n", config->latitude);
    printf("  Longitude:  %.6f°\n", config->longitude);
    printf("  Altitude:   %u m\n", config->altitude);
    printf("\nTransmission:\n");
    printf("  Frequency:  %llu Hz (%.3f MHz)\n",
           (unsigned long long)config->frequency, config->frequency / 1e6);
    printf("  TX Gain:    %d dB\n", config->tx_gain_db);
    printf("  Interval:   %u seconds\n", config->tx_interval_sec);

    if (config->file_mode) {
        printf("  Mode:       FILE OUTPUT\n");
        printf("  Output:     %s\n", config->output_file);
    } else {
        printf("  Mode:       PLUTO TX\n");
        printf("  PlutoSDR:   %s\n", config->pluto_uri);
    }
    printf("=======================================\n\n");
}

// =============================================================================
// TRANSMISSION FUNCTION
// =============================================================================

int transmit_beacon(const app_config_t *config) {
    printf("\n--- Building T.018 Frame ---\n");

    // Build beacon configuration
    beacon_config_t beacon_cfg = {
        .type = config->beacon_type,
        .country_code = config->country_code,
        .tac_number = config->tac_number,
        .serial_number = config->serial_number,
        .test_mode = config->test_mode,
        .position = {
            .latitude = config->latitude,
            .longitude = config->longitude,
            .altitude = config->altitude,
            .valid = 1
        }
    };

    // Build 252-bit frame
    uint8_t frame_bits[T018_FRAME_BITS];
    t018_build_frame(&beacon_cfg, frame_bits);

    // Print frame info
    t018_print_frame(frame_bits);

    // Modulate frame
    printf("\n--- OQPSK Modulation ---\n");
    float complex *iq_samples = malloc(OQPSK_TOTAL_SAMPLES * sizeof(float complex));
    if (!iq_samples) {
        fprintf(stderr, "Failed to allocate I/Q buffer\n");
        return -1;
    }

    uint32_t num_samples = oqpsk_modulate_frame(frame_bits, iq_samples);
    printf("Generated %u I/Q samples\n", num_samples);

    // Verify modulation
    if (!oqpsk_verify_output(iq_samples, num_samples)) {
        fprintf(stderr, "OQPSK verification failed\n");
        free(iq_samples);
        return -1;
    }

    // Transmit or save to file
    int result = 0;

    if (config->file_mode) {
        // Save to file
        printf("\n--- Saving to File ---\n");
        result = pluto_save_iq_file(config->output_file, iq_samples, num_samples, OQPSK_SAMPLE_RATE);
    } else {
        // Transmit via PlutoSDR
        printf("\n--- Transmitting via PlutoSDR ---\n");
        result = pluto_transmit_iq(&pluto_ctx, iq_samples, num_samples);
    }

    free(iq_samples);

    if (result < 0) {
        fprintf(stderr, "%s failed\n", config->file_mode ? "File save" : "Transmission");
        return -1;
    }

    printf("✓ %s complete\n", config->file_mode ? "File save" : "Transmission");
    return 0;
}

// =============================================================================
// MAIN APPLICATION
// =============================================================================

int main(int argc, char *argv[]) {
    app_config_t config;

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║ COSPAS-SARSAT T.018 (2nd Generation) Beacon Transmitter  ║\n");
    printf("║ Platform: Odroid-C4 + ADALM-PLUTO                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");

    // Install signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse command line arguments
    if (parse_args(argc, argv, &config) < 0) {
        return 1;
    }

    print_config(&config);

    // Initialize T.018 protocol
    printf("--- Initialization ---\n");
    t018_init();

    // Verify PRN generator
    printf("Verifying PRN generator...\n");
    if (!prn_verify_table_2_2()) {
        fprintf(stderr, "PRN verification failed!\n");
        return 1;
    }

    // Initialize PlutoSDR (skip in file mode)
    if (!config.file_mode) {
        printf("Initializing PlutoSDR...\n");
        if (pluto_init(&pluto_ctx, config.pluto_uri) < 0) {
            fprintf(stderr, "PlutoSDR initialization failed\n");
            return 1;
        }

        pluto_print_info(&pluto_ctx);

        // Configure TX
        printf("Configuring TX...\n");
        if (pluto_configure_tx(&pluto_ctx, config.frequency, config.tx_gain_db,
                              PLUTO_SAMPLE_RATE) < 0) {
            fprintf(stderr, "TX configuration failed\n");
            pluto_cleanup(&pluto_ctx);
            return 1;
        }
    } else {
        printf("File output mode - skipping PlutoSDR initialization\n");
    }

    // Main transmission loop
    printf("\n╔═══════════════════════════════════════════╗\n");
    if (config.file_mode) {
        printf("║ File Generation Mode                     ║\n");
        printf("║ Press Ctrl+C to stop                     ║\n");
    } else {
        printf("║ Starting Transmission Loop               ║\n");
        printf("║ Press Ctrl+C to stop                     ║\n");
    }
    printf("╚═══════════════════════════════════════════╝\n");

    uint32_t tx_count = 0;
    time_t start_time = time(NULL);

    while (running) {
        tx_count++;
        time_t current_time = time(NULL);
        printf("\n╔═════════════════════════════════════════════════╗\n");
        printf("║ Transmission #%u                                \n", tx_count);
        printf("║ Time: %s", ctime(&current_time));
        printf("║ Uptime: %ld seconds                             \n", current_time - start_time);
        printf("╚═════════════════════════════════════════════════╝\n");

        // Transmit beacon
        if (transmit_beacon(&config) < 0) {
            fprintf(stderr, "Transmission failed, stopping...\n");
            break;
        }

        // Increment transmission count for rotating field
        t018_increment_transmission_count();

        // In file mode, generate only one frame then exit
        if (config.file_mode) {
            printf("\n✓ File mode: Single frame generated, exiting...\n");
            break;
        }

        // Wait for next transmission
        if (running) {
            printf("\nWaiting %u seconds for next transmission...\n", config.tx_interval_sec);
            for (uint32_t i = 0; i < config.tx_interval_sec && running; i++) {
                sleep(1);
            }
        }
    }

    // Cleanup
    printf("\n\n╔═══════════════════════════════════════════╗\n");
    printf("║ Shutting Down                            ║\n");
    printf("╚═══════════════════════════════════════════╝\n");

    if (!config.file_mode) {
        pluto_cleanup(&pluto_ctx);
    }

    printf("\nTransmission Statistics:\n");
    printf("  Total transmissions: %u\n", tx_count);
    printf("  Total runtime: %ld seconds\n", time(NULL) - start_time);

    printf("\n✓ Shutdown complete\n");
    return 0;
}
