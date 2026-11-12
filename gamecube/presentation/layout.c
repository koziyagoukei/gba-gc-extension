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

// Compact HUD cards sit below the corresponding position; the center of
// a Rotation lineup stands forward of the wings.
#define CARD(x, feet_y, size, card_y) { \
    .feet = {x, feet_y}, .sprite_size = size, \
    .platform = {x - size * 0.65f, feet_y - 12, size * 1.3f, 20}, \
    .hud = {x - 95, card_y, 190, 49}, .hp_bar = {x - 87, card_y + 20, 174, 8}, \
    .name = {x - 87, card_y + 2}, .hp_text = {x - 87, card_y + 31}, \
    .status = {x + 26, card_y + 32}, .party = {x + 61, card_y + 32}, \
    .cursor = {x, feet_y - size - 8}, \
    .attack_origin = {x, feet_y - size / 2}, .hit_center = {x, feet_y - size / 2}, \
}
#define MULTI_CANVAS \
    .viewport = {0, 0, 640, 480}, .field = {0, 0, 640, 354}, \
    .command = {24, 362, 592, 26}, .message = {24, 394, 592, 66}, .field_center = {320, 208}

static const BattleLayout double_layout = {
    .format = BATTLE_FORMAT_DOUBLE, .active_per_side = 2, MULTI_CANVAS,
    .anchors = {
        {CARD(176, 301, 99, 304), CARD(422, 301, 99, 304)},
        {CARD(218, 139, 86, 142), CARD(464, 139, 86, 142)},
    },
};
static const BattleLayout triple = {
    .format = BATTLE_FORMAT_TRIPLE, .active_per_side = 3, MULTI_CANVAS,
    .anchors = {
        {CARD(104, 301, 94, 304), CARD(310, 301, 99, 304), CARD(516, 301, 94, 304)},
        {CARD(124, 139, 82, 142), CARD(330, 139, 86, 142), CARD(536, 139, 82, 142)},
    },
};
static const BattleLayout rotation = {
    .format = BATTLE_FORMAT_ROTATION, .active_per_side = 3, MULTI_CANVAS,
    .anchors = {
        {CARD(104, 272, 82, 282), CARD(310, 301, 108, 304), CARD(516, 272, 82, 282)},
        {CARD(124, 127, 70, 131), CARD(330, 151, 94, 154), CARD(536, 127, 70, 131)},
    },
};

static const BattleLayout *const layouts[] = { &single, &double_layout, &triple, &rotation };

const BattleLayout *battle_layout_find(enum BattleFormat format) {
    for (unsigned i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i)
        if (layouts[i]->format == format) return layouts[i];
    return NULL;
}

const BattlerAnchor *battle_layout_anchor(const BattleLayout *layout, Position pos) {
    if (!layout || pos.side >= 2 || pos.slot >= layout->active_per_side || pos.slot >= BC_MAX_ACTIVE) return NULL;
    return &layout->anchors[pos.side][pos.slot];
}
