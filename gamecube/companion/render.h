#ifndef COMPANION_RENDER_H
#define COMPANION_RENDER_H
#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

// Minimal 2D GX renderer: 640x480 orthographic, textured quads modulated by
// vertex colour. Generic UI (font, rectangles) is framework-owned; game
// assets arrive as textures converted from GBA 4bpp data.

#define RGBA(r, g, b, a) ((uint32_t)(r) << 24 | (uint32_t)(g) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))

void render_init(void);
void render_begin(void);
void render_end(void);
void render_rect(float x, float y, float w, float h, uint32_t rgba);
// 8x16 ASCII glyphs (libogc console font) scaled by `scale`.
void render_text(float x, float y, float scale, uint32_t rgba, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
void render_texture(GXTexObj *texture, float x, float y, float w, float h, uint32_t rgba);
// Draws the texel rectangle (u, v, uw, vh) of a texture of size tw x th.
void render_texture_part(GXTexObj *texture, unsigned tw, unsigned th, unsigned u, unsigned v, unsigned uw, unsigned vh,
                         float x, float y, float w, float h, uint32_t rgba);
// Restricts drawing to a screen rectangle (render_scissor_reset restores the full screen).
void render_scissor(float x, float y, float w, float h);
void render_scissor_reset(void);
// Same, mirrored horizontally (e.g. the player's battler, which only has a front sprite).
void render_texture_mirrored(GXTexObj *texture, float x, float y, float w, float h, uint32_t rgba);
// Call after new texture data was written (flushes the texture cache once).
void render_textures_changed(void);

// Converts GBA 8x8-tiled 4bpp pixels + 16-entry BGR555 palette to a GX
// RGB5A3 texture. Index 0 is transparent. Width/height must be multiples of 8.
// Returns the number of non-transparent pixels.
unsigned convert_4bpp_rgb5a3(const uint8_t *tiles, const uint8_t *palette, unsigned width, unsigned height,
                             uint16_t *out);
#endif
