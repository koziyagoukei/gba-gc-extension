#ifndef GC_BATTLE_LAYOUT_H
#define GC_BATTLE_LAYOUT_H

#include "../battle/battle_core.h"

typedef struct { float x, y; } ScreenPoint;
typedef struct { float x, y, w, h; } ScreenRect;

typedef struct {
    ScreenPoint feet;
    float sprite_size;
    ScreenRect platform;
    ScreenRect hud, hp_bar;
    ScreenPoint name, hp_text, status, party, cursor;
    ScreenPoint attack_origin, hit_center;
} BattlerAnchor;

typedef struct {
    enum BattleFormat format;
    uint8_t active_per_side;
    ScreenRect viewport, field, command, message;
    ScreenPoint field_center;
    BattlerAnchor anchors[2][BC_MAX_ACTIVE];
} BattleLayout;

const BattleLayout *battle_layout_find(enum BattleFormat format);
const BattlerAnchor *battle_layout_anchor(const BattleLayout *layout, Position pos);
#endif
