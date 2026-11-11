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

CacheEntry *cache_insert(uint32_t build_id, const AssetInfo *info, const uint8_t *data) {
    const uint32_t palette = asset_palette_bytes(info->palette_format);
    const uint32_t pixels = (uint32_t)info->width * info->height / 2;
    const uint32_t bytes = ((uint32_t)info->width * info->height * 2 + 31) & ~31u;
    if (count == CACHE_MAX_ENTRIES || used + bytes > CACHE_ARENA_BYTES) return NULL;
    if (info->pixel_format != PIXEL_4BPP_TILED || info->palette_format != PALETTE_BGR555_16 ||
        info->width % 8 || info->height % 8 || info->length != pixels + palette)
        return NULL;
    CacheEntry *e = &entries[count];
    uint16_t *texels = (uint16_t *)(arena + used);
    e->build_id = build_id;
    e->key = info->key;
    e->width = info->width;
    e->height = info->height;
    e->source_bytes = info->length;
    e->texture_bytes = bytes;
    e->visible = convert_4bpp_rgb5a3(data, data + pixels, info->width, info->height, texels);
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
