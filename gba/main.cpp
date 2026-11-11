#include <gba.h>
#include <cstdio>

int main() {
    irqInit();
    irqEnable(IRQ_VBLANK);
    consoleDemoInit();
    std::puts("JoyBus PoC");
    for (;;) VBlankIntrWait();
}
