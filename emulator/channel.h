
#ifndef CHANNEL_H
#define CHANNEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double loss_rate;       /* 0.0 to 1.0 */
    double duplicate_rate;  /* 0.0 to 1.0 */
    double corrupt_rate;    /* 0.0 to 1.0 */
    double reorder_rate;    /* 0.0 to 1.0 */
    unsigned int min_delay_ms;
    unsigned int max_delay_ms;
    uint32_t seed;
} ChannelConfig;

typedef struct {
    uint64_t packets_seen;
    uint64_t packets_dropped;
    uint64_t packets_duplicated;
    uint64_t packets_corrupted;
    uint64_t packets_reordered;
} ChannelStats;

/* Initialize the pseudorandom generator with a reproducible seed. */
void channel_init(uint32_t seed);

/* Set safe default values: no impairments and no delay. */
void channel_default_config(ChannelConfig *config);

/* Validate the configuration. Returns 1 if valid, otherwise 0. */
int channel_validate_config(const ChannelConfig *config);

/*
 * Decide how one datagram should be handled.
 * Returns 0 for drop, 1 for one copy, or 2 for two copies.
 * The output buffer contains the forwarded datagram bytes.
 * delay_ms receives the selected delay for this datagram.
 */
int channel_process(
    const ChannelConfig *config,
    ChannelStats *stats,
    const uint8_t *input,
    size_t length,
    uint8_t *output,
    size_t output_capacity,
    unsigned int *delay_ms
);

#endif
