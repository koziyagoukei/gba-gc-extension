#pragma once

#include <gba.h>

namespace joybus {
void init();
void disconnect();
bool read(uint32_t &word);
bool send(const uint32_t *words, unsigned count);
bool reset_seen();
bool overflow_seen();
}
