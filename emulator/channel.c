
#include "channel.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

static double random_unit(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

void channel_init(uint32_t seed)
{
    srand((unsigned int)seed);
}

void channel_default_config(ChannelConfig *config)
{
    if (config == NULL) {
        return;
    }

    config->loss_rate = 0.0;
    config->duplicate_rate = 0.0;
    config->corrupt_rate = 0.0;
    config->reorder_rate = 0.0;
    config->min_delay_ms = 0;
    config->max_delay_ms = 0;
    config->jitter_ms = 0;
    config->seed = 1;
}

int channel_validate_config(const ChannelConfig *config)
{
    if (config == NULL) {
        return 0;
    }

    if (config->loss_rate < 0.0 || config->loss_rate > 1.0 ||
        config->duplicate_rate < 0.0 || config->duplicate_rate > 1.0 ||
        config->corrupt_rate < 0.0 || config->corrupt_rate > 1.0 ||
        config->reorder_rate < 0.0 || config->reorder_rate > 1.0) {
        return 0;
    }

    if (config->min_delay_ms > config->max_delay_ms) {
        return 0;
    }

    return 1;
}

int channel_process(
    const ChannelConfig *config,
    ChannelStats *stats,
    const uint8_t *input,
    size_t length,
    uint8_t *output,
    size_t output_capacity,
    unsigned int *delay_ms)
{
    unsigned int span;

    if (!channel_validate_config(config) ||
        stats == NULL ||
        input == NULL ||
        output == NULL ||
        delay_ms == NULL ||
        length == 0 ||
        length > output_capacity) {
        return -1;
    }

    stats->packets_seen++;
    *delay_ms = config->min_delay_ms;

    if (random_unit() < config->loss_rate) {
        stats->packets_dropped++;
        return 0;
    }

    memcpy(output, input, length);

    if (random_unit() < config->corrupt_rate) {
        size_t position = (size_t)(rand() % (int)length);
        unsigned int bit = (unsigned int)(rand() % 8);

        output[position] ^= (uint8_t)(1U << bit);
        stats->packets_corrupted++;
    }

    if (config->max_delay_ms > config->min_delay_ms) {
        span = config->max_delay_ms - config->min_delay_ms;
        *delay_ms = config->min_delay_ms +
            (unsigned int)(rand() % (span + 1U));
    }

    if (config->jitter_ms > 0) {
        unsigned int jitter = (unsigned int)(rand() %
            (config->jitter_ms + 1U));
        if (*delay_ms <= UINT_MAX - jitter) {
            *delay_ms += jitter;
        } else {
            *delay_ms = UINT_MAX;
        }
    }

    /*
     * The persistent UDP emulator uses this extra delay with its scheduled
     * forwarding queue to create actual out-of-order delivery.
     */
    if (random_unit() < config->reorder_rate) {
        if (*delay_ms <= UINT_MAX - 100U) {
            *delay_ms += 100U;
        }
        stats->packets_reordered++;
    }

    if (random_unit() < config->duplicate_rate) {
        stats->packets_duplicated++;
        return 2;
    }

    return 1;
}
