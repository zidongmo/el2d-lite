#include "el2d/preview_renderer.h"

static float el2d_preview_clamp(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

uint16_t el2d_preview_mouth_color(void) {
    return el2d_rgb565(82u, 32u, 44u);
}

uint16_t el2d_preview_hair_color(void) {
    return el2d_rgb565(84u, 74u, 96u);
}

static int el2d_min3(int a, int b, int c) {
    int min = a < b ? a : b;
    return min < c ? min : c;
}

static int el2d_max3(int a, int b, int c) {
    int max = a > b ? a : b;
    return max > c ? max : c;
}

static int el2d_edge(int ax, int ay, int bx, int by, int px, int py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void el2d_framebuffer_fill_triangle(
    el2d_framebuffer *fb,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    uint16_t color
) {
    if (fb == 0 || fb->pixels == 0) {
        return;
    }
    int min_x = el2d_min3(x0, x1, x2);
    int max_x = el2d_max3(x0, x1, x2);
    int min_y = el2d_min3(y0, y1, y2);
    int max_y = el2d_max3(y0, y1, y2);
    if (min_x < 0) {
        min_x = 0;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_x >= (int)fb->width) {
        max_x = (int)fb->width - 1;
    }
    if (max_y >= (int)fb->height) {
        max_y = (int)fb->height - 1;
    }

    int area = el2d_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            int w0 = el2d_edge(x1, y1, x2, y2, x, y);
            int w1 = el2d_edge(x2, y2, x0, y0, x, y);
            int w2 = el2d_edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                el2d_framebuffer_set_pixel(fb, x, y, color);
            }
        }
    }
}

