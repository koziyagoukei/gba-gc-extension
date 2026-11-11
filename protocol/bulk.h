#ifndef POC_BULK_H
#define POC_BULK_H

#include <stdint.h>

static inline uint8_t bulk_pattern(uint32_t offset) {
    return (uint8_t)((offset*29u) ^ (offset >> 8) ^ 0xa5u);
}

#endif
