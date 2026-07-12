#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "el2d/clip.h"
#include "el2d/easing.h"
#include "el2d/framebuffer.h"
#include "el2d/mesh_renderer.h"
#include "el2d/model.h"
#include "el2d/preview_renderer.h"
#include "el2d_mesh_rasterizer.h"

static void require_close(float actual, float expected, float tolerance, const char *label) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "%s: expected %.6f, got %.6f\n", label, expected, actual);
        exit(1);
    }
}

static void require_u16(uint16_t actual, uint16_t expected, const char *label) {
    if (actual != expected) {
        fprintf(stderr, "%s: expected 0x%04x, got 0x%04x\n", label, expected, actual);
        exit(1);
    }
}

static void require_size(size_t actual, size_t expected, const char *label) {
    if (actual != expected) {
        fprintf(stderr, "%s: expected %zu, got %zu\n", label, expected, actual);
        exit(1);
    }
}

static void test_easing_curves(void) {
    require_close(el2d_ease(EL2D_EASING_LINEAR, 0.25f), 0.25f, 0.0001f, "linear");
    require_close(el2d_ease(EL2D_EASING_EASE_IN_QUAD, 0.50f), 0.25f, 0.0001f, "ease in quad");
    require_close(el2d_ease(EL2D_EASING_EASE_OUT_QUAD, 0.50f), 0.75f, 0.0001f, "ease out quad");
    require_close(el2d_ease(EL2D_EASING_EASE_IN_OUT_QUAD, 0.75f), 0.875f, 0.0001f, "ease in out quad");
    require_close(el2d_ease(EL2D_EASING_EASE_OUT_CUBIC, 0.50f), 0.875f, 0.0001f, "ease out cubic");
    require_close(el2d_ease(EL2D_EASING_LINEAR, -1.0f), 0.0f, 0.0001f, "clamp low");
    require_close(el2d_ease(EL2D_EASING_LINEAR, 2.0f), 1.0f, 0.0001f, "clamp high");
}

static void test_model_parameters_and_transition(void) {
    el2d_model model;
    const char *params[] = {"ParamMouthOpen", "ParamAngleX"};
    if (el2d_model_init(&model, params, 2) != EL2D_OK) {
        fprintf(stderr, "model init failed\n");
        exit(1);
    }

    if (el2d_model_set_parameter(&model, "ParamMouthOpen", 0.2f) != EL2D_OK) {
        fprintf(stderr, "set parameter failed\n");
        exit(1);
    }
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 0.2f, 0.0001f, "direct set");

    el2d_parameter_target targets[] = {
        {"ParamMouthOpen", 1.0f},
        {"ParamAngleX", -6.0f},
    };
    if (el2d_model_transition_to(&model, targets, 2, 1000u, EL2D_EASING_LINEAR) != EL2D_OK) {
        fprintf(stderr, "transition failed\n");
        exit(1);
    }

    el2d_model_update(&model, 500u);
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 0.6f, 0.0001f, "transition midpoint mouth");
    require_close(el2d_model_get_parameter(&model, "ParamAngleX"), -3.0f, 0.0001f, "transition midpoint angle");

    el2d_model_update(&model, 500u);
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 1.0f, 0.0001f, "transition final mouth");
    require_close(el2d_model_get_parameter(&model, "ParamAngleX"), -6.0f, 0.0001f, "transition final angle");
}

static void test_zero_duration_transition_is_immediate(void) {
    el2d_model model;
    const char *params[] = {"ParamMouthOpen"};
    if (el2d_model_init(&model, params, 1) != EL2D_OK) {
        fprintf(stderr, "zero duration model init failed\n");
        exit(1);
    }

    el2d_parameter_target targets[] = {
        {"ParamMouthOpen", 0.85f},
    };
    if (el2d_model_transition_to(&model, targets, 1, 0u, EL2D_EASING_EASE_OUT_CUBIC) != EL2D_OK) {
        fprintf(stderr, "zero duration transition failed\n");
        exit(1);
    }
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 0.85f, 0.0001f, "zero duration immediate");
}

static void test_clip_curve_application(void) {
    el2d_model model;
    const char *params[] = {"ParamMouthOpen"};
    if (el2d_model_init(&model, params, 1) != EL2D_OK) {
        fprintf(stderr, "clip model init failed\n");
        exit(1);
    }

    const el2d_curve_key keys[] = {
        {0u, 0.0f},
        {100u, 1.0f},
        {200u, 0.0f},
    };
    const el2d_curve curves[] = {
        {"ParamMouthOpen", keys, 3, EL2D_EASING_LINEAR},
    };
    const el2d_clip clip = {"mouth_open_close", curves, 1, 200u};

    if (el2d_clip_apply(&clip, &model, 50u, 1.0f) != EL2D_OK) {
        fprintf(stderr, "clip apply failed\n");
        exit(1);
    }
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 0.5f, 0.0001f, "clip midpoint up");

    if (el2d_model_set_parameter(&model, "ParamMouthOpen", 0.0f) != EL2D_OK) {
        fprintf(stderr, "clip reset failed\n");
        exit(1);
    }
    if (el2d_clip_apply(&clip, &model, 150u, 0.5f) != EL2D_OK) {
        fprintf(stderr, "clip apply with weight failed\n");
        exit(1);
    }
    require_close(el2d_model_get_parameter(&model, "ParamMouthOpen"), 0.25f, 0.0001f, "clip weighted down");
}

