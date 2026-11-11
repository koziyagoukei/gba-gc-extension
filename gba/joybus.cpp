#include "joybus.hpp"

#define LINK_CUBE_QUEUE_SIZE 64
#include "../third_party/gba-link-connection/LinkCube.hpp"

namespace {
LinkCube cube;
void serial_irq() { cube._onSerial(); }
}

namespace joybus {
void init() {
    irqSet(IRQ_SERIAL, serial_irq);
    cube.activate();
    irqEnable(IRQ_SERIAL);
}

void disconnect() {
    irqDisable(IRQ_SERIAL);
    cube.deactivate();
    for (unsigned i = 0; i < 30; ++i) VBlankIntrWait();
    init();
}

bool read(uint32_t &word) {
    if (!cube.canRead()) return false;
    word = cube.read();
    return true;
}

bool send(const uint32_t *words, unsigned count) {
    const auto ime = REG_IME;
    REG_IME = 0;
    const bool fits = count <= LINK_CUBE_QUEUE_SIZE - cube.pendingCount();
    if (fits)
        for (unsigned i = 0; i < count; ++i) cube.send(words[i]);
    REG_IME = ime;
    return fits;
}

bool reset_seen() {
    const auto ime = REG_IME;
    REG_IME = 0;
    const bool seen = cube.didReset();
    REG_IME = ime;
    return seen;
}

bool overflow_seen() {
    const auto ime = REG_IME;
    REG_IME = 0;
    const bool seen = cube.didQueueOverflow();
    REG_IME = ime;
    return seen;
}
}
