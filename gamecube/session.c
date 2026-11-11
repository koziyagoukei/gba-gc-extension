#include "session.h"
#include "joybus.h"
#include <ogc/lwp_watchdog.h>
#include <stdio.h>

static char last_failure[96] = "none";
const char *session_last_failure(void) { return last_failure; }
#define FAIL(...) do { snprintf(last_failure, sizeof(last_failure), __VA_ARGS__); return false; } while (0)

bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault) {
    request->seq = ++s->seq;
    uint32_t words[WIRE_WORDS];
    const unsigned count = wire_encode(request, words);
    if (!count) FAIL("encode type=%u", request->type);
    for (unsigned attempt = 0; attempt <= 3; ++attempt) {
        if (attempt) ++s->stats.retries;
        ++s->stats.sent;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t word = words[i];
            if (!attempt && fault == BAD_REQUEST_CRC && i == 3) word ^= 1;
            // A failed word transfer leaves an unknown stream boundary. Reconnect.
            if (!joy_write(word)) FAIL("write seq=%u attempt=%u word=%u", request->seq, attempt, i);
        }
        Parser parser = {0};
        bool retry = false;
        const uint64_t deadline = gettime() + millisecs_to_ticks(s->timeout_ms ? s->timeout_ms : 120);
        while (gettime() < deadline) {
            uint32_t word;
            if (!joy_read(&word)) FAIL("read seq=%u attempt=%u parsed=%u", request->seq, attempt, parser.count);
            const enum ParseResult result = wire_feed(&parser, word, reply);
            if (result == BAD_CRC) { ++s->stats.crc_errors; retry = true; break; }
            if (result == BAD_HEADER) { ++s->stats.bad_headers; FAIL("bad header seq=%u attempt=%u", request->seq, attempt); }
            if (result != FRAME_OK) continue;
            ++s->stats.received;
            if (reply->type == NACK && (reply->seq == request->seq || reply->seq == 0)) {
                ++s->stats.nacks;
                retry = true;
                break;
            }
            // A reply to an older request (ACK or NACK) can still be queued after a
            // retransmission; skip it like a duplicate.
            if (reply->seq < request->seq && reply->seq != 0) { ++s->stats.duplicates; continue; }
            if (reply->type != ACK || reply->seq > request->seq)
                FAIL("unexpected type=%u seq=%u for seq=%u attempt=%u", reply->type, reply->seq, request->seq, attempt);
            if (!attempt && fault == DROP_ACK) continue;
            return true;
        }
        if (!retry) ++s->stats.timeouts;
        if (parser.count) FAIL("partial frame seq=%u attempt=%u words=%u", request->seq, attempt, parser.count); // reset before resending
    }
    FAIL("no reply after retries seq=%u", request->seq);
}
