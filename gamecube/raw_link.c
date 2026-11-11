#include "raw_link.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include "joybus.h"
#include "round_trip.h"

static bool read_marker(uint32_t marker) {
    const uint64_t deadline = gettime() + millisecs_to_ticks(350);
    do {
        uint32_t word;
        if (!joy_read(&word)) return false;
        if (word == marker) return true;
    } while (gettime() < deadline);
    return false;
}

bool raw_round_trip(uint32_t words[3]) {
    if (!joy_reset() || !joy_write(RAW_PROBE) || !read_marker(RAW_VALUES)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!joy_read(&words[i])) return false;
    if (words[0] != 0 || words[1] != UINT32_MAX || words[2] != RAW_PATTERN) return false;
    return joy_write(RAW_ACK) && read_marker(RAW_CONFIRM);
}
