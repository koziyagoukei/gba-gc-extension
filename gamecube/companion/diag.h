#ifndef COMPANION_DIAG_H
#define COMPANION_DIAG_H
// Link and battle diagnostics for the companion host: counters, peaks and a
// ring buffer of recent anomalies. Printed with SYS_Report on link loss, at
// battle end and on request, so a field report can show what happened just
// before a fault. Nothing here changes behaviour.
#include <stdbool.h>
#include <stdint.h>
#include "../session.h"

typedef enum {
    DIAG_SESSION_OPEN = 1,  // a = session number
    DIAG_REQUEST_FAILED,    // a = frame type, b = seq
    DIAG_RETRIES,           // a = frame type, b = retries in this request
    DIAG_SI_ERRORS,         // b = new SI errors since the last request
    DIAG_SLOW_POLL,         // b = gap between two requests (us)
    DIAG_SLOW_CORE,         // b = battle core time for one turn (us), c = events
    DIAG_SLOW_PRESENT,      // b = presentation time for one frame (us)
    DIAG_EVENT_LOG_FULL,    // b = turn, c = events
    DIAG_ANIMATION_FULL,    // b = turn, c = steps
    DIAG_GBA_ANOMALY,       // a = which GBA counter (DiagGbaCounter), b = old, c = new
    DIAG_BATTLE_BEGIN,      // b = battle id
    DIAG_BATTLE_END,        // a = outcome, b = battle id, c = turns
    DIAG_LINK_LOST,         // b = seq
} DiagKind;

typedef enum { DIAG_GBA_RESETS, DIAG_GBA_OVERFLOWS, DIAG_GBA_CRC, DIAG_GBA_DUPLICATES } DiagGbaCounter;

typedef struct {
    uint32_t ms;     // since boot
    uint16_t kind, a;
    uint32_t b, c;
} DiagEntry;

#define DIAG_RING 64

// The GBA's own JoyBus counters, from its PING reply.
typedef struct {
    bool valid;
    uint32_t resets, overflows, crc_errors, duplicates;
} DiagGbaStats;

typedef struct {
    uint32_t session, battle_id;
    uint64_t last_good, last_request;  // ticks; 0 = none yet
    uint32_t si_errors_seen;
    uint32_t requests, failures;
    uint32_t max_poll_us, max_core_us, max_present_us, max_events, max_steps;
    uint32_t event_log_full, animation_full;
    DiagGbaStats gba;
    DiagEntry ring[DIAG_RING];
    unsigned head, count;
} Diagnostics;

// Thresholds for ring entries (not limits; just what is worth recording).
#define DIAG_SLOW_POLL_US 100000u
#define DIAG_SLOW_CORE_US 8000u
#define DIAG_SLOW_PRESENT_US 12000u

void diag_session_open(uint32_t session);
// After every request: ok, the request type, and the session after it.
void diag_exchange(bool ok, uint8_t type, const Session *session, uint32_t retries_before);
void diag_battle_begin(uint32_t battle_id);
void diag_battle_end(uint8_t outcome, uint32_t turns);
void diag_core(uint32_t us, unsigned events, unsigned capacity, unsigned turn);
void diag_presentation(uint32_t us);
void diag_animation(unsigned steps, unsigned capacity, unsigned turn);
void diag_gba(const DiagGbaStats *stats);
void diag_note(DiagKind kind, uint16_t a, uint32_t b, uint32_t c);
// Prints counters, peaks and the ring (oldest first).
void diag_dump(const char *why, const Session *session);
const Diagnostics *diag_get(void);
#endif
