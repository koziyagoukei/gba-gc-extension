#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include "bulk.h"
#include "joybus.h"

static bool transfer_test(Session *session, unsigned gap, uint32_t size, unsigned kind, uint8_t *asset) {
    const LinkStats start = session->stats;
    const uint32_t si_start = joy_errors();
    const uint64_t t0 = gettime();
    Frame request = {.type=BULK_BEGIN,.len=8}, reply;
    wire_put32(request.payload, size);
    wire_put32(request.payload+4, kind);
    if (!session_request(session, &request, &reply, NO_FAULT) || reply.len != 0) return false;
    uint32_t crc = UINT32_MAX;
    for (uint32_t off = 0; off < size;) {
        const uint32_t length = size-off > WIRE_PAYLOAD ? WIRE_PAYLOAD : size-off;
        request.type = BULK_DATA;
        request.len = 8;
        wire_put32(request.payload, off);
        wire_put32(request.payload+4, length);
        if (!session_request(session, &request, &reply, NO_FAULT) || reply.len != length) return false;
        for (uint32_t i = 0; i < length; ++i)
            if (asset) asset[off+i] = reply.payload[i];
            else if (reply.payload[i] != bulk_pattern(off+i)) {
                SYS_Report("POC3 BAD_PATTERN gap=%u size=%u offset=%u\n", gap, size, off+i);
                return false;
            }
        crc = wire_crc(crc, reply.payload, length);
        off += length;
    }
    request.type = BULK_END;
    request.len = 0;
    if (!session_request(session, &request, &reply, NO_FAULT) || reply.len != 4 ||
        wire_get32(reply.payload) != ~crc) return false;
    const uint32_t us = (uint32_t)ticks_to_microsecs(gettime()-t0);
    const uint32_t rate = (uint32_t)((uint64_t)size * 1000000000 / (1024ull * us));
    const LinkStats s = session->stats;
    SYS_Report("POC3 PASS gap=%u bytes=%u us=%u KiBps=%u.%03u crc=%08x "
               "sent=%u recv=%u retries=%u crc_host=%u nack=%u timeout=%u si=%u loss=0\n",
               gap, size, us, rate/1000, rate%1000, ~crc, s.sent-start.sent,
               s.received-start.received, s.retries-start.retries, s.crc_errors-start.crc_errors,
               s.nacks-start.nacks, s.timeouts-start.timeouts, joy_errors()-si_start);
    printf("Bulk %u bytes OK: %u.%03u KiB/s\n", size, rate/1000, rate%1000);
    return true;
}

bool bulk_test(Session *session, unsigned gap) {
    const uint32_t sizes[] = {1024,4096,16384,65536};
    for (unsigned i=0; i<sizeof(sizes)/sizeof(*sizes); ++i)
        if (!transfer_test(session, gap, sizes[i], BULK_PATTERN, NULL)) {
            const LinkStats st = session->stats;
            SYS_Report("POC3 FAIL gap=%u bytes=%u seq=%u sent=%u recv=%u header=%u crc=%u nack=%u retries=%u timeout=%u si=%u\n",
                       gap, sizes[i], session->seq, st.sent, st.received, st.bad_headers,
                       st.crc_errors, st.nacks, st.retries, st.timeouts, joy_errors());
            return false;
        }
    return true;
}

bool fetch_sprite(Session *session, unsigned gap, uint8_t bytes[SPRITE_BYTES]) {
    return transfer_test(session, gap, SPRITE_BYTES, BULK_SPRITE, bytes);
}
