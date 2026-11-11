#ifndef POC_SPRITE_H
#define POC_SPRITE_H
#include <gccore.h>
#include <stdint.h>

bool draw_sprite(GXRModeObj *mode, void *fb, const uint8_t *bytes);
#endif
