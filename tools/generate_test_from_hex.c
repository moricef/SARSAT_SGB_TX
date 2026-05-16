/**
 * @file generate_test_from_hex.c
 * @brief Generate T.018 test signal from hexadecimal frame
 *
 * Takes a 252-bit T.018 frame in hex format and generates:
 * - Complete OQPSK modulated signal with 50-bit preamble
 * - IQ samples ready for transmission or demodulator testing
 *
 * Usage: ./generate_test_from_hex <hex_frame>
 * Example: ./generate_test_from_hex 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <ctype.h>
#include "../include/oqpsk_modulator.h"

#define FRAME_BITS 252
#define DATA_BITS 250

/**
 * @brief Convert hex string to bit array
 * @param hex_string Input hex string (no spaces, no 0x prefix)
 * @param bits Output bit array
 * @param num_bits Expected number of bits
 * @return 1 if success, 0 if error
 */
int hex_to_bits(const char *hex_string, uint8_t *bits, int num_bits) {
    int hex_len = strlen(hex_string);
    int expected_hex_len = (num_bits + 3) / 4;  // Round up

    if (hex_len != expected_hex_len) {
        fprintf(stderr, "Error: Expected %d hex characters for %d bits, got %d\n",
                expected_hex_len, num_bits, hex_len);
        return 0;
    }

    // Convert each hex character to 4 bits
    for (int i = 0; i < hex_len; i++) {
        char c = toupper(hex_string[i]);
        int nibble;

        if (c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10;
        } else {
            fprintf(stderr, "Error: Invalid hex character '%c' at position %d\n", c, i);
            return 0;
        }

        // Convert nibble to 4 bits
        for (int b = 0; b < 4 && (i * 4 + b) < num_bits; b++) {
            bits[i * 4 + b] = (nibble >> (3 - b)) & 1;
        }
    }

    return 1;
}

/**
 * @brief Print frame in hex format
 */
void print_hex_frame(const uint8_t *bits, int num_bits) {
    printf("Frame (hex): ");
    for (int i = 0; i < num_bits; i += 4) {
        int nibble = 0;
        for (int b = 0; b < 4 && (i + b) < num_bits; b++) {
            nibble = (nibble << 1) | bits[i + b];
        }
        printf("%X", nibble);
    }
    printf("\n");
}

/**
 * @brief Print frame structure (header + data)
 */
void print_frame_structure(const uint8_t *frame_bits) {
    printf("\nFrame structure (252 bits):\n");
    printf("  Header (2 bits):  %d%d\n", frame_bits[0], frame_bits[1]);

    printf("  Data (250 bits):\n");
    for (int i = 0; i < 250; i += 50) {
        printf("    Bits %3d-%3d: ", i+2, i+2+49 < 252 ? i+2+49 : 251);
        for (int j = 0; j < 50 && (i + j + 2) < 252; j++) {
            printf("%d", frame_bits[i + j + 2]);
            if ((j + 1) % 10 == 0) printf(" ");
        }
        printf("\n");
    }
}

/**
 * @brief Save frame bits to file
 */
void save_frame_bits(const uint8_t *bits, int num_bits, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Warning: Failed to open %s for writing\n", filename);
        return;
    }

    fprintf(f, "# T.018 Frame (%d bits)\n", num_bits);
    for (int i = 0; i < num_bits; i++) {
        fprintf(f, "%d", bits[i]);
        if ((i + 1) % 50 == 0) fprintf(f, "\n");
        else if ((i + 1) % 10 == 0) fprintf(f, " ");
    }
    if (num_bits % 50 != 0) fprintf(f, "\n");

    fclose(f);
    printf("✓ Frame bits saved to %s\n", filename);
}

/**
 * @brief Save IQ samples to file
 */
