#include "asset_check.h"

bool asset_texture_valid(const AssetInfo *info, uint32_t *texture_bytes) {
    const uint64_t w = info->width, h = info->height;
    if (!w || !h || w > ASSET_MAX_TEXTURE_SIDE || h > ASSET_MAX_TEXTURE_SIDE || w % 8 || h % 8) return false;
    if (info->pixel_format == PIXEL_4BPP_TILED) {
        if (info->palette_format != PALETTE_BGR555_16) return false;
        if ((uint64_t)info->length != w * h / 2 + asset_palette_bytes(info->palette_format)) return false;
    } else if (info->pixel_format != PIXEL_4BPP_TILEMAP) {
        return false;
    }
    *texture_bytes = (uint32_t)((w * h * 2 + 31) & ~(uint64_t)31);
    return true;
}

bool asset_tilemap_layout(const uint8_t *d, uint32_t length, unsigned width, unsigned height, TilemapLayout *out) {
    if (length < TILEMAP_HEADER_BYTES) return false;
    const unsigned mw = d[0] | d[1] << 8, mh = d[2] | d[3] << 8, tiles = d[4] | d[5] << 8, banks = d[6];
    // Dimensions first, then every section size in 64-bit against the length.
    if (!mw || !mh || (uint64_t)mw * 8 != width || (uint64_t)mh * 8 != height) return false;
    if (!banks || banks > 16 || tiles > 1024) return false;  // 4-bit bank and 10-bit tile fields
    const uint64_t map = (uint64_t)mw * mh * 2, gfx = (uint64_t)tiles * 32, pal = (uint64_t)banks * 32;
    if (TILEMAP_HEADER_BYTES + map + gfx + pal != length) return false;
    *out = (TilemapLayout){
        .map_width = mw, .map_height = mh, .tiles = tiles, .banks = banks,
        .map_offset = TILEMAP_HEADER_BYTES,
        .tiles_offset = (uint32_t)(TILEMAP_HEADER_BYTES + map),
        .palette_offset = (uint32_t)(TILEMAP_HEADER_BYTES + map + gfx),
        .opaque = (d[7] & TILEMAP_FLAG_OPAQUE) != 0,
    };
    return true;
}
