/**
 * @file pluto_control.c
 * @brief PlutoSDR Control via libiio
 *
 * Handles PlutoSDR configuration and I/Q transmission:
 * - Device initialization
 * - TX channel configuration
 * - I/Q buffer transmission
 * - Cleanup
 *
 * Based on SARSAT_FGB implementation, adapted for 2G (OQPSK)
 */

#include "pluto_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static struct iio_channel* get_channel(struct iio_device *dev, const char *id, int output) {
    struct iio_channel *chn = iio_device_find_channel(dev, id, output);
    if (!chn) {
        fprintf(stderr, "Channel %s not found\n", id);
    }
    return chn;
}

static int set_channel_attr_longlong(struct iio_channel *chn, const char *attr, long long val) {
    int ret = iio_channel_attr_write_longlong(chn, attr, val);
    if (ret < 0) {
        fprintf(stderr, "Failed to set %s: %s\n", attr, strerror(-ret));
    }
    return ret;
}

/* Unused - reserved for future use
static int set_device_attr_longlong(struct iio_device *dev, const char *attr, long long val) {
    int ret = iio_device_attr_write_longlong(dev, attr, val);
    if (ret < 0) {
        fprintf(stderr, "Failed to set device %s: %s\n", attr, strerror(-ret));
    }
    return ret;
}
*/

// =============================================================================
// INITIALIZATION & CONFIGURATION
// =============================================================================

int pluto_init(pluto_ctx_t *ctx, const char *uri) {
    if (!ctx) {
        fprintf(stderr, "Invalid context pointer\n");
        return -1;
    }

    memset(ctx, 0, sizeof(pluto_ctx_t));

    // Create IIO context
    if (uri) {
        ctx->ctx = iio_create_context_from_uri(uri);
        printf("Connecting to PlutoSDR at %s...\n", uri);
    } else {
        ctx->ctx = iio_create_default_context();
        printf("Connecting to PlutoSDR (auto-detect)...\n");
    }

    if (!ctx->ctx) {
        fprintf(stderr, "Failed to create IIO context\n");
        return -1;
    }

    // Get TX device (cf-ad9361-dds-core-lpc for TX data)
    ctx->tx_dev = iio_context_find_device(ctx->ctx, "cf-ad9361-dds-core-lpc");
    if (!ctx->tx_dev) {
        fprintf(stderr, "TX device (cf-ad9361-dds-core-lpc) not found\n");
        iio_context_destroy(ctx->ctx);
        ctx->ctx = NULL;
        return -1;
    }

    // Get TX I/Q channels (voltage0 = I, voltage1 = Q)
    ctx->tx_i = get_channel(ctx->tx_dev, "voltage0", 1);
    ctx->tx_q = get_channel(ctx->tx_dev, "voltage1", 1);

    if (!ctx->tx_i || !ctx->tx_q) {
        fprintf(stderr, "Failed to get TX I/Q channels\n");
        iio_context_destroy(ctx->ctx);
        ctx->ctx = NULL;
        return -1;
    }

    // Enable TX channels
    iio_channel_enable(ctx->tx_i);
    iio_channel_enable(ctx->tx_q);

    ctx->initialized = 1;
    printf("✓ PlutoSDR initialized successfully\n");

    return 0;
}

