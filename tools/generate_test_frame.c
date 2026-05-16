/**
 * @file generate_test_frame.c
 * @brief Generate complete T.018 test frame with known message
 *
 * Creates a test signal with:
 * - 50-bit preamble (all zeros as per T.018 §2.2.4)
 * - 250-bit known message pattern
 * - Full OQPSK modulation with RRC filtering
 * - Output: IQ file + reference message
 *
 * Usage: ./generate_test_frame
 * Output: test_frame_known.iq, test_frame_message.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include "../include/oqpsk_modulator.h"

// Test message patterns
typedef enum {
    MSG_ALL_ZEROS,      // All 0s (250 bits)
    MSG_ALL_ONES,       // All 1s (250 bits)
    MSG_ALTERNATING,    // 01010101... (250 bits)
    MSG_COUNTER,        // Binary counter (0,1,2,3,4...)
    MSG_CUSTOM          // Custom hex pattern
} message_pattern_t;

/**
 * @brief Generate known test message
 * @param pattern Message pattern type
 * @param message Output buffer (250 bits)
 */
void generate_test_message(message_pattern_t pattern, uint8_t *message) {
    switch (pattern) {
        case MSG_ALL_ZEROS:
            memset(message, 0, 250);
            printf("Message: ALL ZEROS (250 bits)\n");
            break;

        case MSG_ALL_ONES:
            memset(message, 1, 250);
            printf("Message: ALL ONES (250 bits)\n");
            break;

        case MSG_ALTERNATING:
            for (int i = 0; i < 250; i++) {
                message[i] = i % 2;
            }
            printf("Message: ALTERNATING 0101... (250 bits)\n");
            break;

        case MSG_COUNTER:
            // Binary counter: each byte = bit pattern of counter
            for (int i = 0; i < 250; i++) {
                int byte_num = i / 8;
                int bit_pos = 7 - (i % 8);  // MSB first
                message[i] = (byte_num >> bit_pos) & 1;
            }
            printf("Message: BINARY COUNTER (250 bits)\n");
            break;

        case MSG_CUSTOM:
            // Custom test pattern: "SARSAT TEST 406MHz BEACON"
            // ASCII encoding: S=0x53, A=0x41, R=0x52, etc.
            const char *test_string = "SARSAT TEST 406MHz BEACON";
            int len = strlen(test_string);

            for (int i = 0; i < 250; i++) {
                if (i < len * 8) {
                    int char_idx = i / 8;
                    int bit_pos = 7 - (i % 8);  // MSB first
                    message[i] = (test_string[char_idx] >> bit_pos) & 1;
                } else {
                    message[i] = 0;  // Pad with zeros
                }
            }
            printf("Message: CUSTOM \"%s\" (250 bits)\n", test_string);
            break;
    }
}

/**
 * @brief Print message in hex format
 */
void print_message_hex(const uint8_t *message, int num_bits) {
    printf("Message (hex): ");
    for (int i = 0; i < num_bits; i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8 && (i + b) < num_bits; b++) {
            byte |= (message[i + b] << (7 - b));
        }
        printf("%02X ", byte);
        if ((i + 8) % 64 == 0) printf("\n               ");
    }
    printf("\n");
}

/**
 * @brief Verify preamble is all zeros
 */
int verify_preamble(const uint8_t *frame) {
    printf("Verifying preamble (first 50 bits should be 0)...\n");
    for (int i = 0; i < 50; i++) {
        if (frame[i] != 0) {
            printf("✗ ERROR: Preamble bit %d is not 0 (value=%d)\n", i, frame[i]);
            return 0;
        }
    }
    printf("✓ Preamble verified: 50 bits at 0\n");
    return 1;
}

/**
 * @brief Save message to binary file
 */
void save_message_file(const uint8_t *message, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }

    // Save as packed bytes (8 bits per byte)
    for (int i = 0; i < 250; i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8 && (i + b) < 250; b++) {
            byte |= (message[i + b] << (7 - b));
        }
        fwrite(&byte, 1, 1, f);
    }

    fclose(f);
    printf("✓ Reference message saved to %s\n", filename);
}

/**
 * @brief Save IQ samples to file (complex float32)
 */
void save_iq_file(const float complex *iq_samples, uint32_t num_samples, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }

    // Save as interleaved float32: I, Q, I, Q, ...
    for (uint32_t i = 0; i < num_samples; i++) {
        float i_val = crealf(iq_samples[i]);
        float q_val = cimagf(iq_samples[i]);
        fwrite(&i_val, sizeof(float), 1, f);
        fwrite(&q_val, sizeof(float), 1, f);
    }

    fclose(f);
    printf("✓ IQ samples saved to %s (%u samples, %.1f MB)\n",
           filename, num_samples, (num_samples * 2 * sizeof(float)) / (1024.0f * 1024.0f));
}

/**
 * @brief Main test program
 */