static void test_framebuffer_rgb565_and_clear(void) {
    require_u16(el2d_rgb565(255u, 0u, 0u), 0xf800u, "rgb565 red");
    require_u16(el2d_rgb565(0u, 255u, 0u), 0x07e0u, "rgb565 green");
    require_u16(el2d_rgb565(0u, 0u, 255u), 0x001fu, "rgb565 blue");

    uint16_t pixels[16];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 4u, 4u) != EL2D_OK) {
        fprintf(stderr, "framebuffer init failed\n");
        exit(1);
    }
    el2d_framebuffer_clear(&fb, 0x1234u);
    for (size_t i = 0; i < 16u; ++i) {
        require_u16(pixels[i], 0x1234u, "framebuffer clear");
    }
    el2d_framebuffer_set_pixel(&fb, 2, 1, 0xabcd);
    require_u16(pixels[6], 0xabcdu, "framebuffer set pixel");
}

static size_t count_color(const uint16_t *pixels, size_t count, uint16_t color) {
    size_t total = 0u;
    for (size_t i = 0; i < count; ++i) {
        if (pixels[i] == color) {
            total += 1u;
        }
    }
    return total;
}

static void test_preview_renderer_is_parameter_driven(void) {
    uint16_t idle_pixels[96u * 96u];
    uint16_t speaking_pixels[96u * 96u];
    el2d_framebuffer idle_fb;
    el2d_framebuffer speaking_fb;
    if (el2d_framebuffer_init(&idle_fb, idle_pixels, 96u, 96u) != EL2D_OK ||
        el2d_framebuffer_init(&speaking_fb, speaking_pixels, 96u, 96u) != EL2D_OK) {
        fprintf(stderr, "preview framebuffer init failed\n");
        exit(1);
    }

    el2d_preview_pose idle_pose = {
        .mouth_open = 0.1f,
        .eye_open = 0.95f,
        .gaze_x = 0.0f,
        .gaze_y = 0.0f,
        .body_lean = 0.0f,
    };
    el2d_preview_pose speaking_pose = idle_pose;
    speaking_pose.mouth_open = 0.9f;
    speaking_pose.body_lean = 0.35f;

    el2d_preview_render(&idle_fb, &idle_pose);
    el2d_preview_render(&speaking_fb, &speaking_pose);

    uint16_t mouth_color = el2d_preview_mouth_color();
    uint16_t hair_color = el2d_preview_hair_color();
    size_t idle_mouth = count_color(idle_pixels, 96u * 96u, mouth_color);
    size_t speaking_mouth = count_color(speaking_pixels, 96u * 96u, mouth_color);
    size_t idle_hair = count_color(idle_pixels, 96u * 96u, hair_color);
    if (speaking_mouth <= idle_mouth + 20u) {
        fprintf(stderr, "speaking mouth area did not grow: idle=%zu speaking=%zu\n", idle_mouth, speaking_mouth);
        exit(1);
    }
    if (idle_hair < 600u) {
        fprintf(stderr, "layered mesh mass missing: layer=%zu\n", idle_hair);
        exit(1);
    }
}

static void test_mesh_renderer_draws_textured_drawable(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "mesh framebuffer init failed\n");
        exit(1);
    }

    uint16_t texture_pixels[] = {0xf800u};
    uint8_t texture_alpha4[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = texture_alpha4,
        .width = 1u,
        .height = 1u,
    };
    float positions[] = {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        -1.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
    };
    uint16_t indices[] = {0u, 1u, 2u};
    el2d_mesh_drawable drawable = {
        .id = "triangle",
        .texture_index = 0u,
        .render_order = 0,
        .visible = 1u,
        .opacity = 255u,
        .vertex_count = 3u,
        .index_count = 3u,
        .positions = positions,
        .uvs = uvs,
        .indices = indices,
    };
    el2d_mesh_model model = {
        .textures = &texture,
        .texture_count = 1u,
        .drawables = &drawable,
        .drawable_count = 1u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };

    el2d_mesh_render_rgb565(&fb, &model, 0x001fu);

    size_t red = count_color(pixels, 16u * 16u, 0xf800u);
    size_t blue = count_color(pixels, 16u * 16u, 0x001fu);
    if (red < 20u) {
        fprintf(stderr, "mesh renderer did not draw textured triangle: red=%zu\n", red);
        exit(1);
    }
    if (blue == 0u) {
        fprintf(stderr, "mesh renderer overwrote the whole framebuffer unexpectedly\n");
        exit(1);
    }

    el2d_mesh_render_stats stats;
    el2d_mesh_get_last_render_stats(&stats);
    require_size(stats.triangle_count, 1u, "render triangle count");
    require_size(stats.transformed_vertex_count, 3u, "drawable vertices transformed once");
    if (stats.candidate_pixel_count == 0u || stats.covered_pixel_count == 0u || stats.texture_sample_count == 0u) {
        fprintf(stderr, "mesh renderer did not record pixel work\n");
        exit(1);
    }
    require_size(stats.candidate_pixel_count, stats.covered_pixel_count, "scanline skips bbox exterior pixels");
    require_size(
        stats.alpha_zero_sample_count + stats.opaque_sample_count + stats.blended_sample_count,
        stats.texture_sample_count,
        "texture sample classifications");
    require_size(stats.mask_build_count, 0u, "unmasked render has no mask build");
}

