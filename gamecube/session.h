#ifndef POC_SESSION_H
#define POC_SESSION_H
#include "wire.h"

typedef struct {
    uint32_t sent, received, crc_errors, bad_headers, nacks, retries, timeouts, duplicates;
} LinkStats;

typedef struct {
    LinkStats stats;
    uint32_t seq;
} Session;

enum Fault { NO_FAULT, BAD_REQUEST_CRC, DROP_ACK };
bool session_request(Session *s, Frame *request, Frame *reply, enum Fault fault);
#endif
