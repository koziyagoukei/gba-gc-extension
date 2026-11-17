#include "diag.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include "../joybus.h"

static Diagnostics d;

static uint32_t now_ms(void) { return (uint32_t)ticks_to_millisecs(gettime()); }
static uint32_t us_between(uint64_t from, uint64_t to) { return (uint32_t)ticks_to_microsecs(to - from); }

void diag_note(DiagKind kind, uint16_t a, uint32_t b, uint32_t c) {
    d.ring[d.head] = (DiagEntry){now_ms(), (uint16_t)kind, a, b, c};
    d.head = (d.head + 1) % DIAG_RING;
    if (d.count < DIAG_RING) ++d.count;
}

const Diagnostics *diag_get(void) { return &d; }

void diag_session_open(uint32_t session) {
    // Counters and the ring survive reconnects; per-session timing restarts.
    d.session = session;
    d.si_errors_seen = joy_errors();
    diag_note(DIAG_SESSION_OPEN, (uint16_t)session, 0, 0);
}

void diag_exchange(bool ok, uint8_t type, const Session *s, uint32_t retries_before) {
    const RequestTrace *t = &s->last;
    ++d.requests;
    // Idle time between requests (our own processing, drawing, VSync) and the
    // time a request took (its attempts and waits) are separate quantities.
    if (d.last_done && type != HELLO && t->start > d.last_done) {
        const uint32_t idle = us_between(d.last_done, t->start);
        if (idle > d.max_idle_us) d.max_idle_us = idle;
        if (idle > DIAG_SLOW_POLL_US) diag_note(DIAG_SLOW_POLL, type, idle, t->seq);
    }
    const uint32_t took = t->done > t->start ? us_between(t->start, t->done) : 0;
    if (took > d.max_request_us) d.max_request_us = took;
    if (took > DIAG_SLOW_REQUEST_US) diag_note(DIAG_SLOW_REQUEST, type, took, t->attempts);
    d.last_done = t->done ? t->done : gettime();
    const uint32_t si = joy_errors();
    if (si != d.si_errors_seen) {
        diag_note(DIAG_SI_ERRORS, type, si - d.si_errors_seen, si);
        d.si_errors_seen = si;
    }
    if (s->stats.retries != retries_before) {
        ++d.retried_requests;
        diag_note(DIAG_RETRIES, type, s->stats.retries - retries_before, s->seq);
        for (unsigned i = 0; i < t->attempts && i < SESSION_MAX_ATTEMPTS; ++i)
            diag_note(DIAG_ATTEMPT, (uint16_t)(type << 8 | t->end[i]), t->seq, t->wait_us[i]);
    }
    if (ok) {
        d.last_good = t->done ? t->done : gettime();
    } else {
        ++d.failures;
        diag_note(DIAG_REQUEST_FAILED, type, s->seq, t->attempts);
    }
}

void diag_battle_begin(uint32_t battle_id) {
    d.battle_id = battle_id;
    diag_note(DIAG_BATTLE_BEGIN, 0, battle_id, 0);
}

void diag_battle_end(uint8_t outcome, uint32_t turns) { diag_note(DIAG_BATTLE_END, outcome, d.battle_id, turns); }

void diag_core(uint32_t us, unsigned events, unsigned capacity, unsigned turn) {
    if (us > d.max_core_us) d.max_core_us = us;
    if (events > d.max_events) d.max_events = events;
    if (us > DIAG_SLOW_CORE_US) diag_note(DIAG_SLOW_CORE, 0, us, events);
    if (events >= capacity) {
        ++d.event_log_full;
        diag_note(DIAG_EVENT_LOG_FULL, 0, turn, events);
    }
}

void diag_presentation(uint32_t us) {
    if (us > d.max_present_us) d.max_present_us = us;
    if (us > DIAG_SLOW_PRESENT_US) diag_note(DIAG_SLOW_PRESENT, 0, us, 0);
}

void diag_animation(unsigned steps, unsigned capacity, unsigned turn) {
    if (steps > d.max_steps) d.max_steps = steps;
    if (steps >= capacity) {
        ++d.animation_full;
        diag_note(DIAG_ANIMATION_FULL, 0, turn, steps);
    }
}

