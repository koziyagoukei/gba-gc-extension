#include <gba.h>
#include <cstdio>
#include "joybus.hpp"
#include "round_trip.h"

int main() {
    irqInit();
    irqEnable(IRQ_VBLANK);
    consoleDemoInit();
    joybus::init();
    std::puts("Waiting for GameCube");
    for (;;) {
        if (joybus::reset_seen()) std::puts("\x1b[2JWaiting for GameCube");
        uint32_t word;
        if (!joybus::read(word)) continue;
        if (word == RAW_DROP) {
            std::puts("\x1b[2JLink offline for test");
            joybus::disconnect();
            std::puts("\x1b[2JWaiting for GameCube");
        } else if (word == RAW_PROBE) {
            const uint32_t reply[] = {RAW_VALUES, 0, UINT32_MAX, RAW_PATTERN};
            joybus::send(reply, 4);
        } else if (word == RAW_ACK) {
            const uint32_t confirm = RAW_CONFIRM;
            joybus::send(&confirm, 1);
            std::puts("\x1b[2JGC CONNECTED");
        }
    }
}
