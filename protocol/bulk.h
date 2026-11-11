#ifndef POC_BULK_H
#define POC_BULK_H

#include <stdint.h>

#define SPRITE_BYTES (2048u + 32u)
enum { BULK_PATTERN, BULK_SPRITE };

static inline uint8_t bulk_pattern(uint32_t offset) {
    return (uint8_t)((offset*29u) ^ (offset >> 8) ^ 0xa5u);
}

#endif
