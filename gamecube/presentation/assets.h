#ifndef GC_BATTLE_ASSETS_H
#define GC_BATTLE_ASSETS_H
#include "../companion/cache.h"
#include "../battle/battle_core.h"

typedef enum {
    ART_PANEL, ART_HP_FRAME, ART_CURSOR, ART_STATUS, ART_PARTY, ART_EFFECT,
    ART_STAGE, ART_LINK_CREST, ART_WALLPAPER, ART_MEADOW,
    ART_COUNT,
} UiArt;

// Shared Link Plaza / Party Roster / Meadow Arena colors (RRGGBBAA).
#define UI_INK   0x243e49ffu
#define UI_MUTED 0x4a6872ffu
#define UI_CREAM 0xfff6dfffu
#define UI_MINT  0x92e0bdffu
#define UI_SKY   0xa5ddfaffu
#define UI_ROSE  0xf49b91ffu
#define UI_GOLD  0xffd268ffu
#define UI_EDGE  0x527e78ffu
#define UI_WHITE 0xffffffffu

void ui_art_init(void);
GXTexObj *ui_art(UiArt art);
void ui_panel(float x, float y, float w, float h, uint32_t border, uint32_t fill);
CacheEntry *battle_sprite(uint32_t build, const PokemonRecord *mon, Position pos);
CacheEntry *battle_label(uint32_t build, const PokemonRecord *mon);
CacheEntry *battle_move_label(uint32_t build, uint16_t move);
AssetKey battle_sprite_key(const PokemonRecord *mon, Position pos);
AssetKey battle_move_label_key(uint16_t move);
#endif
