// Native tests of the image asset size checks (audit M-01).
#include "asset_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[64 * 1024];

static uint32_t tilemap(unsigned mw, unsigned mh, unsigned tiles, unsigned banks) {
    data[0] = (uint8_t)mw; data[1] = (uint8_t)(mw >> 8);
    data[2] = (uint8_t)mh; data[3] = (uint8_t)(mh >> 8);
    data[4] = (uint8_t)tiles; data[5] = (uint8_t)(tiles >> 8);
    data[6] = (uint8_t)banks; data[7] = 0;
    return TILEMAP_HEADER_BYTES + mw * mh * 2 + tiles * 32 + banks * 32;
}

int main(void) {
    TilemapLayout l;
    // A 240x160 battle background: 30x20 map, 100 tiles, 2 banks.
    uint32_t n = tilemap(30, 20, 100, 2);
    assert(asset_tilemap_layout(data, n, 240, 160, &l));
    assert(l.tiles_offset == 8 + 1200 && l.palette_offset == 8 + 1200 + 3200 && !l.opaque);
    assert(!asset_tilemap_layout(data, n - 1, 240, 160, &l));         // short
    assert(!asset_tilemap_layout(data, n + 1, 240, 160, &l));         // trailing bytes
    assert(!asset_tilemap_layout(data, n, 248, 160, &l));             // size disagrees with the map
    assert(!asset_tilemap_layout(data, 7, 240, 160, &l));             // no header
    n = tilemap(0, 20, 100, 2);
    assert(!asset_tilemap_layout(data, n, 0, 160, &l));               // empty map
    n = tilemap(30, 20, 100, 0);
    assert(!asset_tilemap_layout(data, n, 240, 160, &l));             // no palette
    n = tilemap(30, 20, 100, 17);
    assert(!asset_tilemap_layout(data, n, 240, 160, &l));             // more banks than a tile can name
    // Header values whose sizes wrap 32-bit arithmetic: refused, no pointer formed.
    tilemap(0xffff, 0xffff, 0xffff, 255);
    assert(!asset_tilemap_layout(data, 24576, 0xffff * 8u, 0xffff * 8u, &l));
    tilemap(0x8000, 0x8000, 0, 1);
    assert(!asset_tilemap_layout(data, 8 + 32, 0x40000, 0x40000, &l));

    AssetInfo info = {.width = 64, .height = 64, .pixel_format = PIXEL_4BPP_TILED,
                      .palette_format = PALETTE_BGR555_16, .length = 64 * 64 / 2 + 32};
    uint32_t bytes = 0;
    assert(asset_texture_valid(&info, &bytes) && bytes == 64 * 64 * 2);
    info.length -= 1;
    assert(!asset_texture_valid(&info, &bytes));
    info.length += 1;
    info.width = 0;
    assert(!asset_texture_valid(&info, &bytes));
    info.width = 2048;
    assert(!asset_texture_valid(&info, &bytes));                      // beyond a GX texture
    info.width = 60;
    assert(!asset_texture_valid(&info, &bytes));                      // not a tile multiple
    info = (AssetInfo){.width = 0xfff8, .height = 0xfff8, .pixel_format = PIXEL_4BPP_TILED,
                       .palette_format = PALETTE_BGR555_16, .length = 32};
    assert(!asset_texture_valid(&info, &bytes));                      // 32-bit product would wrap
    info = (AssetInfo){.width = 240, .height = 160, .pixel_format = PIXEL_4BPP_TILEMAP, .length = 100};
    assert(asset_texture_valid(&info, &bytes) && bytes == 240 * 160 * 2);
    info.pixel_format = 9;
    assert(!asset_texture_valid(&info, &bytes));
    puts("asset-check-test OK");
    return 0;
}
