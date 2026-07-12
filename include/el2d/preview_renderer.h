#ifndef EL2D_PREVIEW_RENDERER_H
#define EL2D_PREVIEW_RENDERER_H

#include <stdint.h>

#include "el2d/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_preview_pose {
    float mouth_open;
    float eye_open;
    float gaze_x;
    float gaze_y;
    float body_lean;
} el2d_preview_pose;

uint16_t el2d_preview_mouth_color(void);
uint16_t el2d_preview_hair_color(void);
void el2d_preview_render(el2d_framebuffer *fb, const el2d_preview_pose *pose);

#ifdef __cplusplus
}
#endif

#endif