static void test_mesh_renderer_uses_cubism_uv_origin(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "mesh uv framebuffer init failed\n");
        exit(1);
    }

    uint16_t texture_pixels[] = {
        0xf800u, 0xf800u,
        0x07e0u, 0x07e0u,
    };
    uint8_t texture_alpha4[] = {0xffu, 0xffu};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = texture_alpha4,
        .width = 2u,
        .height = 2u,
    };
    float positions[] = {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f, 1.0f,
        -1.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    el2d_mesh_drawable drawable = {
        .id = "uv_quad",
        .texture_index = 0u,
        .render_order = 0,
        .visible = 1u,
        .opacity = 255u,
        .vertex_count = 4u,
        .index_count = 6u,
        .positions = positions,
        .uvs = uvs,
        .indices = indices,
    };
    el2d_mesh_model model = {
        .textures = &texture,
        .texture_count = 1u,
        .drawables = &drawable,
        .drawable_count = 1u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };

    el2d_mesh_render_rgb565(&fb, &model, 0x001fu);

    require_u16(pixels[4u * 16u + 8u], 0xf800u, "cubism uv top samples png top");
    require_u16(pixels[11u * 16u + 8u], 0x07e0u, "cubism uv bottom samples png bottom");
}

static void test_mesh_renderer_renders_order_range(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "mesh range framebuffer init failed\n");
        exit(1);
    }

    uint16_t red_pixel[] = {0xf800u};
    uint16_t green_pixel[] = {0x07e0u};
    uint8_t opaque_alpha[] = {0xffu};
    el2d_mesh_texture textures[] = {
        {.rgb565 = red_pixel, .alpha4 = opaque_alpha, .width = 1u, .height = 1u},
        {.rgb565 = green_pixel, .alpha4 = opaque_alpha, .width = 1u, .height = 1u},
    };
    float positions[] = {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f, 1.0f,
        -1.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    el2d_mesh_drawable drawables[] = {
        {
            .id = "range_red",
            .texture_index = 0u,
            .render_order = 0,
            .visible = 1u,
            .opacity = 255u,
            .vertex_count = 4u,
            .index_count = 6u,
            .positions = positions,
            .uvs = uvs,
            .indices = indices,
        },
        {
            .id = "range_green",
            .texture_index = 1u,
            .render_order = 1,
            .visible = 1u,
            .opacity = 255u,
            .vertex_count = 4u,
            .index_count = 6u,
            .positions = positions,
            .uvs = uvs,
            .indices = indices,
        },
    };
    el2d_mesh_model model = {
        .textures = textures,
        .texture_count = 2u,
        .drawables = drawables,
        .drawable_count = 2u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };

    el2d_mesh_render_rgb565_blended_range(&fb, &model, 0, 0, 0.0f, 0x001fu, 1, 1, 1u);

    if (count_color(pixels, 16u * 16u, 0xf800u) != 0u) {
        fprintf(stderr, "render range drew skipped lower render order\n");
        exit(1);
    }
    if (count_color(pixels, 16u * 16u, 0x07e0u) < 200u) {
        fprintf(stderr, "render range did not draw selected render order\n");
        exit(1);
    }
}

static void test_mesh_renderer_draws_sparse_high_render_order(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "mesh sparse-order framebuffer init failed\n");
        exit(1);
    }

    uint16_t red_pixel[] = {0xf800u};
    uint16_t green_pixel[] = {0x07e0u};
    uint8_t opaque_alpha[] = {0xffu};
    el2d_mesh_texture textures[] = {
        {.rgb565 = red_pixel, .alpha4 = opaque_alpha, .width = 1u, .height = 1u},
        {.rgb565 = green_pixel, .alpha4 = opaque_alpha, .width = 1u, .height = 1u},
    };
    float positions[] = {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f, 1.0f,
        -1.0f, 1.0f,
    };
    float uvs[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    el2d_mesh_drawable drawables[] = {
        {
            .id = "background",
            .texture_index = 0u,
            .render_order = 0,
            .visible = 1u,
            .opacity = 255u,
            .vertex_count = 4u,
            .index_count = 6u,
            .positions = positions,
            .uvs = uvs,
            .indices = indices,
        },
        {
            .id = "sparse_foreground",
            .texture_index = 1u,
            .render_order = 77,
            .visible = 1u,
            .opacity = 255u,
            .vertex_count = 4u,
            .index_count = 6u,
            .positions = positions,
            .uvs = uvs,
            .indices = indices,
        },
    };
    el2d_mesh_model model = {
        .textures = textures,
        .texture_count = 2u,
        .drawables = drawables,
        .drawable_count = 2u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };

    el2d_mesh_render_rgb565(&fb, &model, 0x001fu);

    if (count_color(pixels, 16u * 16u, 0x07e0u) < 200u) {
        fprintf(stderr, "mesh renderer skipped sparse high render order\n");
        exit(1);
    }
}

