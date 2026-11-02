#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <stdint.h>

#define SEQUENCE_SPACE_SIZE ((uint64_t)UINT32_MAX + 1ULL)

static inline uint32_t sequence_distance(uint32_t base, uint32_t value)
{
    return value - base;
}

static inline int sequence_before(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) < 0;
}

static inline int sequence_in_window(uint32_t base, uint32_t value,
                                     uint32_t window_size)
{
    return sequence_distance(base, value) < window_size;
}

#endif
