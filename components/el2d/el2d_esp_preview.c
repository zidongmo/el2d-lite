#include "el2d_esp_preview.h"

#include "el2d/framebuffer.h"
#include "el2d/preview_renderer.h"

int el2d_esp_preview_render_rgb565(uint16_t *pixels, uint16_t width, uint16_t height, const el2d_esp_preview_input *input) {
    if (pixels == 0 || input == 0) {
        return -1;
    }

    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, width, height) != EL2D_OK) {
        return -1;
    }

    el2d_preview_pose pose = {
        .mouth_open = input->mouth_open,
        .eye_open = input->eye_open,
        .gaze_x = input->gaze_x,
        .gaze_y = input->gaze_y,
        .body_lean = input->body_lean,
    };
    el2d_preview_render(&fb, &pose);
    return 0;
}