static void test_mesh_rasterizer_pretransforms_interpolated_vertices(void) {
    float source_positions[] = {-1.0f, -1.0f};
    float target_positions[] = {1.0f, 1.0f};
    float uvs[] = {0.25f, 0.75f};
    el2d_raster_vertex vertex = {0};
    el2d_raster_transform transform = {
        .source_positions = source_positions,
        .target_positions = target_positions,
        .uvs = uvs,
        .vertex_count = 1u,
        .progress = 0.5f,
        .model_cx = 0.0f,
        .model_cy = 0.0f,
        .scale = 4.0f,
        .framebuffer_width = 16u,
        .framebuffer_height = 16u,
    };

    if (!el2d_raster_transform_vertices(&transform, &vertex, 1u)) {
        fprintf(stderr, "raster vertex transform failed\n");
        exit(1);
    }
    require_size((size_t)vertex.x16, (size_t)(8 << 16), "interpolated screen x16");
    require_size((size_t)vertex.y16, (size_t)(8 << 16), "interpolated screen y16");
    require_size((size_t)vertex.u16, 16384u, "fixed uv u16");
    require_size((size_t)vertex.v16, 49152u, "fixed uv v16");
}

static void test_fixed_scanline_fills_adjacent_triangles_once(void) {
    uint16_t pixels[8u * 8u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 8u, 8u) != EL2D_OK) {
        fprintf(stderr, "scanline framebuffer init failed\n");
        exit(1);
    }
    el2d_framebuffer_clear(&fb, 0x001fu);
    uint16_t texture_pixel[] = {0xf800u};
    uint8_t texture_alpha[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixel,
        .alpha4 = texture_alpha,
        .width = 1u,
        .height = 1u,
    };
    el2d_raster_vertex vertices[] = {
        {.x16 = 2 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 2 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
    };
    el2d_mesh_render_stats stats = {0};
    el2d_raster_draw_target target = {
        .framebuffer = &fb,
        .texture = &texture,
        .opacity = 255u,
        .mask_coverage = 0,
        .stats = &stats,
    };

    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[1], &vertices[2]);
    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[2], &vertices[3]);

    require_size(count_color(pixels, 8u * 8u, 0xf800u), 16u, "adjacent scanline quad coverage");
    require_size(stats.covered_pixel_count, 16u, "adjacent pixels written once");
    require_size(stats.candidate_pixel_count, 16u, "scanline visits covered pixels only");
}

static void test_fixed_scanline_uses_preclassified_triangle_alpha(void) {
    uint16_t pixels[8u * 8u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 8u, 8u) != EL2D_OK) {
        fprintf(stderr, "preclassified alpha framebuffer init failed\n");
        exit(1);
    }
    const uint16_t texture_pixels[] = {0xf800u};
    uint8_t alpha4[] = {0x00u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = alpha4,
        .width = 1u,
        .height = 1u,
    };
    el2d_raster_vertex vertices[] = {
        {.x16 = 2 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
    };
    el2d_raster_draw_target target = {
        .framebuffer = &fb,
        .texture = &texture,
        .opacity = 255u,
        .triangle_alpha_mode = EL2D_TRIANGLE_ALPHA_OPAQUE,
    };

    el2d_framebuffer_clear(&fb, 0x001fu);
    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[1], &vertices[2]);
    if (count_color(pixels, 8u * 8u, 0xf800u) == 0u) {
        fprintf(stderr, "opaque triangle still sampled transparent alpha\n");
        exit(1);
    }

    alpha4[0] = 0xffu;
    target.triangle_alpha_mode = EL2D_TRIANGLE_ALPHA_TRANSPARENT;
    el2d_framebuffer_clear(&fb, 0x001fu);
    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[1], &vertices[2]);
    require_size(count_color(pixels, 8u * 8u, 0xf800u), 0u, "transparent triangle skipped");
}

static void test_fixed_scanline_divisions_are_constant_per_triangle(void) {
    uint16_t pixels[64u * 216u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 64u, 216u) != EL2D_OK) {
        fprintf(stderr, "tall scanline framebuffer init failed\n");
        exit(1);
    }
    el2d_framebuffer_clear(&fb, 0x001fu);
    uint16_t texture_pixel[] = {0xf800u};
    uint8_t texture_alpha[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixel,
        .alpha4 = texture_alpha,
        .width = 1u,
        .height = 1u,
    };
    el2d_raster_vertex vertices[] = {
        {.x16 = 8 << 16, .y16 = 8 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 56 << 16, .y16 = 16 << 16, .u16 = 1 << 16, .v16 = 0},
        {.x16 = 24 << 16, .y16 = 200 << 16, .u16 = 0, .v16 = 1 << 16},
    };
    el2d_mesh_render_stats stats = {0};
    el2d_raster_draw_target target = {
        .framebuffer = &fb,
        .texture = &texture,
        .opacity = 255u,
        .mask_coverage = 0,
        .stats = &stats,
    };

    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[1], &vertices[2]);

    if (stats.scanline_count < 180u) {
        fprintf(stderr, "tall triangle did not exercise enough scanlines: %u\n", stats.scanline_count);
        exit(1);
    }
    if (stats.division_count > 7u) {
        fprintf(stderr, "triangle divisions grew with scanlines: %u\n", stats.division_count);
        exit(1);
    }
}

