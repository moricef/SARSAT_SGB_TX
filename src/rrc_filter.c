/**
 * @file rrc_filter.c
 * @brief Root-Raised Cosine (RRC) Filter Implementation
 *
 * Software FIR filter for pulse shaping in OQPSK modulation.
 * Reduces spectral sidelobes while maintaining signal integrity.
 */

#include "rrc_filter.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Pre-calculated RRC coefficients (calculated on first use)
static float rrc_coeffs[RRC_NUM_TAPS];
static uint8_t coeffs_initialized = 0;

/**
 * @brief Calculate RRC filter coefficients
 *
 * RRC formula:
 * h(t) = (sin(π*t*(1-α)) + 4*α*t*cos(π*t*(1+α))) / (π*t*(1-(4*α*t)²))
 *
 * Where:
 * - t = time normalized by chip period (t/T)
 * - α = roll-off factor
 *
 * Special cases:
 * - t = 0: h(0) = 1 + α*(4/π - 1)
 * - t = ±1/(4*α): h(t) = (α/√2)*[(1+2/π)*sin(π/(4*α)) + (1-2/π)*cos(π/(4*α))]
 */
static void calculate_rrc_coefficients(void) {
    const float alpha = RRC_ROLLOFF;
    const int center = RRC_CENTER_TAP;

    printf("Calculating RRC filter coefficients...\n");
    printf("  Taps: %d\n", RRC_NUM_TAPS);
    printf("  Roll-off: %.2f\n", alpha);
    printf("  Samples/chip: %d\n", RRC_SAMPLES_PER_CHIP);

    float sum = 0.0f;

    for (int i = 0; i < RRC_NUM_TAPS; i++) {
        int n = i - center;  // Sample index relative to center
        float t = (float)n / RRC_SAMPLES_PER_CHIP;  // Time in chip periods

        if (n == 0) {
            // Special case: t = 0
            rrc_coeffs[i] = 1.0f + alpha * (4.0f / M_PI - 1.0f);
        }
        else if (fabsf(fabsf(4.0f * alpha * t) - 1.0f) < 1e-6f) {
            // Special case: t = ±1/(4*α)
            float factor = alpha / sqrtf(2.0f);
            float arg = M_PI / (4.0f * alpha);
            rrc_coeffs[i] = factor * ((1.0f + 2.0f / M_PI) * sinf(arg) +
                                      (1.0f - 2.0f / M_PI) * cosf(arg));
        }
        else {
            // General case
            float pi_t = M_PI * t;
            float numerator = sinf(pi_t * (1.0f - alpha)) +
                             4.0f * alpha * t * cosf(pi_t * (1.0f + alpha));
            float denominator = pi_t * (1.0f - powf(4.0f * alpha * t, 2.0f));
            rrc_coeffs[i] = numerator / denominator;
        }

        sum += rrc_coeffs[i];
    }

    // Normalize coefficients to unity gain
    for (int i = 0; i < RRC_NUM_TAPS; i++) {
        rrc_coeffs[i] /= sum;
    }

    printf("  ✓ Coefficients calculated and normalized\n");
    printf("  Center tap value: %.6f\n", rrc_coeffs[center]);

    coeffs_initialized = 1;
}

void rrc_init(rrc_state_t *state) {
    memset(state, 0, sizeof(rrc_state_t));
    state->write_idx = 0;

    // Calculate coefficients if not already done
    if (!coeffs_initialized) {
        calculate_rrc_coefficients();
    }

    printf("✓ RRC filter initialized\n");
}

void rrc_filter(rrc_state_t *state,
                const float complex *input,
                float complex *output,
                uint32_t num_samples) {

    // Ensure coefficients are initialized
    if (!coeffs_initialized) {
        calculate_rrc_coefficients();
    }

    for (uint32_t n = 0; n < num_samples; n++) {
        // Extract I and Q
        float i_in = crealf(input[n]);
        float q_in = cimagf(input[n]);

        // Shift in new samples (circular buffer)
        state->i_history[state->write_idx] = i_in;
        state->q_history[state->write_idx] = q_in;

        // Increment write index (circular)
        uint32_t next_idx = (state->write_idx + 1) % RRC_NUM_TAPS;

        // Apply FIR filter (convolution)
        float i_out = 0.0f;
        float q_out = 0.0f;

        // Read samples starting from oldest (next_idx)
        uint32_t read_idx = next_idx;
        for (int tap = 0; tap < RRC_NUM_TAPS; tap++) {
            i_out += state->i_history[read_idx] * rrc_coeffs[tap];
            q_out += state->q_history[read_idx] * rrc_coeffs[tap];

            read_idx = (read_idx + 1) % RRC_NUM_TAPS;
        }

        // Output filtered sample
        output[n] = i_out + I * q_out;

        // Update write index for next iteration
        state->write_idx = next_idx;
    }
}

void rrc_get_coefficients(float *coeffs, uint32_t num_taps) {
    if (!coeffs_initialized) {
        calculate_rrc_coefficients();
    }

    uint32_t n = (num_taps < RRC_NUM_TAPS) ? num_taps : RRC_NUM_TAPS;
    memcpy(coeffs, rrc_coeffs, n * sizeof(float));
}
