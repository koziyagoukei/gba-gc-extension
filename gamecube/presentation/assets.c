#include "assets.h"

#include <ogc/cache.h>
#include "../companion/render.h"

// Original art for the Link Plaza and Meadow Arena. RGB5A3 tile generation
// keeps the artwork independent of the ROM and the platform cache.
#define TILE_SIZE 16
#define MEADOW_W 160
#define MEADOW_H 96
#define RGB5(r, g, b) (uint16_t)(0x8000u | (r) << 10 | (g) << 5 | (b))
static uint16_t tiles[ART_MEADOW][TILE_SIZE * TILE_SIZE] __attribute__((aligned(32)));
static uint16_t meadow[MEADOW_W * MEADOW_H] __attribute__((aligned(32)));
static GXTexObj textures[ART_COUNT];

static bool ellipse(int x, int y, int cx, int cy, int rx, int ry) {
    const int dx = x - cx, dy = y - cy;
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

static uint16_t tile_pixel(UiArt art, int x, int y) {
    const int dx = x < 8 ? 7 - x : x - 8;
    const int dy = y < 8 ? 7 - y : y - 8;
    switch (art) {
    case ART_PANEL:
        return dx + dy > 13 ? 0 : 0xffff;
    case ART_HP_FRAME:
        return dx + dy > 13 ? 0 : dx == 7 || dy == 7 ? 0xffff : 0xbdef;
    case ART_CURSOR:
        if (y > 13 || dx > (13 - y) / 2) return 0;
        return y < 3 || dx == (13 - y) / 2 ? RGB5(31, 29, 17) : RGB5(31, 22, 7);
    case ART_STATUS:
        return dx + dy < 8 ? 0xffff : 0;
    case ART_PARTY:
        return dx * dx + dy * dy < 52 ? 0xffff : 0;
    case ART_EFFECT:
        return (dx < 2 || dy < 2 || dx == dy) && dx + dy <= 8 ? 0xffff : 0;
    case ART_STAGE:
        if (!ellipse(x, y, 7, 7, 7, 6)) return 0;
        if (y >= 10) return RGB5(12, 16, 7);
        if (ellipse(x, y, 7, 6, 6, 4)) return ((x + 2 * y) % 7 == 0) ? RGB5(22, 21, 12) : RGB5(24, 23, 14);
        return RGB5(28, 27, 17);
    case ART_LINK_CREST:
        if (dx + dy > 9) return 0;
        if (dx + dy >= 8) return RGB5(31, 29, 17);
        if (dx <= 1 || dy <= 1) return RGB5(28, 31, 25);
        return y < 8 ? RGB5(31, 20, 16) : RGB5(7, 22, 20);
    case ART_WALLPAPER:
        if (dx + dy == 5) return RGB5(23, 28, 28);
        if (x == 0 || y == 0) return RGB5(25, 29, 30);
        return RGB5(28, 31, 30);
    case ART_MEADOW: case ART_COUNT:
        break;
    }
    return 0;
}

static int wave(int x, int period, int height) {
    const int t = x % period;
    return (t < period / 2 ? t : period - t) * height * 2 / period;
}

static uint16_t meadow_pixel(int x, int y) {
    // Warm clouds, distant hills, a tree line, and flowers in the foreground.
    uint16_t color = y < 19 ? RGB5(18, 26, 31) : RGB5(21, 29, 31);
    if (ellipse(x, y, 28, 17, 15, 4) || ellipse(x, y, 27, 14, 7, 5) ||
        ellipse(x, y, 116, 11, 18, 3) || ellipse(x, y, 120, 8, 9, 5)) color = RGB5(28, 31, 29);
    if (y > 26 - wave(x + 12, 76, 12) - wave(x, 40, 5)) color = RGB5(16, 26, 25);
    if (y > 37 - wave(x + 40, 94, 13) - wave(x + 6, 32, 4)) color = RGB5(13, 24, 19);
    if (y > 43 - wave(x, 60, 5) - wave(x + 7, 16, 4)) color = RGB5(9, 22, 15);
    if (y >= 46) color = y < 62 ? RGB5(17, 26, 14) : y < 77 ? RGB5(15, 25, 13) : RGB5(13, 23, 12);
    if (y > 47 && (x * 13 + y * 17) % 131 < 3) color = RGB5(18, 27, 15);
    if (y > 57 && (x * 29 + y * 17) % 289 < 3) color = (x & 1) ? RGB5(31, 26, 13) : RGB5(31, 18, 20);
    if (y > 90 - wave(x + 5, 24, 5) - wave(x + 2, 10, 3)) color = RGB5(7, 16, 9);
    return color;
}

static void upload(UiArt art, uint16_t *pixels, unsigned w, unsigned h) {
    DCFlushRange(pixels, w * h * sizeof(*pixels));
    GX_InitTexObj(&textures[art], pixels, w, h, GX_TF_RGB5A3,
                  art == ART_WALLPAPER ? GX_REPEAT : GX_CLAMP,
                  art == ART_WALLPAPER ? GX_REPEAT : GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&textures[art], GX_NEAR, GX_NEAR);
}

static void put(uint16_t *pixels, unsigned w, unsigned x, unsigned y, uint16_t value) {
    const unsigned tile = y / 4 * (w / 4) + x / 4;
    pixels[tile * 16 + y % 4 * 4 + x % 4] = value;
}

void ui_art_init(void) {
    for (unsigned i = 0; i < ART_MEADOW; ++i) {
        for (unsigned y = 0; y < TILE_SIZE; ++y)
            for (unsigned x = 0; x < TILE_SIZE; ++x)
                put(tiles[i], TILE_SIZE, x, y, tile_pixel(i, x, y));
        upload(i, tiles[i], TILE_SIZE, TILE_SIZE);
    }
    for (unsigned y = 0; y < MEADOW_H; ++y)
        for (unsigned x = 0; x < MEADOW_W; ++x)
            put(meadow, MEADOW_W, x, y, meadow_pixel(x, y));
    upload(ART_MEADOW, meadow, MEADOW_W, MEADOW_H);
    render_textures_changed();
}

GXTexObj *ui_art(UiArt art) { return &textures[art]; }

static void panel_shape(float x, float y, float w, float h, uint32_t color) {
    const float corner = 6;
    render_rect(x + corner, y, w - 2 * corner, h, color);
    render_rect(x, y + corner, corner, h - 2 * corner, color);
    render_rect(x + w - corner, y + corner, corner, h - 2 * corner, color);
    GXTexObj *tile = ui_art(ART_PANEL);
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned col = 0; col < 2; ++col)
            render_texture_part(tile, TILE_SIZE, TILE_SIZE, col * 12, row * 12, 4, 4,
                                x + (col ? w - corner : 0), y + (row ? h - corner : 0), corner, corner, color);
}

void ui_panel(float x, float y, float w, float h, uint32_t border, uint32_t fill) {
    panel_shape(x, y, w, h, border);
    panel_shape(x + 3, y + 3, w - 6, h - 6, fill);
}

AssetKey battle_sprite_key(const PokemonRecord *mon, Position pos) {
    AssetKey key = mon->sprite;
    if (pos.side == 0) key.variant |= PKMN_SPRITE_BACK;
    return key;
}

AssetKey battle_move_label_key(uint16_t move) {
    return (AssetKey){ASSET_TEXT_LABEL, LABEL_ID(LABEL_MOVE, move), 0, 0};
}

CacheEntry *battle_sprite(uint32_t build, const PokemonRecord *mon, Position pos) {
    const AssetKey key = battle_sprite_key(mon, pos);
    return cache_find(build, &key);
}

CacheEntry *battle_label(uint32_t build, const PokemonRecord *mon) {
    const AssetKey key = pkmn_nickname_label(mon);
    return cache_find(build, &key);
}

CacheEntry *battle_move_label(uint32_t build, uint16_t move) {
    const AssetKey key = battle_move_label_key(move);
    return cache_find(build, &key);
}