static void test_fixed_scanline_rasterizes_alpha_mask_coverage(void) {
    require_size(el2d_raster_coverage_bytes(8u, 8u), 8u, "bit-packed mask coverage bytes");
    uint8_t coverage[8u] = {0};
    uint16_t texture_pixel[] = {0xffffu};
    uint8_t texture_alpha[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixel,
        .alpha4 = texture_alpha,
        .width = 1u,
        .height = 1u,
    };
    el2d_raster_vertex vertices[] = {
        {.x16 = 2 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 2 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
    };

    el2d_raster_draw_mask_triangle(coverage, 8u, 8u, &texture, &vertices[0], &vertices[1], &vertices[2]);
    el2d_raster_draw_mask_triangle(coverage, 8u, 8u, &texture, &vertices[0], &vertices[2], &vertices[3]);

    size_t covered = 0u;
    for (size_t index = 0u; index < 8u * 8u; ++index) {
        if ((coverage[index >> 3u] & (uint8_t)(1u << (index & 7u))) != 0u) ++covered;
    }
    require_size(covered, 16u, "fixed scanline mask coverage");
}

static void test_mask_scanline_uses_preclassified_triangle_alpha(void) {
    uint8_t coverage[8u] = {0};
    const uint16_t texture_pixels[] = {0xf800u};
    uint8_t alpha4[] = {0x00u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = alpha4,
        .width = 1u,
        .height = 1u,
    };
    el2d_raster_vertex vertices[] = {
        {.x16 = 2 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 2 << 16, .u16 = 0, .v16 = 0},
        {.x16 = 6 << 16, .y16 = 6 << 16, .u16 = 0, .v16 = 0},
    };

    el2d_raster_draw_mask_triangle_clipped_alpha_mode(
        coverage, 8u, 8u, 0u, 8u, &texture,
        EL2D_TRIANGLE_ALPHA_OPAQUE,
        &vertices[0], &vertices[1], &vertices[2]);
    size_t set_bytes = 0u;
    for (size_t i = 0u; i < sizeof(coverage); ++i) set_bytes += coverage[i] != 0u;
    if (set_bytes == 0u) {
        fprintf(stderr, "opaque mask triangle still sampled transparent alpha\n");
        exit(1);
    }

    memset(coverage, 0, sizeof(coverage));
    uint16_t unused_pixel = 0u;
    el2d_framebuffer mask_fb = {.pixels = &unused_pixel, .width = 8u, .height = 8u};
    el2d_mesh_render_stats stats = {0};
    el2d_raster_draw_target target = {
        .framebuffer = &mask_fb,
        .texture = &texture,
        .opacity = 255u,
        .coverage_output = coverage,
        .triangle_alpha_mode = EL2D_TRIANGLE_ALPHA_OPAQUE,
        .stats = &stats,
    };
    el2d_raster_draw_triangle(&target, &vertices[0], &vertices[1], &vertices[2]);
    if (stats.division_count > 3u) {
        fprintf(stderr, "opaque mask triangle still computed UV divisions: %u\n", stats.division_count);
        exit(1);
    }

    memset(coverage, 0, sizeof(coverage));
    alpha4[0] = 0xffu;
    el2d_raster_draw_mask_triangle_clipped_alpha_mode(
        coverage, 8u, 8u, 0u, 8u, &texture,
        EL2D_TRIANGLE_ALPHA_TRANSPARENT,
        &vertices[0], &vertices[1], &vertices[2]);
    set_bytes = 0u;
    for (size_t i = 0u; i < sizeof(coverage); ++i) set_bytes += coverage[i] != 0u;
    require_size(set_bytes, 0u, "transparent mask triangle skipped");
}

static void test_independent_context_clips_compose_exact_full_frame(void) {
    uint16_t full_pixels[16u * 16u];
    uint16_t split_pixels[16u * 16u];
    el2d_framebuffer full_fb;
    el2d_framebuffer split_fb;
    if (el2d_framebuffer_init(&full_fb, full_pixels, 16u, 16u) != EL2D_OK ||
        el2d_framebuffer_init(&split_fb, split_pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "clipped context framebuffer init failed\n");
        exit(1);
    }
    uint16_t texture_pixels[] = {0x07e0u};
    uint8_t texture_alpha[] = {0xf0u};
    float positions[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
    float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    el2d_mesh_texture texture = {.rgb565 = texture_pixels, .alpha4 = texture_alpha, .width = 1u, .height = 1u};
    el2d_mesh_drawable drawable = {
        .id = "clip-quad", .texture_index = 0u, .render_order = 0, .visible = 1u, .opacity = 255u,
        .vertex_count = 4u, .index_count = 6u, .positions = positions, .uvs = uvs, .indices = indices,
    };
    el2d_mesh_model model = {
        .textures = &texture, .texture_count = 1u, .drawables = &drawable, .drawable_count = 1u,
        .min_x = -1.0f, .max_x = 1.0f, .min_y = -1.0f, .max_y = 1.0f,
    };
    el2d_mesh_render_context *top = el2d_mesh_render_context_create();
    el2d_mesh_render_context *bottom = el2d_mesh_render_context_create();
    if (top == 0 || bottom == 0) {
        fprintf(stderr, "clipped render context allocation failed\n");
        exit(1);
    }

    el2d_mesh_render_rgb565(&full_fb, &model, 0xffffu);
    el2d_framebuffer_clear(&split_fb, 0xffffu);
    el2d_mesh_render_context_rgb565_blended_clipped(top, &split_fb, &model, 0, 0, 0.0f, 0xffffu, 0u, 8u, 0u);
    el2d_mesh_render_context_rgb565_blended_clipped(bottom, &split_fb, &model, 0, 0, 0.0f, 0xffffu, 8u, 16u, 0u);

    if (memcmp(full_pixels, split_pixels, sizeof(full_pixels)) != 0) {
        fprintf(stderr, "independent clipped contexts did not compose the full frame\n");
        exit(1);
    }
    el2d_mesh_render_context_destroy(top);
    el2d_mesh_render_context_destroy(bottom);
}

static void test_mesh_renderer_reuses_shared_mask_set(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "shared mask framebuffer init failed\n");
        exit(1);
    }
    uint16_t texture_pixels[] = {0x07e0u};
    uint8_t texture_alpha[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = texture_alpha,
        .width = 1u,
        .height = 1u,
    };
    float mask_positions[] = {
        -1.0f, -1.0f,
         0.0f, -1.0f,
         0.0f,  1.0f,
        -1.0f,  1.0f,
    };
    float color_positions[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    uint16_t masks[] = {0u};
    el2d_mesh_drawable drawables[5] = {0};
    drawables[0] = (el2d_mesh_drawable){
        .id = "shared_mask",
        .texture_index = 0u,
        .render_order = 0,
        .visible = 0u,
        .opacity = 0u,
        .vertex_count = 4u,
        .index_count = 6u,
        .positions = mask_positions,
        .uvs = uvs,
        .indices = indices,
    };
    for (size_t index = 1u; index < 5u; ++index) {
        drawables[index] = (el2d_mesh_drawable){
            .id = "masked_color",
            .texture_index = 0u,
            .render_order = (int16_t)index,
            .visible = 1u,
            .opacity = 255u,
            .vertex_count = 4u,
            .index_count = 6u,
            .positions = color_positions,
            .uvs = uvs,
            .indices = indices,
            .mask_count = 1u,
            .masks = masks,
        };
    }
    el2d_mesh_model model = {
        .textures = &texture,
        .texture_count = 1u,
        .drawables = drawables,
        .drawable_count = 5u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };

    el2d_mesh_render_rgb565(&fb, &model, 0x001fu);
    el2d_mesh_render_stats stats = {0};
    el2d_mesh_get_last_render_stats(&stats);
    require_size(stats.mask_build_count, 1u, "shared mask built once");
    require_size(stats.mask_reuse_count, 3u, "shared mask reused three times");
    if (count_color(pixels, 16u * 16u, 0x07e0u) == 0u) {
        fprintf(stderr, "shared mask removed all color pixels\n");
        exit(1);
    }
}

static void test_mesh_renderer_interpolates_mask_geometry(void) {
    uint16_t pixels[16u * 16u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "transition mask framebuffer init failed\n");
        exit(1);
    }
    uint16_t texture_pixels[] = {0x07e0u};
    uint8_t texture_alpha[] = {0xf0u};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixels,
        .alpha4 = texture_alpha,
        .width = 1u,
        .height = 1u,
    };
    float source_mask_positions[] = {-1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, -1.0f, 1.0f};
    float target_mask_positions[] = { 0.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,  0.0f, 1.0f};
    float color_positions[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
    float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    uint16_t masks[] = {0u};
    el2d_mesh_drawable source_drawables[] = {
        {.id = "moving_mask", .texture_index = 0u, .render_order = 0, .visible = 0u, .opacity = 0u, .vertex_count = 4u, .index_count = 6u, .positions = source_mask_positions, .uvs = uvs, .indices = indices},
        {.id = "masked_color", .texture_index = 0u, .render_order = 1, .visible = 1u, .opacity = 255u, .vertex_count = 4u, .index_count = 6u, .positions = color_positions, .uvs = uvs, .indices = indices, .mask_count = 1u, .masks = masks},
    };
    el2d_mesh_drawable target_drawables[] = {
        {.id = "moving_mask", .texture_index = 0u, .render_order = 0, .visible = 0u, .opacity = 0u, .vertex_count = 4u, .index_count = 6u, .positions = target_mask_positions, .uvs = uvs, .indices = indices},
        {.id = "masked_color", .texture_index = 0u, .render_order = 1, .visible = 1u, .opacity = 255u, .vertex_count = 4u, .index_count = 6u, .positions = color_positions, .uvs = uvs, .indices = indices, .mask_count = 1u, .masks = masks},
    };
    el2d_mesh_model source = {.textures = &texture, .texture_count = 1u, .drawables = source_drawables, .drawable_count = 2u, .min_x = -1.0f, .max_x = 1.0f, .min_y = -1.0f, .max_y = 1.0f};
    el2d_mesh_model target = {.textures = &texture, .texture_count = 1u, .drawables = target_drawables, .drawable_count = 2u, .min_x = -1.0f, .max_x = 1.0f, .min_y = -1.0f, .max_y = 1.0f};

    el2d_mesh_render_rgb565_blended(&fb, &source, &target, 0, 0.5f, 0x001fu);
    require_u16(pixels[8u * 16u + 8u], 0x07e0u, "transitioned mask covers midpoint");
    require_u16(pixels[8u * 16u + 2u], 0x001fu, "transitioned mask leaves source edge");
}

