/**
 * @file verify_prn.c
 * @brief Rigorous PRN verification against T.018 Table 2.2
 *
 * Verifies all four PRN initialization values:
 * - Normal I: 0x000001 → 8000 0108 4212 84A1
 * - Normal Q: 0x3583F2 → 3F83 58BA D030 F231
 * - Self-test I: 0x69E780 → 0F93 4A4D 4CF3 028D
 * - Self-test Q: 0x3CB948 → (to be verified)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../include/prn_generator.h"

// T.018 Table 2.2 reference values (first 64 chips in hex)
typedef struct {
    const char *name;
    uint32_t init_value;
    const char *expected_hex;
} prn_test_t;

static const prn_test_t tests[] = {
    {"Normal I",    0x000001, "8000010842128 4A1"},
    {"Normal Q",    0x1AC1FC, "3F8358BAD030F231"},
    {"Self-test I", 0x52C9F0, "0F934A4D4CF3028D"},
    {"Self-test Q", 0x3CE928, "14973DC716CDE124"}
};

/**
 * @brief Convert 64 PRN chips to hexadecimal string
 */
void chips_to_hex(const int8_t *chips, char *hex_str) {
    for (int byte_idx = 0; byte_idx < 8; byte_idx++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int chip_idx = byte_idx * 8 + bit;
            // Logic 1 → chip -1 → bit 1
            // Logic 0 → chip +1 → bit 0
            if (chips[chip_idx] == -1) {
                byte |= (1 << (7 - bit));
            }
        }
        sprintf(hex_str + byte_idx * 2, "%02X", byte);
    }
}

/**
 * @brief Generate 64 chips using LFSR
 */
void generate_64_chips(uint32_t init_value, int8_t *chips) {
    uint32_t lfsr = init_value;

    for (int i = 0; i < 64; i++) {
        // Output = X0 (LSB)
        // T.018 Table 2.3: Logic 1 → -1, Logic 0 → +1
        chips[i] = (lfsr & 1) ? -1 : +1;

        // LFSR feedback: X0 ⊕ X18
        uint8_t feedback = (lfsr ^ (lfsr >> 18)) & 1;

        // Shift RIGHT, feedback into X22
        lfsr = (lfsr >> 1) | ((uint32_t)feedback << 22);

        // Mask 23 bits
        lfsr &= 0x7FFFFF;
    }
}

/**
 * @brief Display LFSR state in T.018 Table 2.2 format
 */
void print_lfsr_state(uint32_t lfsr) {
    printf("    Register [22..0]: ");
    for (int i = 22; i >= 0; i--) {
        printf("%d", (lfsr >> i) & 1);
        if (i > 0 && i % 4 == 0) printf(" ");
    }
    printf("\n");
}

/**
 * @brief Verify PRN against T.018 Table 2.2 reference
 */
int verify_prn_sequence(const char *name, uint32_t init_value, const char *expected_hex) {
    printf("\n========================================\n");
    printf("Testing: %s\n", name);
    printf("========================================\n");
    printf("  Init value: 0x%06X\n", init_value);
    print_lfsr_state(init_value);

    // Generate 64 chips
    int8_t chips[64];
    generate_64_chips(init_value, chips);

    // Convert to hex
    char hex_str[17];
    chips_to_hex(chips, hex_str);
    hex_str[16] = '\0';

    // Format for display
    printf("  Generated:  ");
    for (int i = 0; i < 16; i++) {
        printf("%c", hex_str[i]);
        if ((i + 1) % 4 == 0 && i < 15) printf(" ");
    }
    printf("\n");

    if (strcmp(expected_hex, "UNKNOWN") != 0) {
        // Remove spaces from expected
        char expected_clean[17];
        int j = 0;
        for (int i = 0; expected_hex[i] && j < 16; i++) {
            if (expected_hex[i] != ' ') {
                expected_clean[j++] = expected_hex[i];
            }
        }
        expected_clean[j] = '\0';

        printf("  Expected:   ");
        for (int i = 0; i < strlen(expected_clean); i++) {
            printf("%c", expected_clean[i]);
            if ((i + 1) % 4 == 0 && i < strlen(expected_clean) - 1) printf(" ");
        }
        printf("\n");

        // Compare
        if (strcmp(hex_str, expected_clean) == 0) {
            printf("  ✓ PASS - Matches T.018 Table 2.2\n");
            return 1;
        } else {
            printf("  ✗ FAIL - Mismatch!\n");

            // Show bit-by-bit comparison
            printf("\n  Chip-by-chip comparison (first 32):\n");
            printf("    Chip: ");
            for (int i = 0; i < 32; i++) printf("%2d ", i);
            printf("\n    Got:  ");
            for (int i = 0; i < 32; i++) printf("%2d ", chips[i]);
            printf("\n");

            return 0;
        }
    } else {
        printf("  (No reference value - showing generated sequence)\n");

        // Display first 32 chips
        printf("\n  First 32 chips:\n    ");
        for (int i = 0; i < 32; i++) {
            printf("%2d ", chips[i]);
            if ((i + 1) % 16 == 0 && i < 31) printf("\n    ");
        }
        printf("\n");

        return 1;
    }
}

/**
 * @brief Extract initialization value from T.018 Table 2.2 binary representation
 */
uint32_t parse_table_init(const char *binary_str) {
    uint32_t value = 0;
    for (int i = 0; i < 23 && binary_str[i]; i++) {
        if (binary_str[i] == '1') {
            value |= (1 << (22 - i));
        }
    }
    return value;
}

int main(void) {
    printf("========================================\n");
    printf("T.018 PRN Verification Tool\n");
    printf("========================================\n");
    printf("Spec: T.018 Issue 1 Rev.12 Table 2.2\n");
    printf("LFSR: X^23 + X^18 + 1\n");
    printf("Feedback: X0 ⊕ X18 → X22\n");
    printf("\n");

    // Verify table values from T.018 Table 2.2
    printf("Verifying initialization values from T.018 Table 2.2:\n");
    printf("\n");
    printf("Normal I:     000 0000 0000 0000 0000 0001 = 0x%06X\n",
           parse_table_init("00000000000000000000001"));
    printf("Normal Q:     001 1010 1100 0001 1111 1100 = 0x%06X\n",
           parse_table_init("00110101100000111111100"));
    printf("Self-test I:  101 0010 1100 1001 1111 0000 = 0x%06X\n",
           parse_table_init("10100101100100111110000"));
    printf("Self-test Q:  011 1100 1110 1001 0010 1000 = 0x%06X\n",
           parse_table_init("01111001110010100101000"));

    // Run verification tests
    int passed = 0;
    int total = 0;

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        total++;
        if (verify_prn_sequence(tests[i].name, tests[i].init_value, tests[i].expected_hex)) {
            passed++;
        }
    }

    // Test against prn_generator.c implementation
    printf("\n========================================\n");
    printf("Testing prn_generator.c Implementation\n");
    printf("========================================\n");

    if (prn_verify_table_2_2()) {
        passed++;
    }
    total++;

    // Summary
    printf("\n========================================\n");
    printf("Verification Summary\n");
    printf("========================================\n");
    printf("  Tests passed: %d/%d\n", passed, total);

    if (passed == total) {
        printf("  ✓ ALL TESTS PASSED\n");
        printf("\n  PRN generator is FULLY COMPLIANT with T.018 Table 2.2\n");
        return 0;
    } else {
        printf("  ✗ SOME TESTS FAILED\n");
        printf("\n  PRN generator needs corrections!\n");
        return 1;
    }
}
