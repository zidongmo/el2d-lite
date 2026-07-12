#ifndef EL2D_ESP_PREVIEW_H
#define EL2D_ESP_PREVIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_esp_preview_input {
    float mouth_open;
    float eye_open;
    float gaze_x;
    float gaze_y;
    float body_lean;
} el2d_esp_preview_input;

int el2d_esp_preview_render_rgb565(uint16_t *pixels, uint16_t width, uint16_t height, const el2d_esp_preview_input *input);

#ifdef __cplusplus
}
#endif

#endif