int pluto_configure_tx(pluto_ctx_t *ctx,
                      uint64_t frequency,
                      int32_t gain_db,
                      uint32_t sample_rate) {
    if (!ctx || !ctx->ctx || !ctx->initialized) {
        fprintf(stderr, "PlutoSDR not initialized\n");
        return -1;
    }

    // Get physical device (ad9361-phy) for configuration
    struct iio_device *phy = iio_context_find_device(ctx->ctx, "ad9361-phy");
    if (!phy) {
        fprintf(stderr, "ad9361-phy device not found\n");
        return -1;
    }

    // Get TX LO (local oscillator) channel
    struct iio_channel *tx_lo = get_channel(phy, "altvoltage1", 1);
    if (!tx_lo) {
        fprintf(stderr, "TX LO channel not found\n");
        return -1;
    }

    // Set TX frequency
    if (set_channel_attr_longlong(tx_lo, "frequency", frequency) < 0) {
        return -1;
    }
    ctx->frequency = frequency;

    // Get TX channel for gain/sample rate configuration
    struct iio_channel *tx_chan = get_channel(phy, "voltage0", 1);
    if (!tx_chan) {
        fprintf(stderr, "TX channel not found in phy\n");
        return -1;
    }

    // Set sample rate
    if (set_channel_attr_longlong(tx_chan, "sampling_frequency", sample_rate) < 0) {
        return -1;
    }

    // Set TX hardware gain (PlutoSDR uses attenuation: negative dB)
    // Rev.B (AD9363): integer millidB via longlong
    // Rev.C (AD9364): double dB via iio_channel_attr_write_double
    // Range: -89.75 dB to 0 dB
    double hw_gain = (double)gain_db;
    if (hw_gain > 0.0) hw_gain = 0.0;
    if (hw_gain < -89.75) hw_gain = -89.75;

    int ret = iio_channel_attr_write_double(tx_chan, "hardwaregain", hw_gain);
    if (ret < 0) {
        // Fallback: try longlong millidB (Rev.B)
        int32_t hw_gain_mdb = (int32_t)(hw_gain * 1000.0);
        ret = iio_channel_attr_write_longlong(tx_chan, "hardwaregain", hw_gain_mdb);
        if (ret < 0) {
            fprintf(stderr, "Failed to set hardwaregain: %s\n", strerror(-ret));
            return -1;
        }
    }
    ctx->gain_db = gain_db;

    // Set RF bandwidth (typically 1.5x to 2x sample rate)
    uint32_t rf_bandwidth = sample_rate * 2;
    set_channel_attr_longlong(tx_chan, "rf_bandwidth", rf_bandwidth);

    printf("✓ PlutoSDR TX configured:\n");
    printf("  Frequency: %llu Hz (%.3f MHz)\n",
           (unsigned long long)frequency, frequency / 1e6);
    printf("  Sample rate: %u Hz (%.1f kHz)\n",
           sample_rate, sample_rate / 1e3);
    printf("  TX gain: %d dB\n", gain_db);
    printf("  RF bandwidth: %u Hz (%.1f kHz)\n",
           rf_bandwidth, rf_bandwidth / 1e3);

    return 0;
}

// =============================================================================
// TRANSMISSION FUNCTIONS
// =============================================================================

int pluto_transmit_iq(pluto_ctx_t *ctx,
                     const float complex *iq_samples,
                     uint32_t num_samples) {
    if (!ctx || !ctx->tx_dev || !iq_samples || num_samples == 0) {
        fprintf(stderr, "Invalid parameters for transmission\n");
        return -1;
    }

    /* Use a fixed-size kernel buffer matching Pluto's DMA buffer.
     * The Pluto IIO driver uses 256 kiB buffers by default.
     * At 2 bytes/sample/channel × 2 channels = 4 bytes/sample,
     * 256 kiB / 4 = 65536 samples. */
    const uint32_t BUF_SAMPLES = 65536;
    uint32_t total_sent = 0;

    printf("Transmitting %u samples (buffer %u samples)...\n",
           num_samples, BUF_SAMPLES);

    /* Create ONE buffer, reuse for all chunks. */
    ctx->tx_buf = iio_device_create_buffer(ctx->tx_dev, BUF_SAMPLES, 0);
    if (!ctx->tx_buf) {
        fprintf(stderr, "Failed to create TX buffer\n");
        return -1;
    }

    while (total_sent < num_samples) {
        uint32_t chunk = num_samples - total_sent;
        if (chunk > BUF_SAMPLES) chunk = BUF_SAMPLES;

        /* Fill buffer. */
        int16_t *buf = (int16_t *)iio_buffer_start(ctx->tx_buf);
        if (!buf) {
            fprintf(stderr, "Failed to get buffer pointer at sample %u\n", total_sent);
            iio_buffer_destroy(ctx->tx_buf);
            ctx->tx_buf = NULL;
            return -1;
        }

        for (uint32_t i = 0; i < chunk; i++) {
            float i_val = crealf(iq_samples[total_sent + i]);
            float q_val = cimagf(iq_samples[total_sent + i]);

            int16_t i_sample = (int16_t)(i_val * 2047.0f);
            int16_t q_sample = (int16_t)(q_val * 2047.0f);

            if (i_sample > 2047) i_sample = 2047;
            if (i_sample < -2048) i_sample = -2048;
            if (q_sample > 2047) q_sample = 2047;
            if (q_sample < -2048) q_sample = -2048;

            buf[2*i]     = i_sample;
            buf[2*i + 1] = q_sample;
        }

        /* Pad with zeros to fill the buffer. */
        for (uint32_t i = chunk; i < BUF_SAMPLES; i++) {
            buf[2*i]     = 0;
            buf[2*i + 1] = 0;
        }

        /* Push — blocks until DMA has consumed the buffer. */
        ssize_t nbytes_tx = iio_buffer_push(ctx->tx_buf);
        if (nbytes_tx < 0) {
            fprintf(stderr, "TX buffer push failed at sample %u: %s\n",
                    total_sent, strerror(-nbytes_tx));
            iio_buffer_destroy(ctx->tx_buf);
            ctx->tx_buf = NULL;
            return -1;
        }

        total_sent += chunk;

        if (total_sent % 500000 < BUF_SAMPLES) {
            printf("  Transmitted %u/%u samples (%.1f%%)\n",
                   total_sent, num_samples,
                   (total_sent * 100.0f) / num_samples);
        }
    }

    iio_buffer_destroy(ctx->tx_buf);
    ctx->tx_buf = NULL;

    printf("✓ Transmitted %u I/Q samples total\n", total_sent);
    return total_sent;
}

