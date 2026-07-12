#ifndef EL2D_FRAMEBUFFER_H
#define EL2D_FRAMEBUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "el2d/model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_framebuffer {
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
} el2d_framebuffer;

uint16_t el2d_rgb565(uint8_t r, uint8_t g, uint8_t b);
el2d_result el2d_framebuffer_init(el2d_framebuffer *fb, uint16_t *pixels, uint16_t width, uint16_t height);
void el2d_framebuffer_clear(el2d_framebuffer *fb, uint16_t color);
void el2d_framebuffer_set_pixel(el2d_framebuffer *fb, int x, int y, uint16_t color);
void el2d_framebuffer_fill_rect(el2d_framebuffer *fb, int x, int y, int width, int height, uint16_t color);
void el2d_framebuffer_fill_ellipse(el2d_framebuffer *fb, int cx, int cy, int rx, int ry, uint16_t color);

#ifdef __cplusplus
}
#endif

#endif
