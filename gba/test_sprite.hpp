#pragma once
#include <cstdint>

// Original pixel-art fixture: a diamond-shaped radio beacon.
inline uint8_t beacon_pixel(unsigned x, unsigned y) {
    const unsigned dx = x < 32 ? 31-x : x-32;
    const unsigned dy = y < 32 ? 31-y : y-32;
    if (dx+dy > 30) return 0;
    if (dx+dy >= 28) return 1;
    if (dx < 4 || dy < 4) return (dx+dy < 15) ? 4 : 3;
    return dx+dy < 17 ? 5 : 2;
}

inline uint8_t sprite_byte(uint32_t offset) {
    if (offset >= 2048) {
        static constexpr uint16_t palette[16] = {
            0, 0x7fff, 0x7c42, 0x7f00, 0x03ff, 0x5eea
        };
        const unsigned at = offset-2048;
        return (uint8_t)(palette[at/2] >> (8*(at%2)));
    }
    const unsigned tile = offset/32, within = offset%32;
    const unsigned x = (tile%8)*8 + (within%4)*2;
    const unsigned y = (tile/8)*8 + within/4;
    return (uint8_t)(beacon_pixel(x,y) | (beacon_pixel(x+1,y)<<4));
}
