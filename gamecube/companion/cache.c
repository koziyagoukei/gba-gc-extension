#include "cache.h"
#include "render.h"
#include <ogc/cache.h>

static uint8_t arena[CACHE_ARENA_BYTES] __attribute__((aligned(32)));
static CacheEntry entries[CACHE_MAX_ENTRIES];
static unsigned count, hits, misses;
static uint32_t used, source_total;

CacheEntry *cache_find(uint32_t build_id, const AssetKey *key) {
    for (unsigned i = 0; i < count; ++i)
        if (entries[i].build_id == build_id && asset_key_equal(&entries[i].key, key)) return &entries[i];
    return NULL;
}

void cache_count_hit(void) { ++hits; }

// PIXEL_4BPP_TILEMAP: a GBA text BG (tilemap + tiles + palette banks).
static bool convert_tilemap(const uint8_t *d, uint32_t length, unsigned width, unsigned height, uint16_t *out,
                            unsigned *visible) {
    if (length < TILEMAP_HEADER_BYTES) return false;
    const unsigned mw = d[0] | d[1] << 8, mh = d[2] | d[3] << 8, tiles = d[4] | d[5] << 8, banks = d[6];
    const bool opaque = d[7] & TILEMAP_FLAG_OPAQUE;
    const uint8_t *map = d + TILEMAP_HEADER_BYTES, *gfx = map + mw * mh * 2, *pal = gfx + tiles * 32;
    if (mw * 8 != width || mh * 8 != height || !banks || pal + banks * 32 != d + length) return false;
    *visible = 0;
    for (unsigned ty = 0; ty < mh; ++ty)
        for (unsigned tx = 0; tx < mw; ++tx) {
            const unsigned e = map[(ty * mw + tx) * 2] | map[(ty * mw + tx) * 2 + 1] << 8;
            const unsigned tile = e & 0x3ff, bank = (e >> 12) < banks ? e >> 12 : 0;
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 8; ++x) {
                    const unsigned sx = e & 0x400 ? 7 - x : x, sy = e & 0x800 ? 7 - y : y;
                    unsigned index = 0;
                    if (tile < tiles) index = (gfx[tile * 32 + sy * 4 + sx / 2] >> (4 * (sx & 1))) & 15;
                    const uint8_t *c = pal + bank * 32 + index * 2;
                    const unsigned color = c[0] | c[1] << 8;
                    const unsigned px = tx * 8 + x, py = ty * 8 + y;
                    const unsigned t = (py / 4) * (width / 4) + px / 4;
                    out[t * 16 + (py % 4) * 4 + px % 4] = index == 0 && !opaque ? 0 :
                        (uint16_t)(0x8000 | (color & 31) << 10 | (color & 0x3e0) | ((color >> 10) & 31));
                    *visible += index != 0 || opaque;
                }
        }
    return true;
}

CacheEntry *cache_insert(uint32_t build_id, const AssetInfo *info, const uint8_t *data) {
    const uint32_t palette = asset_palette_bytes(info->palette_format);
    const uint32_t pixels = (uint32_t)info->width * info->height / 2;
    const uint32_t bytes = ((uint32_t)info->width * info->height * 2 + 31) & ~31u;
    if (count == CACHE_MAX_ENTRIES || used + bytes > CACHE_ARENA_BYTES) return NULL;
    if (info->width % 8 || info->height % 8) return NULL;
    if (info->pixel_format == PIXEL_4BPP_TILED &&
        (info->palette_format != PALETTE_BGR555_16 || info->length != pixels + palette))
        return NULL;
    if (info->pixel_format != PIXEL_4BPP_TILED && info->pixel_format != PIXEL_4BPP_TILEMAP) return NULL;
    CacheEntry *e = &entries[count];
    uint16_t *texels = (uint16_t *)(arena + used);
    e->build_id = build_id;
    e->key = info->key;
    e->width = info->width;
    e->height = info->height;
    e->source_bytes = info->length;
    e->texture_bytes = bytes;
    if (info->pixel_format == PIXEL_4BPP_TILEMAP) {
        if (!convert_tilemap(data, info->length, info->width, info->height, texels, &e->visible)) return NULL;
    } else {
        e->visible = convert_4bpp_rgb5a3(data, data + pixels, info->width, info->height, texels);
    }
    DCFlushRange(texels, bytes);
    GX_InitTexObj(&e->texture, texels, info->width, info->height, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&e->texture, GX_NEAR, GX_NEAR);
    render_textures_changed();
    used += bytes;
    source_total += info->length;
    ++misses;
    ++count;
    return e;
}

CacheStats cache_stats(void) {
    return (CacheStats){count, hits, misses, used, source_total};
}