void save_iq_file(const float complex *iq_samples, uint32_t num_samples, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", filename);
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
 * @brief Main program
 */
int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("T.018 Test Signal Generator (from Hex)\n");
    printf("========================================\n\n");

    // Check arguments
    if (argc < 2) {
        printf("Usage: %s <hex_frame> [output_prefix]\n\n", argv[0]);
        printf("Arguments:\n");
        printf("  hex_frame      - 252-bit T.018 frame in hex (63 characters)\n");
        printf("  output_prefix  - Optional output file prefix (default: test_frame)\n\n");
        printf("Example:\n");
        printf("  %s 89C3F45638D95999A02B33326C3EC4400003FFF00C028320000E899A09C80A4\n\n", argv[0]);
        printf("Output files:\n");
        printf("  <prefix>.iq         - IQ samples (complex float32)\n");
        printf("  <prefix>_frame.txt  - Frame bits (human readable)\n");
        printf("  <prefix>_data.bin   - Data bits only (250 bits)\n");
        return 1;
    }

    const char *hex_frame = argv[1];
    const char *output_prefix = (argc >= 3) ? argv[2] : "test_frame";

    printf("Input hex frame: %s\n", hex_frame);
    printf("Hex length: %zu characters\n", strlen(hex_frame));
    printf("Expected: 63 characters (252 bits)\n\n");

    // Parse 252-bit frame
    uint8_t frame_bits[FRAME_BITS];
    if (!hex_to_bits(hex_frame, frame_bits, FRAME_BITS)) {
        fprintf(stderr, "Failed to parse hex frame\n");
        return 1;
    }

    printf("✓ Hex frame parsed successfully\n");
    print_hex_frame(frame_bits, FRAME_BITS);
    print_frame_structure(frame_bits);

    // Extract 250 data bits (skip 2-bit header)
    uint8_t data_bits[DATA_BITS];
    memcpy(data_bits, frame_bits + 2, DATA_BITS);

    printf("\n");
    printf("T.018 Transmission structure:\n");
    printf("  Preamble:  50 bits (all zeros) - added by modulator\n");
    printf("  Data:     250 bits (from hex frame bits 2-251)\n");
    printf("  Total:    300 bits transmitted\n");
    printf("\n");

    // Save frame bits
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_frame.txt", output_prefix);
    save_frame_bits(frame_bits, FRAME_BITS, filename);

    // Save data bits (binary)
    snprintf(filename, sizeof(filename), "%s_data.bin", output_prefix);
    FILE *f_data = fopen(filename, "wb");
    if (f_data) {
        // Save as packed bytes
        for (int i = 0; i < DATA_BITS; i += 8) {
            uint8_t byte = 0;
            for (int b = 0; b < 8 && (i + b) < DATA_BITS; b++) {
                byte |= (data_bits[i + b] << (7 - b));
            }
            fwrite(&byte, 1, 1, f_data);
        }
        fclose(f_data);
        printf("✓ Data bits saved to %s\n", filename);
    }

    printf("\n");

    // Allocate IQ buffer
    float complex *iq_samples = malloc(OQPSK_TOTAL_SAMPLES * sizeof(float complex));
    if (!iq_samples) {
        fprintf(stderr, "Failed to allocate IQ buffer\n");
        return 1;
    }

    printf("Modulating with OQPSK...\n");
    printf("  Sample rate: %d Hz (%.1f MHz)\n", OQPSK_SAMPLE_RATE, OQPSK_SAMPLE_RATE / 1e6);
    printf("  Chip rate: %d chips/s (%.1f kHz)\n", OQPSK_CHIP_RATE, OQPSK_CHIP_RATE / 1e3);
    printf("  Samples per chip: %.2f\n", OQPSK_SAMPLES_PER_CHIP);
    printf("  Duration: ~1000 ms\n");
    printf("\n");

    // Modulate using reference implementation
    // oqpsk_modulate_frame takes 250 data bits and adds preamble internally
    uint32_t num_samples = oqpsk_modulate_frame(data_bits, iq_samples);

    if (num_samples == 0) {
        fprintf(stderr, "✗ Modulation failed\n");
        free(iq_samples);
        return 1;
    }

    printf("\n✓ Modulation complete: %u samples generated\n", num_samples);

    // Verify output
    if (!oqpsk_verify_output(iq_samples, num_samples)) {
        fprintf(stderr, "✗ Output verification failed\n");
        free(iq_samples);
        return 1;
    }

    // Save IQ file
    printf("\n");
    snprintf(filename, sizeof(filename), "%s.iq", output_prefix);
    save_iq_file(iq_samples, num_samples, filename);

    // Print summary
    printf("\n");
    printf("========================================\n");
    printf("Generation Complete\n");
    printf("========================================\n");
    printf("Output files:\n");
    printf("  %s.iq         - IQ samples (%.1f MB)\n",
           output_prefix, (num_samples * 2 * sizeof(float)) / (1024.0f * 1024.0f));
    printf("  %s_frame.txt  - Complete frame (252 bits)\n", output_prefix);
    printf("  %s_data.bin   - Data only (250 bits packed)\n", output_prefix);
    printf("\n");
    printf("Signal parameters:\n");
    printf("  Samples: %u\n", num_samples);
    printf("  Duration: %.3f ms\n", (float)num_samples / OQPSK_SAMPLE_RATE * 1000.0f);
    printf("  Format: Complex float32 (I/Q interleaved)\n");
    printf("\n");
    printf("View with:\n");
    printf("  inspectrum %s.iq\n", output_prefix);
    printf("\n");

    free(iq_samples);
    return 0;
}
