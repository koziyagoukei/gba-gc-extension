#ifndef GC_RAW_LINK_H
#define GC_RAW_LINK_H
#include <stdbool.h>
#include <stdint.h>

// JoyBus RESET followed by the raw 32-bit probe/ack exchange that precedes
// framing. On success words holds 0, 0xffffffff and 0x1234a5c7 as echoed.
bool raw_round_trip(uint32_t words[3]);
#endif
