/**
 * @file oqpsk_modulator.h
 * @brief OQPSK Modulator with DSSS spreading
 *
 * Generates I/Q samples for T.018 2nd generation beacons:
 * - OQPSK modulation with Tc/2 offset
 * - DSSS spreading (256 chips/bit)
 * - Sample rate: 2.4576 MHz (64 samples/chip)
 */

#ifndef OQPSK_MODULATOR_H
#define OQPSK_MODULATOR_H

#include <stdint.h>
#include <complex.h>

// T.018 modulation parameters (Section 2.2.3)
#define OQPSK_CHIP_RATE         38400       // 38.4 kchips/s per channel
#define OQPSK_SAMPLE_RATE       2457600     // 2.4576 MHz (64 samples/chip, PlutoSDR compatible)
#define OQPSK_SAMPLES_PER_CHIP  64          // 2.4576M / 38.4k = 64 samples/chip (integer)
#define OQPSK_DATA_RATE         300         // 300 bps total
#define OQPSK_CHIPS_PER_BIT     256         // 256 chips per bit (spreading factor)

// Frame timing (T.018 Section 2.2.3.b: odd bits→I, even bits→Q)
#define OQPSK_PREAMBLE_BITS     50          // Preamble duration
#define OQPSK_MESSAGE_BITS      250         // Message data
#define OQPSK_TOTAL_BITS        300         // Preamble + Message
#define OQPSK_BITS_PER_CHANNEL  150         // 150 bits on I, 150 bits on Q (parallel)
#define OQPSK_TOTAL_SAMPLES     5000000     // 76,800 chips × 64 samp/chip + margin (4.9M + margin)

// OQPSK modulator state
typedef struct {
    uint16_t current_bit;                   // Current bit position
    uint16_t current_chip;                  // Current chip in bit
    float prev_i_chip;                      // Previous I chip (for interpolation)
    float prev_q_chip;                      // Previous Q chip (OQPSK delay)
    uint32_t sample_count;                  // Total samples generated
} oqpsk_state_t;

/**
 * @brief Initialize OQPSK modulator
 * @param state Modulator state
 */
void oqpsk_init(oqpsk_state_t *state);

/**
 * @brief Generate I/Q samples for complete T.018 frame
 * @param frame_bits 252-bit frame (2 header + 250 data)
 * @param iq_samples Output buffer (~1.3M complex samples)
 * @return Number of samples generated
 *
 * T.018 Section 2.2.3.b: Implements odd/even bit separation
 * - 300 bits total (50 preamble + 250 data)
 * - Odd bits (1,3,5,...) → I channel: 150 bits @ 150 bps
 * - Even bits (2,4,6,...) → Q channel: 150 bits @ 150 bps
 * - Each bit spread over 256 chips (38,400 chips total per channel)
 * - Q channel delayed by Tc/2 (OQPSK offset)
 * - Duration: 1000 ms @ 38.4 kchips/s
 */
uint32_t oqpsk_modulate_frame(const uint8_t *frame_bits,
                              float complex *iq_samples);

/**
 * @brief Generate I/Q samples for single data bit
 * @param bit Data bit (0 or 1)
 * @param i_chips I-channel PRN (256 chips)
 * @param q_chips Q-channel PRN (256 chips)
 * @param iq_samples Output buffer (4096 samples)
 * @param state Modulator state
 * @return Number of samples generated
 */
uint32_t oqpsk_modulate_bit(uint8_t bit,
                            const int8_t *i_chips,
                            const int8_t *q_chips,
                            float complex *iq_samples,
                            oqpsk_state_t *state);

/**
 * @brief Verify modulator output (sanity check)
 * @param iq_samples Complex samples
 * @param num_samples Number of samples
 * @return 1 if valid, 0 if errors
 */
uint8_t oqpsk_verify_output(const float complex *iq_samples,
                            uint32_t num_samples);

#endif // OQPSK_MODULATOR_H
