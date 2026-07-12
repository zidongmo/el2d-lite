#include <stdint.h>

#include "el2d_esp_preview.h"
#include "esp_log.h"

#if defined(__has_include)
#if __has_include("el2d_generated_preview_config.h")
#include "el2d_generated_preview_config.h"
#endif
#endif

#ifndef EL2D_PREVIEW_MODEL_NAME
#define EL2D_PREVIEW_MODEL_NAME "procedural-preview"
#endif

#ifndef EL2D_PREVIEW_CANVAS_WIDTH
#define EL2D_PREVIEW_CANVAS_WIDTH 240
#endif

#ifndef EL2D_PREVIEW_CANVAS_HEIGHT
#define EL2D_PREVIEW_CANVAS_HEIGHT 216
#endif

#ifndef EL2D_PREVIEW_TEXTURE_BYTES_RGB565
#define EL2D_PREVIEW_TEXTURE_BYTES_RGB565 0
#endif

#ifndef EL2D_PREVIEW_MOUTH_PARAMETER_ID
#define EL2D_PREVIEW_MOUTH_PARAMETER_ID "ParamMouthOpenY"
#endif

#ifndef EL2D_PREVIEW_LEFT_EYE_PARAMETER_ID
#define EL2D_PREVIEW_LEFT_EYE_PARAMETER_ID "ParamEyeLOpen"
#endif

#ifndef EL2D_PREVIEW_RIGHT_EYE_PARAMETER_ID
#define EL2D_PREVIEW_RIGHT_EYE_PARAMETER_ID "ParamEyeROpen"
#endif

static const char *TAG = "el2d_preview";

void app_main(void) {
    enum {
        width = EL2D_PREVIEW_CANVAS_WIDTH,
        height = EL2D_PREVIEW_CANVAS_HEIGHT,
    };
    static uint16_t pixels[width * height];
    const el2d_esp_preview_input input = {
        .mouth_open = 0.75f,
        .eye_open = 0.9f,
        .gaze_x = -0.08f,
        .gaze_y = 0.0f,
        .body_lean = 0.25f,
    };

    int result = el2d_esp_preview_render_rgb565(pixels, width, height, &input);
    ESP_LOGI(TAG, "model=%s canvas=%dx%d texture_rgb565=%d mouth=%s eyes=%s/%s", EL2D_PREVIEW_MODEL_NAME, width, height, EL2D_PREVIEW_TEXTURE_BYTES_RGB565, EL2D_PREVIEW_MOUTH_PARAMETER_ID, EL2D_PREVIEW_LEFT_EYE_PARAMETER_ID, EL2D_PREVIEW_RIGHT_EYE_PARAMETER_ID);
    ESP_LOGI(TAG, "render result=%d first_pixel=0x%04x center_pixel=0x%04x", result, pixels[0], pixels[(height / 2) * width + (width / 2)]);
}