static void test_render_context_uses_texture_overrides(void) {
    uint16_t pixels[8u * 8u];
    el2d_framebuffer fb;
    if (el2d_framebuffer_init(&fb, pixels, 8u, 8u) != EL2D_OK) {
        fprintf(stderr, "texture override framebuffer init failed\n");
        exit(1);
    }
    uint16_t red[] = {0xf800u};
    uint16_t green[] = {0x07e0u};
    uint8_t alpha[] = {0xf0u};
    el2d_mesh_texture source_texture = {.rgb565 = red, .alpha4 = alpha, .width = 1u, .height = 1u};
    el2d_mesh_texture override_texture = {.rgb565 = green, .alpha4 = alpha, .width = 1u, .height = 1u};
    float positions[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
    float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    uint16_t indices[] = {0u, 1u, 2u, 0u, 2u, 3u};
    el2d_mesh_drawable drawable = {
        .id = "texture_override",
        .texture_index = 0u,
        .render_order = 0,
        .visible = 1u,
        .opacity = 255u,
        .vertex_count = 4u,
        .index_count = 6u,
        .positions = positions,
        .uvs = uvs,
        .indices = indices,
    };
    el2d_mesh_model model = {
        .textures = &source_texture,
        .texture_count = 1u,
        .drawables = &drawable,
        .drawable_count = 1u,
        .min_x = -1.0f,
        .max_x = 1.0f,
        .min_y = -1.0f,
        .max_y = 1.0f,
    };
    el2d_mesh_render_context *context = el2d_mesh_render_context_create();
    if (context == NULL) {
        fprintf(stderr, "texture override context allocation failed\n");
        exit(1);
    }
    el2d_mesh_render_context_set_texture_overrides(context, &override_texture, 1u);
    el2d_mesh_render_context_rgb565_blended_clipped(
        context, &fb, &model, NULL, NULL, 0.0f, 0x001fu, 0u, fb.height, 1u);
    require_u16(pixels[4u * 8u + 4u], 0x07e0u, "texture override center pixel");
    el2d_mesh_render_context_destroy(context);
}

static void test_mesh_renderer_adds_expression_delta_over_state_morph(void) {
    uint16_t layered_pixels[16u * 16u];
    uint16_t expected_pixels[16u * 16u];
    el2d_framebuffer layered_fb;
    el2d_framebuffer expected_fb;
    if (el2d_framebuffer_init(&layered_fb, layered_pixels, 16u, 16u) != EL2D_OK ||
        el2d_framebuffer_init(&expected_fb, expected_pixels, 16u, 16u) != EL2D_OK) {
        fprintf(stderr, "expression framebuffer init failed\n");
        exit(1);
    }
    uint16_t texture_pixel[] = {0xf800u};
    uint8_t alpha4[] = {0xffu};
    el2d_mesh_texture texture = {
        .rgb565 = texture_pixel,
        .alpha4 = alpha4,
        .width = 1u,
        .height = 1u,
    };
    float from_positions[] = {-0.9f, -0.8f, -0.3f, -0.8f, -0.9f, -0.2f};
    float to_positions[] = {-0.7f, -0.8f, -0.1f, -0.8f, -0.7f, -0.2f};
    float expression_positions[] = {-0.5f, -0.8f, 0.1f, -0.8f, -0.5f, -0.2f};
    float expected_positions[] = {-0.6f, -0.8f, 0.0f, -0.8f, -0.6f, -0.2f};
    float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    uint16_t indices[] = {0u, 1u, 2u};
#define EXPRESSION_DRAWABLE(POSITIONS, ID) { \
        .id = ID, .texture_index = 0u, .render_order = 0, .visible = 1u, \
        .opacity = 255u, .vertex_count = 3u, .index_count = 3u, \
        .positions = POSITIONS, .uvs = uvs, .indices = indices }
    el2d_mesh_drawable from_drawable = EXPRESSION_DRAWABLE(from_positions, "face");
    el2d_mesh_drawable to_drawable = EXPRESSION_DRAWABLE(to_positions, "face");
    el2d_mesh_drawable expression_drawable = EXPRESSION_DRAWABLE(expression_positions, "face");
    el2d_mesh_drawable expected_drawable = EXPRESSION_DRAWABLE(expected_positions, "face");
    el2d_mesh_drawable incompatible_drawable = EXPRESSION_DRAWABLE(expression_positions, "other");
#undef EXPRESSION_DRAWABLE
#define EXPRESSION_MODEL(DRAWABLE) { \
        .textures = &texture, .texture_count = 1u, .drawables = &DRAWABLE, \
        .drawable_count = 1u, .min_x = -1.0f, .max_x = 1.0f, .min_y = -1.0f, .max_y = 1.0f }
    el2d_mesh_model from_model = EXPRESSION_MODEL(from_drawable);
    el2d_mesh_model to_model = EXPRESSION_MODEL(to_drawable);
    el2d_mesh_model expression_model = EXPRESSION_MODEL(expression_drawable);
    el2d_mesh_model expected_model = EXPRESSION_MODEL(expected_drawable);
    el2d_mesh_model incompatible_model = EXPRESSION_MODEL(incompatible_drawable);
