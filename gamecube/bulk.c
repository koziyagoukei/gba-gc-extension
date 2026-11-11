#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include "bulk.h"
#include "joybus.h"
#include "../protocol/bulk.h"

static bool transfer_test(Session *session, unsigned gap, uint32_t size) {
    const LinkStats start = session->stats;
    const uint32_t si_start = joy_errors();
    const uint64_t t0 = gettime();
    Frame request = {.type=BULK_BEGIN,.len=4}, reply;
    wire_put32(request.payload, size);
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
            if (reply.payload[i] != bulk_pattern(off+i)) {
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
    printf("Bulk %u KiB OK: %u.%03u KiB/s\n", size/1024, rate/1000, rate%1000);
    return true;
}

bool bulk_test(Session *session, unsigned gap) {
    const uint32_t sizes[] = {1024,4096,16384,65536};
    for (unsigned i=0; i<sizeof(sizes)/sizeof(*sizes); ++i)
        if (!transfer_test(session, gap, sizes[i])) {
            SYS_Report("POC3 FAIL gap=%u bytes=%u\n", gap, sizes[i]);
            return false;
        }
    return true;
}
