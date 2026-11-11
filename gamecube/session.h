#ifndef POC_SESSION_H
#define POC_SESSION_H
#include "wire.h"

typedef struct {
    uint32_t sent, received, crc_errors, bad_headers, nacks, retries, timeouts, duplicates;
} LinkStats;

typedef struct {
    LinkStats stats;
    uint32_t seq;
    // Per-attempt reply deadline; 0 = 120 ms (Phase 1 default). A peer that
    // answers once per game frame may need more after a heavy frame.
    uint32_t timeout_ms;
} Session;

enum Fault { NO_FAULT, BAD_REQUEST_CRC, DROP_ACK };
bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault);
// Why the last session_request returned false (diagnostics).
const char *session_last_failure(void);
#endif
