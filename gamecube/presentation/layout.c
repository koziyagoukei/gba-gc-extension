#include "layout.h"

#include <stddef.h>

// Native TV canvas. Single uses active slot 0 (center) for each side.
static const BattleLayout single = {
    .format = BATTLE_FORMAT_SINGLE,
    .active_per_side = 1,
    .viewport = {0, 0, 640, 480},
    .field = {0, 0, 640, 354},
    .command = {24, 362, 592, 26},
    .message = {24, 394, 592, 66},
    .field_center = {320, 208},
    .anchors = {
        {{
            .feet = {176, 348}, .sprite_size = 144, .platform = {76, 326, 200, 28},
            .hud = {351, 253, 259, 87}, .hp_bar = {365, 288, 230, 10},
            .name = {365, 263}, .hp_text = {365, 305}, .status = {498, 307},
            .party = {551, 306}, .cursor = {176, 198},
            .attack_origin = {205, 271}, .hit_center = {176, 278},
        }},
        {{
            .feet = {467, 205}, .sprite_size = 128, .platform = {387, 191, 160, 22},
            .hud = {30, 44, 259, 87}, .hp_bar = {44, 79, 230, 10},
            .name = {44, 54}, .hp_text = {44, 96}, .status = {177, 98},
            .party = {230, 97}, .cursor = {467, 72},
            .attack_origin = {442, 137}, .hit_center = {467, 141},
        }},
    },
};

static const BattleLayout *const layouts[] = { &single };

const BattleLayout *battle_layout_find(enum BattleFormat format) {
    for (unsigned i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i)
        if (layouts[i]->format == format) return layouts[i];
    return NULL;
}

const BattlerAnchor *battle_layout_anchor(const BattleLayout *layout, Position pos) {
    if (!layout || pos.side >= 2 || pos.slot >= layout->active_per_side || pos.slot >= BC_MAX_ACTIVE) return NULL;
    return &layout->anchors[pos.side][pos.slot];
}