void diag_gba(const DiagGbaStats *g) {
    if (d.gba.valid) {
        const uint32_t old[] = {d.gba.resets, d.gba.overflows, d.gba.crc_errors, d.gba.duplicates,
                                d.gba.stale_drops, d.gba.resyncs};
        const uint32_t now[] = {g->resets, g->overflows, g->crc_errors, g->duplicates, g->stale_drops, g->resyncs};
        for (unsigned i = 0; i < 6; ++i)
            if (now[i] != old[i]) diag_note(DIAG_GBA_ANOMALY, (uint16_t)i, old[i], now[i]);
    }
    d.gba = *g;
    d.gba.valid = true;
}

static const char *kind_name(uint16_t kind) {
    static const char *const names[] = {"?", "SESSION", "FAILED", "RETRIES", "SI_ERRORS", "SLOW_POLL", "SLOW_CORE",
                                        "SLOW_PRESENT", "EVENTS_FULL", "ANIM_FULL", "GBA", "BATTLE", "END", "LOST",
                                        "ATTEMPT", "SLOW_REQUEST"};
    return kind < sizeof(names) / sizeof(*names) ? names[kind] : "?";
}

void diag_dump(const char *why, const Session *s) {
    const uint64_t t = gettime();
    SYS_Report("DIAG %s session=%u battle=%08x requests=%u failures=%u last_good_ms=%u seq=%u\n", why, d.session,
               d.battle_id, d.requests, d.failures, d.last_good ? (uint32_t)ticks_to_millisecs(t - d.last_good) : 0,
               s ? s->seq : 0);
    if (s)
        SYS_Report("DIAG link sent=%u received=%u crc=%u bad_headers=%u nacks=%u retries=%u timeouts=%u duplicates=%u "
                   "write_errors=%u read_errors=%u resyncs=%u retried_requests=%u\n",
                   s->stats.sent, s->stats.received, s->stats.crc_errors, s->stats.bad_headers, s->stats.nacks,
                   s->stats.retries, s->stats.timeouts, s->stats.duplicates, s->stats.write_errors,
                   s->stats.read_errors, s->stats.resyncs, d.retried_requests);
    const JoyStats *j = joy_stats();
    SYS_Report("DIAG si transfers=%u submit_fail=%u busy_waits=%u busy_fail=%u timeouts=%u status_errors=%u "
               "not_ready_polls=%u not_ready_reads=%u max_transfer_us=%u last_status=%x (cumulative, incl. probing)\n",
               j->transfers, j->submit_failures, j->busy_waits, j->busy_failures, j->completion_timeouts,
               j->status_errors, j->not_ready_polls, j->not_ready_reads, j->max_transfer_us, j->last_status);
    SYS_Report("DIAG peaks request_us=%u idle_us=%u core_us=%u present_us=%u events=%u steps=%u events_full=%u anim_full=%u\n",
               d.max_request_us, d.max_idle_us, d.max_core_us, d.max_present_us, d.max_events, d.max_steps,
               d.event_log_full, d.animation_full);
    if (d.gba.valid && d.gba.extended)
        SYS_Report("DIAG gba resets=%u overflows=%u crc=%u duplicates=%u nacks=%u stale_drops=%u resyncs=%u "
                   "max_queue=%u requests=%u (last PING)\n", d.gba.resets, d.gba.overflows, d.gba.crc_errors,
                   d.gba.duplicates, d.gba.nacks, d.gba.stale_drops, d.gba.resyncs, d.gba.max_queue, d.gba.requests);
    else if (d.gba.valid)
        SYS_Report("DIAG gba resets=%u overflows=%u crc=%u duplicates=%u (last PING)\n", d.gba.resets,
                   d.gba.overflows, d.gba.crc_errors, d.gba.duplicates);
    else
        SYS_Report("DIAG gba no PING reply yet\n");
    const unsigned first = (d.head + DIAG_RING - d.count) % DIAG_RING;
    for (unsigned i = 0; i < d.count; ++i) {
        const DiagEntry *e = &d.ring[(first + i) % DIAG_RING];
        SYS_Report("DIAG ring %2u t=%u %s a=%u b=%u c=%u\n", i, e->ms, kind_name(e->kind), e->a, e->b, e->c);
    }
}