void el2d_preview_render(el2d_framebuffer *fb, const el2d_preview_pose *pose) {
    if (fb == 0 || pose == 0) {
        return;
    }

    const uint16_t background = el2d_rgb565(13u, 17u, 18u);
    const uint16_t coat = el2d_rgb565(34u, 42u, 48u);
    const uint16_t coat_shadow = el2d_rgb565(24u, 29u, 34u);
    const uint16_t shirt = el2d_rgb565(236u, 225u, 210u);
    const uint16_t skin = el2d_rgb565(241u, 210u, 184u);
    const uint16_t skin_shadow = el2d_rgb565(214u, 162u, 145u);
    const uint16_t hair = el2d_preview_hair_color();
    const uint16_t hair_shadow = el2d_rgb565(57u, 53u, 75u);
    const uint16_t eye = el2d_rgb565(30u, 35u, 45u);
    const uint16_t cheek = el2d_rgb565(219u, 120u, 122u);
    const uint16_t mouth = el2d_preview_mouth_color();

    float mouth_open = el2d_preview_clamp(pose->mouth_open, 0.0f, 1.0f);
    float eye_open = el2d_preview_clamp(pose->eye_open, 0.08f, 1.0f);
    int lean = (int)(pose->body_lean * 12.0f);
    int gaze_x = (int)(pose->gaze_x * 6.0f);
    int gaze_y = (int)(pose->gaze_y * 5.0f);

    int w = (int)fb->width;
    int h = (int)fb->height;
    int cx = w / 2;
    int cy = h / 2;
    int scale = w < h ? w : h;

    el2d_framebuffer_clear(fb, background);

    int body_cx = cx + lean / 2;
    int shoulder_y = cy + scale * 20 / 100;
    int waist_y = cy + scale * 50 / 100;
    int shoulder_half = scale * 31 / 100;
    int waist_half = scale * 20 / 100;
    el2d_framebuffer_fill_triangle(fb, body_cx - shoulder_half, shoulder_y, body_cx + shoulder_half, shoulder_y, body_cx - waist_half, waist_y, coat_shadow);
    el2d_framebuffer_fill_triangle(fb, body_cx + shoulder_half, shoulder_y, body_cx + waist_half, waist_y, body_cx - waist_half, waist_y, coat_shadow);
    el2d_framebuffer_fill_triangle(fb, body_cx - shoulder_half + scale * 5 / 100, shoulder_y, body_cx, shoulder_y + scale * 28 / 100, body_cx - waist_half / 2, waist_y, coat);
    el2d_framebuffer_fill_triangle(fb, body_cx + shoulder_half - scale * 5 / 100, shoulder_y, body_cx, shoulder_y + scale * 28 / 100, body_cx + waist_half / 2, waist_y, coat);
    el2d_framebuffer_fill_rect(fb, body_cx - scale * 7 / 100, shoulder_y + scale * 3 / 100, scale * 14 / 100, scale * 20 / 100, shirt);
    el2d_framebuffer_fill_rect(fb, body_cx - scale * 2 / 100, shoulder_y + scale * 4 / 100, scale * 4 / 100, scale * 33 / 100, coat_shadow);

    int hx = cx + lean;
    int hy = cy - scale * 9 / 100;
    int head_rx = scale * 20 / 100;
    int head_ry = scale * 25 / 100;
    el2d_framebuffer_fill_ellipse(fb, hx, hy - scale * 2 / 100, scale * 27 / 100, scale * 33 / 100, hair_shadow);
    el2d_framebuffer_fill_rect(fb, hx - scale * 26 / 100, hy - scale * 4 / 100, scale * 10 / 100, scale * 36 / 100, hair_shadow);
    el2d_framebuffer_fill_rect(fb, hx + scale * 16 / 100, hy - scale * 4 / 100, scale * 10 / 100, scale * 36 / 100, hair_shadow);

    el2d_framebuffer_fill_ellipse(fb, hx, hy + scale * 2 / 100, head_rx, head_ry, skin);
    el2d_framebuffer_fill_ellipse(fb, hx, hy + scale * 23 / 100, scale * 10 / 100, scale * 5 / 100, skin_shadow);
    el2d_framebuffer_fill_rect(fb, hx - scale * 7 / 100, hy + scale * 23 / 100, scale * 14 / 100, scale * 12 / 100, skin);

    el2d_framebuffer_fill_ellipse(fb, hx, hy - scale * 18 / 100, scale * 25 / 100, scale * 15 / 100, hair);
    el2d_framebuffer_fill_triangle(fb, hx - scale * 25 / 100, hy - scale * 23 / 100, hx - scale * 8 / 100, hy - scale * 24 / 100, hx - scale * 18 / 100, hy + scale * 9 / 100, hair);
    el2d_framebuffer_fill_triangle(fb, hx - scale * 12 / 100, hy - scale * 26 / 100, hx + scale * 4 / 100, hy - scale * 25 / 100, hx - scale * 5 / 100, hy + scale * 9 / 100, hair);
    el2d_framebuffer_fill_triangle(fb, hx + scale * 1 / 100, hy - scale * 25 / 100, hx + scale * 20 / 100, hy - scale * 22 / 100, hx + scale * 8 / 100, hy + scale * 7 / 100, hair);
    el2d_framebuffer_fill_triangle(fb, hx + scale * 12 / 100, hy - scale * 20 / 100, hx + scale * 27 / 100, hy - scale * 12 / 100, hx + scale * 17 / 100, hy + scale * 11 / 100, hair);

    int eye_ry = (int)(scale * 4 / 100 * eye_open);
    if (eye_ry < 1) {
        eye_ry = 1;
    }
    int left_eye_x = hx - scale * 9 / 100 + gaze_x;
    int right_eye_x = hx + scale * 9 / 100 + gaze_x;
    int eye_y = hy - scale * 4 / 100 + gaze_y;
    el2d_framebuffer_fill_ellipse(fb, left_eye_x, eye_y, scale * 4 / 100, eye_ry, eye);
    el2d_framebuffer_fill_ellipse(fb, right_eye_x, eye_y, scale * 4 / 100, eye_ry, eye);

    el2d_framebuffer_fill_ellipse(fb, hx - scale * 14 / 100, hy + scale * 7 / 100, scale * 4 / 100, scale * 2 / 100, cheek);
    el2d_framebuffer_fill_ellipse(fb, hx + scale * 14 / 100, hy + scale * 7 / 100, scale * 4 / 100, scale * 2 / 100, cheek);

    int mouth_rx = scale * (5 + (int)(mouth_open * 3.0f)) / 100;
    int mouth_ry = scale * (1 + (int)(mouth_open * 7.0f)) / 100;
    el2d_framebuffer_fill_ellipse(fb, hx, hy + scale * 13 / 100, mouth_rx, mouth_ry, mouth);
}
