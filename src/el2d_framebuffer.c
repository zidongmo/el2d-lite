#include "el2d/framebuffer.h"

#include <string.h>

uint16_t el2d_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((uint16_t)(r & 0xf8u) << 8u) | ((uint16_t)(g & 0xfcu) << 3u) | ((uint16_t)b >> 3u));
}

el2d_result el2d_framebuffer_init(el2d_framebuffer *fb, uint16_t *pixels, uint16_t width, uint16_t height) {
    if (fb == 0 || pixels == 0 || width == 0u || height == 0u) {
        return EL2D_ERROR_INVALID_ARGUMENT;
    }
    fb->pixels = pixels;
    fb->width = width;
    fb->height = height;
    return EL2D_OK;
}

void el2d_framebuffer_clear(el2d_framebuffer *fb, uint16_t color) {
    if (fb == 0 || fb->pixels == 0) {
        return;
    }
    size_t count = (size_t)fb->width * (size_t)fb->height;
    if ((uint8_t)color == (uint8_t)(color >> 8u)) {
        memset(fb->pixels, (uint8_t)color, count * sizeof(uint16_t));
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        fb->pixels[i] = color;
    }
}

void el2d_framebuffer_set_pixel(el2d_framebuffer *fb, int x, int y, uint16_t color) {
    if (fb == 0 || fb->pixels == 0 || x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height) {
        return;
    }
    fb->pixels[(size_t)y * fb->width + (size_t)x] = color;
}

void el2d_framebuffer_fill_rect(el2d_framebuffer *fb, int x, int y, int width, int height, uint16_t color) {
    if (fb == 0 || fb->pixels == 0 || width <= 0 || height <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)fb->width) {
        x1 = (int)fb->width;
    }
    if (y1 > (int)fb->height) {
        y1 = (int)fb->height;
    }
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            el2d_framebuffer_set_pixel(fb, px, py, color);
        }
    }
}

void el2d_framebuffer_fill_ellipse(el2d_framebuffer *fb, int cx, int cy, int rx, int ry, uint16_t color) {
    if (fb == 0 || fb->pixels == 0 || rx <= 0 || ry <= 0) {
        return;
    }
    int x0 = cx - rx;
    int x1 = cx + rx;
    int y0 = cy - ry;
    int y1 = cy + ry;
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int limit = rx2 * ry2;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx;
            int dy = y - cy;
            if ((dx * dx * ry2 + dy * dy * rx2) <= limit) {
                el2d_framebuffer_set_pixel(fb, x, y, color);
            }
        }
    }
}
