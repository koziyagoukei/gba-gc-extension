// GC Extension companion host (Phase 2): shows the party of a GBA running
// pokeemerald-expansion in GC Extension Mode. GBA on GC port 2, controller on
// port 1 (START ends the session, A reconnects, Z = 3 s link-silence test).
#include <gccore.h>
#include "../joybus.h"
#include "party_view.h"
#include "render.h"
#include "../presentation/assets.h"

int main(void) {
    render_init();
    ui_art_init();
    PAD_Init();
    joy_init(); // keep SI polling off the GBA channel after PAD_Init
    joy_set_interval(COMPANION_INTERVAL);
    party_view_init();
    for (;;) {
        PAD_ScanPads();
        const u16 down = PAD_ButtonsDown(0);
        party_view_update(down & PAD_BUTTON_START, down & PAD_BUTTON_A, down & PAD_TRIGGER_Z);
        party_view_draw();
    }
}
