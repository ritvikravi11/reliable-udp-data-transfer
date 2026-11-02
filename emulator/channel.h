
#ifndef CHANNEL_H
#define CHANNEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double loss_rate;       
    double duplicate_rate; 
    double corrupt_rate;   
    double reorder_rate;   
    unsigned int min_delay_ms;
    unsigned int max_delay_ms;
    unsigned int jitter_ms;
    uint32_t seed;
} ChannelConfig;

typedef struct {
    uint64_t packets_seen;
    uint64_t packets_dropped;
    uint64_t packets_duplicated;
    uint64_t packets_corrupted;
    uint64_t packets_reordered;
} ChannelStats;

void channel_init(uint32_t seed);

void channel_default_config(ChannelConfig *config);

int channel_validate_config(const ChannelConfig *config);

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
