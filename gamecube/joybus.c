#include "joybus.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/si.h>

#define PORT 1
static unsigned delay_us = 160;
static uint8_t tx[8] __attribute__((aligned(32)));
static uint8_t rx[8] __attribute__((aligned(32)));
static volatile bool done = true;
static volatile uint32_t status;
static volatile uint64_t completed_at;
static uint32_t errors;
static JoyStats stats;

static void completed(int32_t channel, uint32_t result) {
    (void)channel;
    status = result;
    completed_at = gettime();
    done = true;
}

void joy_init(void) { SI_DisablePolling(SI_CHAN1_BIT); }
void joy_set_interval(unsigned us) { delay_us = us; }
uint32_t joy_errors(void) { return errors; }
const JoyStats *joy_stats(void) { return &stats; }

static void wait_done(uint64_t deadline) {
    while (!done && gettime() < deadline) {}
}

static bool transfer(uint8_t command, uint32_t word, unsigned out, unsigned in) {
    if (!done) {
        // The previous transfer timed out but its callback is still due; a new
        // transfer cannot start before it (audit J-02). Give it a while.
        ++stats.busy_waits;
        wait_done(gettime() + millisecs_to_ticks(30));
        if (!done) { ++stats.busy_failures; ++errors; return false; }
    }
    tx[0] = command;
    for (unsigned i = 0; i < 4; ++i) tx[i+1] = (uint8_t)(word >> (8*i));
    done = false;
    const uint64_t start = gettime();
    if (!SI_Transfer(PORT, tx, out, rx, in, completed, delay_us)) {
        done = true;
        ++stats.submit_failures;
        ++errors;
        return false;
    }
    ++stats.transfers;
    wait_done(start + millisecs_to_ticks(30));
    if (!done) { ++stats.completion_timeouts; ++errors; return false; }
    const uint32_t us = (uint32_t)ticks_to_microsecs(completed_at - start);
    if (us > stats.max_transfer_us) stats.max_transfer_us = us;
    if (status != 0) { ++stats.status_errors; stats.last_status = status; ++errors; return false; }
    return true;
}

bool joy_status(void) { return transfer(0x00, 0, 1, 3) && rx[1] == 4; }
bool joy_reset(void) { return transfer(0xff, 0, 1, 3) && rx[1] == 4; }

JoyResult joy_read_word(uint32_t *word) {
    // A READ without JOYSTAT.TRANS returns the previous register contents.
    // The command also gives the slave an IRQ to refill the register.
    const uint64_t deadline = gettime() + millisecs_to_ticks(30);
    do {
        if (!transfer(0x14, 0, 1, 5)) return JOY_ERROR;
        if (rx[4] & 8u) break;
        ++stats.not_ready_polls;
    } while (gettime() < deadline);
    if (!(rx[4] & 8u)) { ++stats.not_ready_reads; return JOY_NOT_READY; }
    *word = 0;
    for (unsigned i = 0; i < 4; ++i) *word |= (uint32_t)rx[i] << (8*i);
    return JOY_OK;
}

bool joy_read(uint32_t *word) {
    const JoyResult r = joy_read_word(word);
    if (r == JOY_NOT_READY) ++errors;  // as before for the raw handshake's callers
    return r == JOY_OK;
}

bool joy_write(uint32_t word) { return transfer(0x15, word, 5, 1); }