// =============================================================================
// TX ENABLE/DISABLE
// =============================================================================

int pluto_enable_tx(pluto_ctx_t *ctx, uint8_t enable) {
    if (!ctx || !ctx->ctx || !ctx->initialized) {
        fprintf(stderr, "PlutoSDR not initialized\n");
        return -1;
    }

    // Get physical device
    struct iio_device *phy = iio_context_find_device(ctx->ctx, "ad9361-phy");
    if (!phy) {
        fprintf(stderr, "ad9361-phy device not found\n");
        return -1;
    }

    // Get TX channel
    struct iio_channel *tx_chan = get_channel(phy, "voltage0", 1);
    if (!tx_chan) {
        fprintf(stderr, "TX channel not found\n");
        return -1;
    }

    // Enable/disable TX power
    const char *powerdown_attr = "powerdown";
    int ret = iio_channel_attr_write_bool(tx_chan, powerdown_attr, !enable);
    if (ret < 0) {
        fprintf(stderr, "Failed to %s TX: %s\n",
                enable ? "enable" : "disable", strerror(-ret));
        return -1;
    }

    printf("TX %s\n", enable ? "enabled" : "disabled");
    return 0;
}

// =============================================================================
// CLEANUP
// =============================================================================

void pluto_cleanup(pluto_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->tx_buf) {
        iio_buffer_destroy(ctx->tx_buf);
        ctx->tx_buf = NULL;
    }

    if (ctx->ctx) {
        iio_context_destroy(ctx->ctx);
        ctx->ctx = NULL;
    }

    ctx->initialized = 0;
    printf("PlutoSDR cleaned up\n");
}

// =============================================================================
// INFO & UTILITY FUNCTIONS
// =============================================================================

void pluto_print_info(const pluto_ctx_t *ctx) {
    if (!ctx || !ctx->ctx) {
        printf("PlutoSDR: Not connected\n");
        return;
    }

    printf("\nPlutoSDR Information:\n");
    printf("  Context: %s\n", iio_context_get_name(ctx->ctx));
    printf("  Description: %s\n", iio_context_get_description(ctx->ctx));

    // Get device attributes
    struct iio_device *phy = iio_context_find_device(ctx->ctx, "ad9361-phy");
    if (phy) {
        char fw_version[256];
        ssize_t ret = iio_device_attr_read(phy, "ensm_mode", fw_version, sizeof(fw_version));
        if (ret > 0) {
            printf("  ENSM Mode: %s\n", fw_version);
        }
    }

    if (ctx->initialized) {
        printf("  TX Frequency: %llu Hz (%.3f MHz)\n",
               (unsigned long long)ctx->frequency, ctx->frequency / 1e6);
        printf("  TX Gain: %d dB\n", ctx->gain_db);
    }

    printf("\n");
}

// =============================================================================
// QUERY FUNCTIONS
// =============================================================================

uint8_t pluto_is_connected(const pluto_ctx_t *ctx) {
    return (ctx && ctx->ctx && ctx->initialized);
}

uint64_t pluto_get_tx_frequency(const pluto_ctx_t *ctx) {
    if (!ctx || !ctx->ctx) return 0;

    struct iio_device *phy = iio_context_find_device(ctx->ctx, "ad9361-phy");
    if (!phy) return 0;

    struct iio_channel *tx_lo = get_channel(phy, "altvoltage1", 1);
    if (!tx_lo) return 0;

    long long freq = 0;
    iio_channel_attr_read_longlong(tx_lo, "frequency", &freq);
    return (uint64_t)freq;
}

uint32_t pluto_get_sample_rate(const pluto_ctx_t *ctx) {
    if (!ctx || !ctx->ctx) return 0;

    struct iio_device *phy = iio_context_find_device(ctx->ctx, "ad9361-phy");
    if (!phy) return 0;

    struct iio_channel *tx_chan = get_channel(phy, "voltage0", 1);
    if (!tx_chan) return 0;

    long long rate = 0;
    iio_channel_attr_read_longlong(tx_chan, "sampling_frequency", &rate);
    return (uint32_t)rate;
}

// =============================================================================
// FILE I/O FUNCTIONS
// =============================================================================

/**
 * @brief Create SigMF metadata file
 * @param base_filename Base filename (without extension)
 * @param num_samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @return 0 on success, -1 on error
 */
