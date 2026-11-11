#include "render.h"
#include <ogc/cache.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern uint8_t console_font_8x16[]; // libogc, 256 glyphs x 16 rows, MSB = left pixel

#define FONT_W 128
#define FONT_H 256

static uint8_t fifo[256 * 1024] __attribute__((aligned(32)));
static uint16_t font_pixels[FONT_W * FONT_H] __attribute__((aligned(32)));
static uint16_t white_pixels[4 * 4] __attribute__((aligned(32)));
static GXTexObj font_texture, white_texture;
static GXRModeObj *mode;
static void *xfb[2];
static unsigned current;
static bool invalidate = true;

static void put_texel(uint16_t *out, unsigned width, unsigned x, unsigned y, uint16_t value) {
    const unsigned tile = (y / 4) * (width / 4) + x / 4;
    out[tile * 16 + (y % 4) * 4 + x % 4] = value;
}

unsigned convert_4bpp_rgb5a3(const uint8_t *tiles, const uint8_t *palette, unsigned width, unsigned height,
                             uint16_t *out) {
    unsigned visible = 0;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const unsigned gba_tile = (y / 8) * (width / 8) + x / 8;
            const uint8_t packed = tiles[gba_tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            const unsigned index = (packed >> (4 * (x % 2))) & 15;
            const unsigned color = palette[index * 2] | palette[index * 2 + 1] << 8;
            put_texel(out, width, x, y, index == 0 ? 0 :
                      (uint16_t)(0x8000 | (color & 31) << 10 | (color & 0x3e0) | ((color >> 10) & 31)));
            visible += index != 0;
        }
    return visible;
}

static void build_font(void) {
    for (unsigned c = 0; c < 256; ++c)
        for (unsigned row = 0; row < 16; ++row) {
            const uint8_t bits = console_font_8x16[c * 16 + row];
            for (unsigned bit = 0; bit < 8; ++bit)
                put_texel(font_pixels, FONT_W, (c % 16) * 8 + bit, (c / 16) * 16 + row,
                          (bits >> (7 - bit)) & 1 ? 0xffff : 0x0000);
        }
    for (unsigned i = 0; i < 16; ++i) white_pixels[i] = 0xffff;
    DCFlushRange(font_pixels, sizeof(font_pixels));
    DCFlushRange(white_pixels, sizeof(white_pixels));
    GX_InitTexObj(&font_texture, font_pixels, FONT_W, FONT_H, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&font_texture, GX_NEAR, GX_NEAR);
    GX_InitTexObj(&white_texture, white_pixels, 4, 4, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&white_texture, GX_NEAR, GX_NEAR);
}

void render_init(void) {
    VIDEO_Init();
    mode = VIDEO_GetPreferredMode(NULL);
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    memset(fifo, 0, sizeof(fifo));
    GX_Init(fifo, sizeof(fifo));
    GX_SetCopyClear((GXColor){14, 19, 30, 255}, 0x00ffffff);
    GX_SetViewport(0, 0, mode->fbWidth, mode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, mode->fbWidth, mode->efbHeight);
    const unsigned xfb_height = GX_SetDispCopyYScale(GX_GetYScaleFactor(mode->efbHeight, mode->xfbHeight));
    GX_SetDispCopySrc(0, 0, mode->fbWidth, mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth, xfb_height);
    GX_SetCopyFilter(mode->aa, mode->sample_pattern, GX_TRUE, mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2 * mode->xfbHeight);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetPixelFmt(mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    Mtx44 projection;
    Mtx model;
    guOrtho(projection, 0, 480, 0, 640, 0, 300);
    guMtxIdentity(model);
    guMtxTransApply(model, model, 0, 0, -5);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_LoadPosMtxImm(model, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    build_font();
}

void render_textures_changed(void) { invalidate = true; }

void render_begin(void) {
    if (invalidate) {
        GX_InvalidateTexAll();
        invalidate = false;
    }
}

void render_end(void) {
    GX_DrawDone();
    GX_CopyDisp(xfb[current], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[current]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    current ^= 1;
}

static void quad(float x, float y, float w, float h, uint32_t rgba, float u0, float v0, float u1, float v1) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_Color1u32(rgba); GX_TexCoord2f32(u0, v0);
    GX_Position2f32(x + w, y);     GX_Color1u32(rgba); GX_TexCoord2f32(u1, v0);
    GX_Position2f32(x + w, y + h); GX_Color1u32(rgba); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x, y + h);     GX_Color1u32(rgba); GX_TexCoord2f32(u0, v1);
    GX_End();
}

void render_rect(float x, float y, float w, float h, uint32_t rgba) {
    GX_LoadTexObj(&white_texture, GX_TEXMAP0);
    quad(x, y, w, h, rgba, 0, 0, 1, 1);
}

void render_texture(GXTexObj *texture, float x, float y, float w, float h, uint32_t rgba) {
    GX_LoadTexObj(texture, GX_TEXMAP0);
    quad(x, y, w, h, rgba, 0, 0, 1, 1);
}

void render_texture_mirrored(GXTexObj *texture, float x, float y, float w, float h, uint32_t rgba) {
    GX_LoadTexObj(texture, GX_TEXMAP0);
    quad(x, y, w, h, rgba, 1, 0, 0, 1);
}

void render_text(float x, float y, float scale, uint32_t rgba, const char *fmt, ...) {
    char text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    GX_LoadTexObj(&font_texture, GX_TEXMAP0);
    const float w = 8 * scale, h = 16 * scale;
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c, x += w) {
        if (*c == ' ') continue;
        const float u = (*c % 16) * 8.0f / FONT_W, v = (*c / 16) * 16.0f / FONT_H;
        quad(x, y, w, h, rgba, u, v, u + 8.0f / FONT_W, v + 16.0f / FONT_H);
    }
}
