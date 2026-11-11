#ifndef COMPANION_CACHE_H
#define COMPANION_CACHE_H
#include <gccore.h>
#include "message.h"

// Session RAM cache of client assets, keyed by (client build id, AssetKey).
// Entries are stored as ready-to-draw GX textures. Nothing is persisted; a
// different build id never matches, so assets of another ROM are not reused.

#define CACHE_ARENA_BYTES (1024u * 1024u)
#define CACHE_MAX_ENTRIES 128

typedef struct {
    uint32_t build_id;
    AssetKey key;
    uint16_t width, height;
    uint32_t source_bytes;   // bytes received over the link (pixels + palette)
    uint32_t texture_bytes;  // bytes of the RGB5A3 texture in the arena
    unsigned visible;        // non-transparent pixels
    GXTexObj texture;
} CacheEntry;

typedef struct {
    unsigned entries, hits, misses;
    uint32_t arena_used, source_bytes;
} CacheStats;

CacheEntry *cache_find(uint32_t build_id, const AssetKey *key);
// Converts and stores a verified asset. Returns NULL when the arena is full.
CacheEntry *cache_insert(uint32_t build_id, const AssetInfo *info, const uint8_t *data);
void cache_count_hit(void);
CacheStats cache_stats(void);
#endif
