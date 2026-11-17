#ifndef POC_SESSION_H
#define POC_SESSION_H
#include "wire.h"

typedef struct {
    uint32_t sent, received, crc_errors, bad_headers, nacks, retries, timeouts, duplicates;
    // Audit J-02/J-03: SI trouble that used to end the session now ends only
    // the attempt; these count it.
    uint32_t write_errors, read_errors, resyncs;
} LinkStats;

// Why an attempt of a request ended.
enum AttemptEnd { ATTEMPT_OK, ATTEMPT_TIMEOUT, ATTEMPT_NACK, ATTEMPT_CRC, ATTEMPT_HEADER, ATTEMPT_WRITE };
#define SESSION_MAX_ATTEMPTS 4

// One request as seen by the host (audit J-03): when it started and ended,
// and how each attempt ended and how long it waited for a reply.
typedef struct {
    uint32_t seq;
    uint8_t type, attempts;
    uint8_t end[SESSION_MAX_ATTEMPTS];   // enum AttemptEnd
    uint32_t wait_us[SESSION_MAX_ATTEMPTS];
    uint64_t start, done;                // ticks
} RequestTrace;

typedef struct {
    LinkStats stats;
    uint32_t seq;
    // Per-attempt reply deadline; 0 = 120 ms (Phase 1 default). A peer that
    // answers once per game frame may need more after a heavy frame.
    uint32_t timeout_ms;
    RequestTrace last;
} Session;

enum Fault { NO_FAULT, BAD_REQUEST_CRC, DROP_ACK };
bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault);
// Why the last session_request returned false (diagnostics).
const char *session_last_failure(void);
#endif
