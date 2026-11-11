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
static uint32_t errors;

static void completed(int32_t channel, uint32_t result) {
    (void)channel;
    status = result;
    done = true;
}

void joy_init(void) { SI_DisablePolling(SI_CHAN1_BIT); }
void joy_set_interval(unsigned us) { delay_us = us; }
uint32_t joy_errors(void) { return errors; }

static bool transfer(uint8_t command, uint32_t word, unsigned out, unsigned in) {
    if (!done) { ++errors; return false; }
    tx[0] = command;
    for (unsigned i = 0; i < 4; ++i) tx[i+1] = (uint8_t)(word >> (8*i));
    done = false;
    if (!SI_Transfer(PORT, tx, out, rx, in, completed, delay_us)) {
        done = true;
        ++errors;
        return false;
    }
    const uint64_t deadline = gettime() + millisecs_to_ticks(30);
    while (!done && gettime() < deadline) {}
    if (!done || status != 0) { ++errors; return false; }
    return true;
}

bool joy_status(void) { return transfer(0x00, 0, 1, 3) && rx[1] == 4; }
bool joy_reset(void) { return transfer(0xff, 0, 1, 3) && rx[1] == 4; }

bool joy_read(uint32_t *word) {
    // A READ without JOYSTAT.TRANS returns the previous register contents.
    // The command also gives the slave an IRQ to refill the register.
    const uint64_t deadline = gettime() + millisecs_to_ticks(30);
    do {
        if (!transfer(0x14, 0, 1, 5)) return false;
        if (rx[4] & 8u) break;
    } while (gettime() < deadline);
    if (!(rx[4] & 8u)) { ++errors; return false; }
    *word = 0;
    for (unsigned i = 0; i < 4; ++i) *word |= (uint32_t)rx[i] << (8*i);
    return true;
}

bool joy_write(uint32_t word) { return transfer(0x15, word, 5, 1); }
