#include "sprite.h"
#include "bulk.h"
#include <ogc/cache.h>
#include <stdio.h>

static uint8_t fifo[256*1024] __attribute__((aligned(32)));
static uint16_t pixels[64*64] __attribute__((aligned(32)));

bool draw_sprite(GXRModeObj *mode, void *fb, const uint8_t *bytes) {
    unsigned visible = 0;
    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            const unsigned gba_tile = (y/8)*8+x/8;
            const uint8_t packed = bytes[gba_tile*32+(y%8)*4+(x%8)/2];
            const unsigned index = (packed>>(4*(x%2)))&15;
            const uint8_t *p = bytes+2048+index*2;
            const unsigned color = p[0] | (p[1]<<8);
            const unsigned gx_tile = (y/4)*16+x/4;
            pixels[gx_tile*16+(y%4)*4+x%4] = index == 0 ? 0 :
                (uint16_t)(0x8000 | ((color&31)<<10) | (color&0x3e0) | ((color>>10)&31));
            visible += index != 0;
        }
    }
    if (!visible || visible == 64*64) return false;
    DCFlushRange(pixels, sizeof(pixels));
    GX_Init(fifo, sizeof(fifo));
    GX_SetCopyClear((GXColor){14,19,30,255}, 0x00ffffff);
    GX_SetViewport(0,0,mode->fbWidth,mode->efbHeight,0,1);
    GX_SetScissor(0,0,mode->fbWidth,mode->efbHeight);
    const unsigned xfb_height = GX_SetDispCopyYScale(GX_GetYScaleFactor(mode->efbHeight,mode->xfbHeight));
    GX_SetDispCopySrc(0,0,mode->fbWidth,mode->efbHeight);
    GX_SetDispCopyDst(mode->fbWidth,xfb_height);
    GX_SetCopyFilter(mode->aa,mode->sample_pattern,GX_TRUE,mode->vfilter);
    GX_SetFieldMode(mode->field_rendering, mode->viHeight == 2*mode->xfbHeight);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetPixelFmt(mode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24,GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_CLEAR);
    Mtx44 projection;
    Mtx model;
    guOrtho(projection,0,480,0,640,0,300);
    guMtxIdentity(model);
    guMtxTransApply(model,model,0,0,-5);
    GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
    GX_LoadPosMtxImm(model,GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XY,GX_F32,0);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GX_SetNumChans(1);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);

    GXTexObj texture;
    GX_InitTexObj(&texture,pixels,64,64,GX_TF_RGB5A3,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjFilterMode(&texture,GX_NEAR,GX_NEAR);
    GX_InvalidateTexAll();
    GX_LoadTexObj(&texture,GX_TEXMAP0);

    GX_CopyDisp(fb,GX_TRUE); // Clear the EFB before compositing the sprite.
    GX_Begin(GX_QUADS,GX_VTXFMT0,4);
    GX_Position2f32(224,116); GX_TexCoord2f32(0,0);
    GX_Position2f32(416,116); GX_TexCoord2f32(1,0);
    GX_Position2f32(416,308); GX_TexCoord2f32(1,1);
    GX_Position2f32(224,308); GX_TexCoord2f32(0,1);
    GX_End();
    GX_CopyDisp(fb,GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    SYS_Report("POC4 GX PASS 64x64 4bpp visible=%u\n", visible);
    return true;
}
