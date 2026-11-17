#ifndef JOYBUS_HOST_H
#define JOYBUS_HOST_H
#include <stdbool.h>
#include <stdint.h>

// Outcome of one word read: a word, no word yet (the GBA has not refilled
// JOY_TRANS within the poll window), or a failed SI transfer whose result is
// unknown (the word may have been consumed on the GBA side).
typedef enum { JOY_OK, JOY_NOT_READY, JOY_ERROR } JoyResult;

// SI transfer counters (cumulative; diagnostics only).
typedef struct {
    uint32_t transfers;        // SI_Transfer calls that started
    uint32_t submit_failures;  // SI_Transfer refused the transfer
    uint32_t busy_waits;       // a new transfer had to wait for the previous callback
    uint32_t busy_failures;    // ... and the previous one never completed
    uint32_t completion_timeouts;  // no callback within 30 ms
    uint32_t status_errors;    // callback with a nonzero SI status
    uint32_t not_ready_polls;  // READs that returned the old JOY_TRANS (no TRANS flag)
    uint32_t not_ready_reads;  // joy_read_word calls that gave up with JOY_NOT_READY
    uint32_t max_transfer_us;  // longest submit-to-callback time
    uint32_t last_status;      // SI status of the last failed transfer
} JoyStats;

void joy_init(void);
void joy_set_interval(unsigned us);
bool joy_reset(void);
bool joy_status(void);
JoyResult joy_read_word(uint32_t *word);
bool joy_read(uint32_t *word);  // joy_read_word(word) == JOY_OK
bool joy_write(uint32_t word);
uint32_t joy_errors(void);
const JoyStats *joy_stats(void);
#endif
