#include "session.h"
#include "joybus.h"
#include <ogc/lwp_watchdog.h>
#include <stdio.h>

static char last_failure[96] = "none";
const char *session_last_failure(void) { return last_failure; }
#define FAIL(...) do { snprintf(last_failure, sizeof(last_failure), __VA_ARGS__); s->last.done = gettime(); return false; } while (0)

// One request: up to SESSION_MAX_ATTEMPTS transmissions of the same frame
// (same seq). A transmission ends early on a NACK, a reply that fails its
// CRC or header with nothing else buffered, or a failed word write (the
// GBA re-checks or expires the partial frame, so the resend is safe). SI
// trouble while reading only costs that word. The session is given up only
// when every attempt failed or the peer answers out of protocol.
bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault) {
    request->seq = ++s->seq;
    s->last = (RequestTrace){.seq = request->seq, .type = request->type, .start = gettime()};
    uint32_t words[WIRE_WORDS];
    const unsigned count = wire_encode(request, words);
    if (!count) FAIL("encode type=%u", request->type);
    for (unsigned attempt = 0; attempt < SESSION_MAX_ATTEMPTS; ++attempt) {
        if (attempt) ++s->stats.retries;
        ++s->stats.sent;
        s->last.attempts = (uint8_t)(attempt + 1);
        enum AttemptEnd end = ATTEMPT_TIMEOUT;
        const uint64_t sent_at = gettime();
        bool written = true;
        for (unsigned i = 0; i < count && written; ++i) {
            uint32_t word = words[i];
            if (!attempt && fault == BAD_REQUEST_CRC && i == 3) word ^= 1;
            if (!joy_write(word)) written = false;
        }
        if (!written) {
            ++s->stats.write_errors;
            s->last.end[attempt] = ATTEMPT_WRITE;
            continue;
        }
        Parser parser = {0};
        const uint64_t deadline = gettime() + millisecs_to_ticks(s->timeout_ms ? s->timeout_ms : 120);
        while (gettime() < deadline) {
            uint32_t word;
            const JoyResult read = joy_read_word(&word);
            if (read == JOY_NOT_READY) continue;
            if (read == JOY_ERROR) { ++s->stats.read_errors; continue; }  // this word may be lost
            const unsigned resyncs = parser.resyncs;
            const enum ParseResult result = wire_feed(&parser, word, reply);
            s->stats.resyncs += parser.resyncs - resyncs;
            if (result == BAD_CRC || result == BAD_HEADER) {
                if (result == BAD_CRC) ++s->stats.crc_errors; else ++s->stats.bad_headers;
                if (parser.count) continue;  // a later frame start is already buffered
                end = result == BAD_CRC ? ATTEMPT_CRC : ATTEMPT_HEADER;
                break;
            }
            if (result != FRAME_OK) continue;
            ++s->stats.received;
            if (reply->type == NACK && (reply->seq == request->seq || reply->seq == 0)) {
                ++s->stats.nacks;
                end = ATTEMPT_NACK;
                break;
            }
            // A reply to an older request (ACK or NACK) can still be queued after a
            // retransmission; skip it like a duplicate. An ACK always carries the
            // sequence of the request it answers, so seq 0 is never ours.
            if (reply->seq < request->seq) { ++s->stats.duplicates; continue; }
            if (reply->type != ACK || reply->seq > request->seq)
                FAIL("unexpected type=%u seq=%u for seq=%u attempt=%u", reply->type, reply->seq, request->seq, attempt);
            if (!attempt && fault == DROP_ACK) continue;
            s->last.end[attempt] = ATTEMPT_OK;
            s->last.wait_us[attempt] = (uint32_t)ticks_to_microsecs(gettime() - sent_at);
            s->last.done = gettime();
            return true;
        }
        if (end == ATTEMPT_TIMEOUT) ++s->stats.timeouts;
        s->last.end[attempt] = (uint8_t)end;
        s->last.wait_us[attempt] = (uint32_t)ticks_to_microsecs(gettime() - sent_at);
    }
    FAIL("no reply after %u attempts seq=%u", SESSION_MAX_ATTEMPTS, request->seq);
}
