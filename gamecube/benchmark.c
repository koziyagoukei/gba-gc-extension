#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "benchmark.h"
#include "joybus.h"

#define SAMPLE_COUNT 10000u
static uint32_t samples[SAMPLE_COUNT];

bool ping(Session *session, uint32_t pattern, Frame *reply, enum Fault fault) {
    Frame req = {.len=4, .type=PING};
    wire_put32(req.payload, pattern);
    return session_request(session, &req, reply, fault) && reply->len == 20 &&
           wire_get32(reply->payload) == pattern;
}

static int compare(const void *a, const void *b) {
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

bool reliability_test(Session *session, unsigned interval_us, bool faults) {
    uint64_t sum = 0;
    Frame reply;
    const LinkStats start = session->stats;
    const uint32_t si_start = joy_errors();
    const uint64_t began = gettime();
    for (unsigned i = 0; i < SAMPLE_COUNT; ++i) {
        enum Fault fault = NO_FAULT;
        if (faults && i == 500) fault = BAD_REQUEST_CRC;
        if (faults && i == 501) fault = DROP_ACK;
        const uint64_t t0 = gettime();
        if (!ping(session, UINT32_C(0x12340000) | i, &reply, fault)) {
            SYS_Report("POC2 FAIL sample=%u retries=%u timeouts=%u si=%u\n", i,
                       session->stats.retries-start.retries, session->stats.timeouts-start.timeouts,
                       joy_errors()-si_start);
            return false;
        }
        samples[i] = (uint32_t)ticks_to_microsecs(gettime()-t0);
        sum += samples[i];
        if ((i+1) % 1000 == 0) printf("PING %u / %u\n", i+1, SAMPLE_COUNT);
    }
    const uint32_t elapsed = (uint32_t)ticks_to_microsecs(gettime()-began);
    qsort(samples, SAMPLE_COUNT, sizeof(*samples), compare);
    const LinkStats s = session->stats;
    SYS_Report("POC2 PASS gap=%u faults=%u samples=%u sent=%u recv=%u crc_host=%u crc_gba=%u "
               "nack=%u retries=%u dup_host=%u dup_gba=%u overflow_gba=%u reset_gba=%u timeout=%u si=%u\n",
               interval_us, faults, SAMPLE_COUNT, s.sent-start.sent, s.received-start.received,
               s.crc_errors-start.crc_errors, wire_get32(reply.payload+12),
               s.nacks-start.nacks, s.retries-start.retries, s.duplicates-start.duplicates,
               wire_get32(reply.payload+16), wire_get32(reply.payload+8), wire_get32(reply.payload+4),
               s.timeouts-start.timeouts, joy_errors()-si_start);
    SYS_Report("POC2 RTT us avg=%u median=%u p95=%u p99=%u elapsed=%u\n",
               (uint32_t)(sum/SAMPLE_COUNT), (samples[4999]+samples[5000])/2, samples[9499], samples[9899], elapsed);
    printf("10,000 PINGs OK; retry %u timeout %u\n", s.retries-start.retries, s.timeouts-start.timeouts);
    printf("RTT us median %u / p95 %u / p99 %u\n",
           (samples[4999]+samples[5000])/2, samples[9499], samples[9899]);
    return true;
}