static int create_sigmf_meta(const char *base_filename, uint32_t num_samples, uint32_t sample_rate) {
    char meta_filename[512];
    snprintf(meta_filename, sizeof(meta_filename), "%s.sigmf-meta", base_filename);

    FILE *fp = fopen(meta_filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create metadata file '%s': %s\n",
                meta_filename, strerror(errno));
        return -1;
    }

    // Get current time in ISO 8601 format
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char datetime[64];
    strftime(datetime, sizeof(datetime), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    // Write SigMF metadata in JSON format
    fprintf(fp, "{\n");
    fprintf(fp, "    \"global\": {\n");
    fprintf(fp, "        \"core:datatype\": \"cf32_le\",\n");
    fprintf(fp, "        \"core:sample_rate\": %u,\n", sample_rate);
    fprintf(fp, "        \"core:version\": \"1.0.0\",\n");
    fprintf(fp, "        \"core:description\": \"COSPAS-SARSAT T.018 2nd generation beacon test frame with OQPSK modulation, DSSS spreading (256 chips/bit), half-sine pulse shaping, SPS=%u\",\n", sample_rate / 38400);
    fprintf(fp, "        \"core:author\": \"SARSAT_SGB Generator\",\n");
    fprintf(fp, "        \"core:hw\": \"Software generated (baseband)\"\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    \"captures\": [\n");
    fprintf(fp, "        {\n");
    fprintf(fp, "            \"core:sample_start\": 0,\n");
    fprintf(fp, "            \"core:frequency\": 0,\n");
    fprintf(fp, "            \"core:datetime\": \"%s\"\n", datetime);
    fprintf(fp, "        }\n");
    fprintf(fp, "    ],\n");
    fprintf(fp, "    \"annotations\": [\n");
    fprintf(fp, "        {\n");
    fprintf(fp, "            \"core:sample_start\": 0,\n");
    fprintf(fp, "            \"core:sample_count\": %u,\n", num_samples);
    fprintf(fp, "            \"core:comment\": \"Complete T.018 frame: 50-bit preamble + 250-bit message (300 bits total), 38400 chips/channel, %.3f second duration\"\n", (float)num_samples / sample_rate);
    fprintf(fp, "        }\n");
    fprintf(fp, "    ]\n");
    fprintf(fp, "}\n");

    fclose(fp);
    return 0;
}

int pluto_save_iq_file(const char *filename,
                       const float complex *iq_samples,
                       uint32_t num_samples,
                       uint32_t sample_rate) {
    if (!filename || !iq_samples || num_samples == 0) {
        fprintf(stderr, "Invalid parameters for file save\n");
        return -1;
    }

    // Extract base filename and create .sigmf-data filename
    char base_filename[512];
    char data_filename[512];

    // Remove extension if present
    const char *dot = strrchr(filename, '.');
    if (dot && (strcmp(dot, ".iq") == 0 || strcmp(dot, ".sigmf-data") == 0)) {
        size_t len = dot - filename;
        strncpy(base_filename, filename, len);
        base_filename[len] = '\0';
    } else {
        strncpy(base_filename, filename, sizeof(base_filename) - 1);
        base_filename[sizeof(base_filename) - 1] = '\0';
    }

    snprintf(data_filename, sizeof(data_filename), "%s.sigmf-data", base_filename);

    FILE *fp = fopen(data_filename, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open output file '%s': %s\n",
                filename, strerror(errno));
        return -1;
    }

    // Write interleaved float I/Q samples (GNU Radio/inspectrum compatible)
    // Format: [I0, Q0, I1, Q1, I2, Q2, ...]
    for (uint32_t i = 0; i < num_samples; i++) {
        float i_val = crealf(iq_samples[i]);
        float q_val = cimagf(iq_samples[i]);

        if (fwrite(&i_val, sizeof(float), 1, fp) != 1 ||
            fwrite(&q_val, sizeof(float), 1, fp) != 1) {
            fprintf(stderr, "Failed to write sample %u: %s\n", i, strerror(errno));
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    // Create SigMF metadata file
    if (create_sigmf_meta(base_filename, num_samples, sample_rate) < 0) {
        fprintf(stderr, "Warning: Failed to create SigMF metadata file\n");
    }

    // Calculate file size
    size_t file_size = num_samples * 2 * sizeof(float);
    printf("✓ Saved %u I/Q samples to '%s' (%.2f KB)\n",
           num_samples, data_filename, file_size / 1024.0);
    printf("  Format: SigMF (cf32_le - 32-bit float interleaved I/Q)\n");
    printf("  Sample rate: %.1f kHz\n", sample_rate / 1000.0);
    printf("  Duration: %.3f ms\n", (num_samples / (float)sample_rate) * 1000.0);
    printf("  Metadata: %s.sigmf-meta\n", base_filename);

    return 0;
}