int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("T.018 Test Frame Generator\n");
    printf("========================================\n\n");

    // Select message pattern (default: CUSTOM)
    message_pattern_t pattern = MSG_CUSTOM;

    if (argc > 1) {
        if (strcmp(argv[1], "zeros") == 0) pattern = MSG_ALL_ZEROS;
        else if (strcmp(argv[1], "ones") == 0) pattern = MSG_ALL_ONES;
        else if (strcmp(argv[1], "alt") == 0) pattern = MSG_ALTERNATING;
        else if (strcmp(argv[1], "counter") == 0) pattern = MSG_COUNTER;
        else if (strcmp(argv[1], "custom") == 0) pattern = MSG_CUSTOM;
        else {
            printf("Usage: %s [zeros|ones|alt|counter|custom]\n", argv[0]);
            printf("Default: custom\n");
            return 1;
        }
    }

    // Generate test message (250 bits)
    uint8_t message[250];
    generate_test_message(pattern, message);
    print_message_hex(message, 250);

    // Save reference message
    save_message_file(message, "test_frame_message.bin");

    // Also save as bit file for easier debugging
    FILE *f_bits = fopen("test_frame_message_bits.txt", "w");
    if (f_bits) {
        fprintf(f_bits, "# T.018 Test Message (250 bits)\n");
        fprintf(f_bits, "# Pattern: ");
        switch(pattern) {
            case MSG_ALL_ZEROS: fprintf(f_bits, "ALL ZEROS\n"); break;
            case MSG_ALL_ONES: fprintf(f_bits, "ALL ONES\n"); break;
            case MSG_ALTERNATING: fprintf(f_bits, "ALTERNATING\n"); break;
            case MSG_COUNTER: fprintf(f_bits, "COUNTER\n"); break;
            case MSG_CUSTOM: fprintf(f_bits, "CUSTOM\n"); break;
        }

        for (int i = 0; i < 250; i++) {
            fprintf(f_bits, "%d", message[i]);
            if ((i + 1) % 50 == 0) fprintf(f_bits, "\n");
            else if ((i + 1) % 10 == 0) fprintf(f_bits, " ");
        }
        fclose(f_bits);
        printf("✓ Reference bits saved to test_frame_message_bits.txt\n");
    }

    printf("\n");

    // Allocate IQ buffer (2.6M samples max)
    float complex *iq_samples = malloc(OQPSK_TOTAL_SAMPLES * sizeof(float complex));
    if (!iq_samples) {
        fprintf(stderr, "Failed to allocate IQ buffer\n");
        return 1;
    }

    printf("Modulating T.018 frame with OQPSK...\n");
    printf("  Sample rate: %d Hz\n", OQPSK_SAMPLE_RATE);
    printf("  Chip rate: %d chips/s\n", OQPSK_CHIP_RATE);
    printf("  Samples per chip: %.2f\n", OQPSK_SAMPLES_PER_CHIP);
    printf("  Frame: 50 preamble + 250 data = 300 bits\n");
    printf("  Duration: ~1000 ms\n");
    printf("\n");

    // Modulate frame using reference implementation
    uint32_t num_samples = oqpsk_modulate_frame(message, iq_samples);

    if (num_samples == 0) {
        fprintf(stderr, "✗ Modulation failed\n");
        free(iq_samples);
        return 1;
    }

    printf("\n");
    printf("✓ Modulation complete: %u samples generated\n", num_samples);

    // Verify output
    if (!oqpsk_verify_output(iq_samples, num_samples)) {
        fprintf(stderr, "✗ Output verification failed\n");
        free(iq_samples);
        return 1;
    }

    // Save IQ file
    printf("\n");
    save_iq_file(iq_samples, num_samples, "test_frame_known.iq");

    // Print statistics
    printf("\n");
    printf("========================================\n");
    printf("Test Frame Generation Complete\n");
    printf("========================================\n");
    printf("Output files:\n");
    printf("  - test_frame_known.iq          (IQ samples, float32)\n");
    printf("  - test_frame_message.bin       (message, packed bytes)\n");
    printf("  - test_frame_message_bits.txt  (message, ASCII bits)\n");
    printf("  - chips_after_spreading.bin    (debug: chips after DSSS)\n");
    printf("\n");
    printf("Expected samples: 1,228,800 (76,800 chips × 16 samp/chip)\n");
    printf("Actual samples:   %u\n", num_samples);
    printf("Duration:         %.3f ms\n", (float)num_samples / OQPSK_SAMPLE_RATE * 1000.0f);
    printf("\n");
    printf("Next steps:\n");
    printf("  1. View with inspectrum: inspectrum test_frame_known.iq\n");
    printf("  2. Demodulate and compare with test_frame_message.bin\n");
    printf("  3. Verify preamble detection (first 50 bits = 0)\n");
    printf("\n");

    free(iq_samples);
    return 0;
}
