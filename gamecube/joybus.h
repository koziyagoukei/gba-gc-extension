#ifndef JOYBUS_HOST_H
#define JOYBUS_HOST_H
#include <stdbool.h>
#include <stdint.h>
void joy_init(void);
void joy_set_interval(unsigned us);
bool joy_reset(void);
bool joy_status(void);
bool joy_read(uint32_t *word);
bool joy_write(uint32_t word);
uint32_t joy_errors(void);
#endif
