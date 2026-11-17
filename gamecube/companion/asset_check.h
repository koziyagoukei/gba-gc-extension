#ifndef COMPANION_ASSET_CHECK_H
#define COMPANION_ASSET_CHECK_H
// Size checks for received image assets, done before any pointer into the
// data is formed (audit M-01). No GX here, so they are tested natively
// (asset_check_test.c).
#include <stdbool.h>
#include <stdint.h>
#include "message.h"

#define ASSET_MAX_TEXTURE_SIDE 1024u  // GX texture limit

// A PIXEL_4BPP_TILEMAP asset: header, then map, tiles and palette banks.
typedef struct {
    unsigned map_width, map_height, tiles, banks;
    uint32_t map_offset, tiles_offset, palette_offset;
    bool opaque;
} TilemapLayout;

// Checks an image's dimensions and pixel/palette format against the received
// length; on success *texture_bytes is the RGB5A3 texture size (32-aligned).
bool asset_texture_valid(const AssetInfo *info, uint32_t *texture_bytes);
// Checks a tilemap header and that every section fits the data exactly.
bool asset_tilemap_layout(const uint8_t *data, uint32_t length, unsigned width, unsigned height,
                          TilemapLayout *out);
#endif
