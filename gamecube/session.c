#include "session.h"
#include "joybus.h"
#include <ogc/lwp_watchdog.h>

bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault) {
    request->seq = ++s->seq;
    uint32_t words[WIRE_WORDS];
    const unsigned count = wire_encode(request, words);
    if (!count) return false;
    for (unsigned attempt = 0; attempt <= 3; ++attempt) {
        if (attempt) ++s->stats.retries;
        ++s->stats.sent;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t word = words[i];
            if (!attempt && fault == BAD_REQUEST_CRC && i == 3) word ^= 1;
            // A failed word transfer leaves an unknown stream boundary. Reconnect.
            if (!joy_write(word)) return false;
        }
        Parser parser = {0};
        bool retry = false;
        const uint64_t deadline = gettime() + millisecs_to_ticks(120);
        while (gettime() < deadline) {
            uint32_t word;
            if (!joy_read(&word)) return false;
            const enum ParseResult result = wire_feed(&parser, word, reply);
            if (result == BAD_CRC) { ++s->stats.crc_errors; retry = true; break; }
            if (result == BAD_HEADER) { ++s->stats.bad_headers; return false; }
            if (result != FRAME_OK) continue;
            ++s->stats.received;
            if (reply->type == NACK && (reply->seq == request->seq || reply->seq == 0)) {
                ++s->stats.nacks;
                retry = true;
                break;
            }
            if (reply->type != ACK || reply->seq > request->seq) return false;
            if (reply->seq < request->seq) { ++s->stats.duplicates; continue; }
            if (!attempt && fault == DROP_ACK) continue;
            return true;
        }
        if (!retry) ++s->stats.timeouts;
        if (parser.count) return false; // Partial frame. Reset before sending again.
    }
    return false;
}