#undef EXPRESSION_MODEL
    el2d_mesh_expression_layer layer = {
        .reference_model = &from_model,
        .target_model = &expression_model,
        .weight = 0.5f,
    };
    el2d_mesh_render_context *context = el2d_mesh_render_context_create();
    if (context == 0) {
        fprintf(stderr, "expression render context allocation failed\n");
        exit(1);
    }

    el2d_mesh_render_context_rgb565_blended_expressions_clipped(
        context, &layered_fb, &from_model, &to_model, 0, 0.5f,
        &layer, 1u, 0x001fu, 0u, 16u, 1u);
    el2d_mesh_render_rgb565(&expected_fb, &expected_model, 0x001fu);
    if (memcmp(layered_pixels, expected_pixels, sizeof(layered_pixels)) != 0) {
        fprintf(stderr, "expression delta did not compose over state morph\n");
        exit(1);
    }

    layer.weight = 0.0f;
    el2d_mesh_render_context_rgb565_blended_expressions_clipped(
        context, &layered_fb, &from_model, &to_model, 0, 0.5f,
        &layer, 1u, 0x001fu, 0u, 16u, 1u);
    el2d_mesh_render_rgb565_blended(&expected_fb, &from_model, &to_model, 0, 0.5f, 0x001fu);
    if (memcmp(layered_pixels, expected_pixels, sizeof(layered_pixels)) != 0) {
        fprintf(stderr, "zero expression weight changed state morph\n");
        exit(1);
    }

    el2d_mesh_render_context_rgb565_blended_clipped(
        context, &layered_fb, &from_model, &incompatible_model, 0, 0.5f,
        0x001fu, 0u, 16u, 1u);
    el2d_mesh_render_rgb565(&expected_fb, &from_model, 0x001fu);
    if (memcmp(layered_pixels, expected_pixels, sizeof(layered_pixels)) != 0) {
        fprintf(stderr, "mismatched state drawable id used index fallback\n");
        exit(1);
    }
    el2d_mesh_render_rgb565_blended(&expected_fb, &from_model, &to_model, 0, 0.5f, 0x001fu);

    layer.target_model = &incompatible_model;
    layer.weight = 1.0f;
    el2d_mesh_render_context_rgb565_blended_expressions_clipped(
        context, &layered_fb, &from_model, &to_model, 0, 0.5f,
        &layer, 1u, 0x001fu, 0u, 16u, 1u);
    if (memcmp(layered_pixels, expected_pixels, sizeof(layered_pixels)) != 0) {
        fprintf(stderr, "incompatible expression drawable changed state morph\n");
        exit(1);
    }
    el2d_mesh_render_context_destroy(context);
}

int main(void) {
    test_easing_curves();
    test_model_parameters_and_transition();
    test_zero_duration_transition_is_immediate();
    test_clip_curve_application();
    test_framebuffer_rgb565_and_clear();
    test_preview_renderer_is_parameter_driven();
    test_mesh_renderer_draws_textured_drawable();
    test_mesh_renderer_uses_cubism_uv_origin();
    test_mesh_renderer_renders_order_range();
    test_mesh_renderer_draws_sparse_high_render_order();
    test_mesh_rasterizer_pretransforms_interpolated_vertices();
    test_fixed_scanline_fills_adjacent_triangles_once();
    test_fixed_scanline_uses_preclassified_triangle_alpha();
    test_fixed_scanline_divisions_are_constant_per_triangle();
    test_fixed_scanline_rasterizes_alpha_mask_coverage();
    test_mask_scanline_uses_preclassified_triangle_alpha();
    test_independent_context_clips_compose_exact_full_frame();
    test_mesh_renderer_reuses_shared_mask_set();
    test_mesh_renderer_interpolates_mask_geometry();
    test_render_context_uses_texture_overrides();
    test_mesh_renderer_adds_expression_delta_over_state_morph();
    puts("el2d runtime tests passed");
    return 0;
}
