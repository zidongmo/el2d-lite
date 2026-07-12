#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "el2d/framebuffer.h"
#include "el2d/preview_renderer.h"

static unsigned char rgb565_to_r(uint16_t color) {
    return (unsigned char)(((color >> 11u) & 0x1fu) * 255u / 31u);
}

static unsigned char rgb565_to_g(uint16_t color) {
    return (unsigned char)(((color >> 5u) & 0x3fu) * 255u / 63u);
}

static unsigned char rgb565_to_b(uint16_t color) {
    return (unsigned char)((color & 0x1fu) * 255u / 31u);
}

static int write_ppm(const char *path, const el2d_framebuffer *fb) {
    FILE *file = fopen(path, "wb");
    if (file == 0) {
        fprintf(stderr, "failed to open output: %s\n", path);
        return 1;
    }

    fprintf(file, "P6\n%u %u\n255\n", (unsigned)fb->width, (unsigned)fb->height);
    for (uint16_t y = 0; y < fb->height; ++y) {
        for (uint16_t x = 0; x < fb->width; ++x) {
            uint16_t color = fb->pixels[(size_t)y * fb->width + x];
            unsigned char rgb[3] = {rgb565_to_r(color), rgb565_to_g(color), rgb565_to_b(color)};
            if (fwrite(rgb, sizeof(rgb), 1u, file) != 1u) {
                fclose(file);
                fprintf(stderr, "failed to write output: %s\n", path);
                return 1;
            }
        }
    }

    fclose(file);
    return 0;
}

int main(int argc, char **argv) {
    const char *output = argc > 1 ? argv[1] : "build/el2d_preview.ppm";
    const char *state = argc > 2 ? argv[2] : "active";
    enum { width = 240, height = 216 };
    uint16_t pixels[width * height];
    el2d_framebuffer fb;

    if (el2d_framebuffer_init(&fb, pixels, width, height) != EL2D_OK) {
        fprintf(stderr, "failed to initialize framebuffer\n");
        return 1;
    }

    el2d_preview_pose pose = {
        .mouth_open = 0.08f,
        .eye_open = 0.95f,
        .gaze_x = 0.0f,
        .gaze_y = 0.0f,
        .body_lean = 0.0f,
    };

    if (strcmp(state, "active") == 0) {
        pose.mouth_open = 0.82f;
        pose.eye_open = 0.9f;
        pose.gaze_x = -0.08f;
        pose.body_lean = 0.32f;
    } else if (strcmp(state, "focus") == 0) {
        pose.mouth_open = 0.02f;
        pose.eye_open = 0.72f;
        pose.gaze_x = 0.2f;
        pose.gaze_y = 0.1f;
        pose.body_lean = -0.12f;
    } else if (strcmp(state, "rest") == 0) {
        pose.mouth_open = 0.04f;
        pose.eye_open = 0.36f;
        pose.gaze_y = 0.16f;
        pose.body_lean = -0.18f;
    }

    el2d_preview_render(&fb, &pose);
    if (write_ppm(output, &fb) != 0) {
        return 1;
    }

    printf("wrote %s (%ux%u RGB565 preview, state=%s)\n", output, (unsigned)width, (unsigned)height, state);
    return 0;
}
