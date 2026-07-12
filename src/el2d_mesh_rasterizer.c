#include "el2d_mesh_rasterizer.h"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

#define EL2D_FIXED_ONE 65536.0f
#define EL2D_FIXED_HALF 32768
#define EL2D_FIXED_SCALE 65536

#ifndef EL2D_RENDER_DETAILED_STATS
#define EL2D_RENDER_DETAILED_STATS 1
#endif

static float el2d_raster_clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static int32_t el2d_raster_float_to_fixed(float value) {
    float scaled = value * EL2D_FIXED_ONE;
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

int el2d_raster_transform_vertices(
    const el2d_raster_transform *transform,
    el2d_raster_vertex *output,
    size_t output_capacity
) {
    if (transform == 0 || output == 0 ||
        transform->source_positions == 0 || transform->uvs == 0 ||
        transform->vertex_count == 0u || output_capacity < transform->vertex_count ||
        transform->framebuffer_width == 0u || transform->framebuffer_height == 0u) {
        return 0;
    }
    float progress = el2d_raster_clamp01(transform->progress);
    for (size_t index = 0u; index < transform->vertex_count; ++index) {
        size_t position_index = index * 2u;
        float x = transform->source_positions[position_index];
        float y = transform->source_positions[position_index + 1u];
        if (transform->target_positions != 0) {
            x += (transform->target_positions[position_index] - x) * progress;
            y += (transform->target_positions[position_index + 1u] - y) * progress;
        }
        float screen_x = ((x - transform->model_cx) * transform->scale) +
            ((float)transform->framebuffer_width * 0.5f);
        float screen_y = ((transform->model_cy - y) * transform->scale) +
            ((float)transform->framebuffer_height * 0.5f);
        output[index].x16 = el2d_raster_float_to_fixed(screen_x);
        output[index].y16 = el2d_raster_float_to_fixed(screen_y);
        output[index].u16 = el2d_raster_float_to_fixed(transform->uvs[position_index]);
        output[index].v16 = el2d_raster_float_to_fixed(transform->uvs[position_index + 1u]);
    }
    return 1;
}

typedef struct el2d_raster_edge {
    int y_start;
    int y_end;
    int32_t x_start16;
    int32_t dx16;
    int valid;
} el2d_raster_edge;

static int el2d_raster_ceil_pixel_center(int32_t value16) {
    int64_t numerator = (int64_t)value16 - EL2D_FIXED_HALF;
    if (numerator >= 0) {
        return (int)((numerator + EL2D_FIXED_SCALE - 1) / EL2D_FIXED_SCALE);
    }
    return (int)(numerator / EL2D_FIXED_SCALE);
}

static int el2d_raster_clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

size_t el2d_raster_coverage_bytes(uint16_t width, uint16_t height) {
    size_t pixel_count = (size_t)width * height;
    return (pixel_count + 7u) / 8u;
}

static int el2d_raster_coverage_test(const uint8_t *coverage, size_t pixel_index) {
    return coverage != 0 &&
        (coverage[pixel_index >> 3u] & (uint8_t)(1u << (pixel_index & 7u))) != 0u;
}

static uint16_t el2d_raster_blend_rgb565(uint16_t dst, uint16_t src, uint8_t alpha) {
    if (alpha == 0u) return dst;
    if (alpha >= 255u) return src;
    uint8_t inv = (uint8_t)(255u - alpha);
    uint8_t sr5 = (uint8_t)((src >> 11u) & 0x1fu);
    uint8_t sg6 = (uint8_t)((src >> 5u) & 0x3fu);
    uint8_t sb5 = (uint8_t)(src & 0x1fu);
    uint8_t dr5 = (uint8_t)((dst >> 11u) & 0x1fu);
    uint8_t dg6 = (uint8_t)((dst >> 5u) & 0x3fu);
    uint8_t db5 = (uint8_t)(dst & 0x1fu);
    uint8_t r5 = (uint8_t)(((uint16_t)sr5 * alpha + (uint16_t)dr5 * inv + 127u) / 255u);
    uint8_t g6 = (uint8_t)(((uint16_t)sg6 * alpha + (uint16_t)dg6 * inv + 127u) / 255u);
    uint8_t b5 = (uint8_t)(((uint16_t)sb5 * alpha + (uint16_t)db5 * inv + 127u) / 255u);
    return (uint16_t)((r5 << 11u) | (g6 << 5u) | b5);
}

static void el2d_raster_sample_and_blend(
    const el2d_raster_draw_target *target,
    size_t framebuffer_index,
    int32_t u16,
    int32_t v16
) {
    const el2d_mesh_texture *texture = target->texture;
    if (target->coverage_output != 0 &&
        target->triangle_alpha_mode == EL2D_TRIANGLE_ALPHA_OPAQUE) {
        target->coverage_output[framebuffer_index >> 3u] |=
            (uint8_t)(1u << (framebuffer_index & 7u));
        return;
    }
    int32_t max_u16 = ((int32_t)texture->width - 1) * EL2D_FIXED_SCALE;
    int32_t max_v16 = ((int32_t)texture->height - 1) * EL2D_FIXED_SCALE;
    int32_t clamped_u16 = el2d_raster_clamp_int(u16, 0, max_u16);
    int32_t clamped_v16 = el2d_raster_clamp_int(v16, 0, max_v16);
    int tx = (int)((clamped_u16 + EL2D_FIXED_HALF) >> 16);
    int ty = (int)((clamped_v16 + EL2D_FIXED_HALF) >> 16);
#if EL2D_RENDER_DETAILED_STATS
    if (target->stats != 0) ++target->stats->texture_sample_count;
#endif
    size_t texture_index = (size_t)ty * texture->width + (size_t)tx;
    uint8_t alpha4 = 15u;
    if (target->triangle_alpha_mode != EL2D_TRIANGLE_ALPHA_OPAQUE) {
        uint8_t alpha_pair = texture->alpha4 != 0
            ? texture->alpha4[texture_index >> 1u]
            : 0xffu;
        alpha4 = (texture_index & 1u) == 0u
            ? (uint8_t)(alpha_pair >> 4u)
            : (uint8_t)(alpha_pair & 0x0fu);
    }
    if (alpha4 == 0u) {
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->alpha_zero_sample_count;
#endif
        return;
    }
    if (target->coverage_output != 0) {
        target->coverage_output[framebuffer_index >> 3u] |=
            (uint8_t)(1u << (framebuffer_index & 7u));
        return;
    }
    uint16_t source_color = texture->rgb565[texture_index];
    if (alpha4 == 15u && target->opacity == 255u) {
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->opaque_sample_count;
#endif
        target->framebuffer->pixels[framebuffer_index] = source_color;
        return;
    }
#if EL2D_RENDER_DETAILED_STATS
    if (target->stats != 0) ++target->stats->blended_sample_count;
#endif
    uint8_t alpha = target->opacity == 255u
        ? (uint8_t)(alpha4 * 17u)
        : (uint8_t)(((uint16_t)alpha4 * 17u * target->opacity) / 255u);
    target->framebuffer->pixels[framebuffer_index] = el2d_raster_blend_rgb565(
        target->framebuffer->pixels[framebuffer_index],
        source_color,
        alpha);
}

static el2d_raster_edge el2d_raster_setup_edge(
    const el2d_raster_vertex *a,
    const el2d_raster_vertex *b,
    int height,
    el2d_mesh_render_stats *stats
) {
    el2d_raster_edge edge = {0};
    const el2d_raster_vertex *low = a;
    const el2d_raster_vertex *high = b;
    if (low->y16 > high->y16) {
        low = b;
        high = a;
    }
    if (low->y16 == high->y16) return edge;
    edge.y_start = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(low->y16), 0, height);
    edge.y_end = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(high->y16), 0, height);
    if (edge.y_start >= edge.y_end) return edge;
    int64_t denominator = (int64_t)high->y16 - low->y16;
    edge.dx16 = (int32_t)(((int64_t)(high->x16 - low->x16) * EL2D_FIXED_SCALE) / denominator);
    int64_t first_y16 = (int64_t)edge.y_start * EL2D_FIXED_SCALE + EL2D_FIXED_HALF;
    edge.x_start16 = low->x16 + (int32_t)(((int64_t)edge.dx16 * (first_y16 - low->y16)) >> 16);
    edge.valid = 1;
#if EL2D_RENDER_DETAILED_STATS
    if (stats != 0) ++stats->division_count;
#endif
    return edge;
}

void IRAM_ATTR el2d_raster_draw_triangle(
    const el2d_raster_draw_target *target,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2
) {
    if (target == 0 || target->framebuffer == 0 || target->framebuffer->pixels == 0 ||
        target->texture == 0 || target->texture->rgb565 == 0 ||
        target->texture->width == 0u || target->texture->height == 0u ||
        target->opacity == 0u || v0 == 0 || v1 == 0 || v2 == 0) {
        return;
    }
#if EL2D_RENDER_DETAILED_STATS
    if (target->stats != 0) ++target->stats->triangle_count;
#endif
    if (target->triangle_alpha_mode == EL2D_TRIANGLE_ALPHA_TRANSPARENT) {
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->rejected_triangle_count;
#endif
        return;
    }
    int64_t area = (int64_t)(v1->x16 - v0->x16) * (v2->y16 - v0->y16) -
        (int64_t)(v1->y16 - v0->y16) * (v2->x16 - v0->x16);
    if (area == 0) {
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->rejected_triangle_count;
#endif
        return;
    }
    int32_t min_x16 = v0->x16 < v1->x16 ? v0->x16 : v1->x16;
    if (v2->x16 < min_x16) min_x16 = v2->x16;
    int32_t max_x16 = v0->x16 > v1->x16 ? v0->x16 : v1->x16;
    if (v2->x16 > max_x16) max_x16 = v2->x16;
    int32_t min_y16 = v0->y16 < v1->y16 ? v0->y16 : v1->y16;
    if (v2->y16 < min_y16) min_y16 = v2->y16;
    int32_t max_y16 = v0->y16 > v1->y16 ? v0->y16 : v1->y16;
    if (v2->y16 > max_y16) max_y16 = v2->y16;
    int width = target->framebuffer->width;
    int height = target->framebuffer->height;
    if (max_x16 <= 0 || min_x16 >= width * EL2D_FIXED_SCALE ||
        max_y16 <= 0 || min_y16 >= height * EL2D_FIXED_SCALE) {
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->rejected_triangle_count;
#endif
        return;
    }
    int clip_y_min = 0;
    int clip_y_max = height;
    if (target->clip_y_max > target->clip_y_min) {
        clip_y_min = el2d_raster_clamp_int(target->clip_y_min, 0, height);
        clip_y_max = el2d_raster_clamp_int(target->clip_y_max, clip_y_min, height);
    }
    int y_start = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(min_y16), clip_y_min, clip_y_max);
    int y_end = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(max_y16), clip_y_min, clip_y_max);
    int64_t dx1 = (int64_t)v1->x16 - v0->x16;
    int64_t dy1 = (int64_t)v1->y16 - v0->y16;
    int64_t dx2 = (int64_t)v2->x16 - v0->x16;
    int64_t dy2 = (int64_t)v2->y16 - v0->y16;
    int64_t du1 = (int64_t)v1->u16 - v0->u16;
    int64_t dv1 = (int64_t)v1->v16 - v0->v16;
    int64_t du2 = (int64_t)v2->u16 - v0->u16;
    int64_t dv2 = (int64_t)v2->v16 - v0->v16;
    int opaque_coverage = target->coverage_output != 0 &&
        target->triangle_alpha_mode == EL2D_TRIANGLE_ALPHA_OPAQUE;
    int32_t du_dx16 = 0;
    int32_t du_dy16 = 0;
    int32_t dv_dx16 = 0;
    int32_t dv_dy16 = 0;
    if (!opaque_coverage) {
        du_dx16 = (int32_t)(((du1 * dy2 - du2 * dy1) * EL2D_FIXED_SCALE) / area);
        du_dy16 = (int32_t)(((dx1 * du2 - dx2 * du1) * EL2D_FIXED_SCALE) / area);
        dv_dx16 = (int32_t)(((dv1 * dy2 - dv2 * dy1) * EL2D_FIXED_SCALE) / area);
        dv_dy16 = (int32_t)(((dx1 * dv2 - dx2 * dv1) * EL2D_FIXED_SCALE) / area);
    }
    int32_t texture_width_scale = (int32_t)target->texture->width - 1;
    int32_t texture_height_scale = (int32_t)target->texture->height - 1;
    int32_t texture_u0_16 = v0->u16 * texture_width_scale;
    int32_t texture_v0_16 = (EL2D_FIXED_SCALE - v0->v16) * texture_height_scale;
    int32_t texture_du_dx16 = du_dx16 * texture_width_scale;
    int32_t texture_du_dy16 = du_dy16 * texture_width_scale;
    int32_t texture_dv_dx16 = -dv_dx16 * texture_height_scale;
    int32_t texture_dv_dy16 = -dv_dy16 * texture_height_scale;
#if EL2D_RENDER_DETAILED_STATS
    if (target->stats != 0 && !opaque_coverage) target->stats->division_count += 4u;
#endif
    el2d_raster_edge edges[] = {
        el2d_raster_setup_edge(v0, v1, height, target->stats),
        el2d_raster_setup_edge(v1, v2, height, target->stats),
        el2d_raster_setup_edge(v2, v0, height, target->stats),
    };
    for (int y = y_start; y < y_end; ++y) {
        int32_t py16 = y * EL2D_FIXED_SCALE + EL2D_FIXED_HALF;
        int32_t intersections[3];
        int count = 0;
        for (size_t edge_index = 0u; edge_index < 3u; ++edge_index) {
            const el2d_raster_edge *edge = &edges[edge_index];
            if (edge->valid && y >= edge->y_start && y < edge->y_end) {
                intersections[count++] = edge->x_start16 +
                    (int32_t)((int64_t)edge->dx16 * (y - edge->y_start));
            }
        }
        if (count < 2) continue;
#if EL2D_RENDER_DETAILED_STATS
        if (target->stats != 0) ++target->stats->scanline_count;
#endif
        int left_index = 0;
        int right_index = 0;
        for (int index = 1; index < count; ++index) {
            if (intersections[index] < intersections[left_index]) left_index = index;
            if (intersections[index] > intersections[right_index]) right_index = index;
        }
        int32_t left_x16 = intersections[left_index];
        int32_t right_x16 = intersections[right_index];
        int64_t span16 = (int64_t)right_x16 - left_x16;
        if (span16 <= 0) continue;
        int x_start = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(left_x16), 0, width);
        int x_end = el2d_raster_clamp_int(el2d_raster_ceil_pixel_center(right_x16), 0, width);
        int64_t pixel_x16 = (int64_t)x_start * EL2D_FIXED_SCALE + EL2D_FIXED_HALF;
        int64_t delta_x16 = pixel_x16 - v0->x16;
        int64_t delta_y16 = (int64_t)py16 - v0->y16;
        int32_t u16 = texture_u0_16 + (int32_t)(((int64_t)texture_du_dx16 * delta_x16 + (int64_t)texture_du_dy16 * delta_y16) >> 16);
        int32_t v16 = texture_v0_16 + (int32_t)(((int64_t)texture_dv_dx16 * delta_x16 + (int64_t)texture_dv_dy16 * delta_y16) >> 16);
        size_t framebuffer_index = (size_t)y * target->framebuffer->width + (size_t)x_start;
        for (int x = x_start; x < x_end; ++x) {
#if EL2D_RENDER_DETAILED_STATS
            if (target->stats != 0) {
                ++target->stats->candidate_pixel_count;
                ++target->stats->covered_pixel_count;
            }
#endif
            if (target->mask_coverage == 0 || el2d_raster_coverage_test(
                    target->mask_coverage,
                    framebuffer_index)) {
                el2d_raster_sample_and_blend(target, framebuffer_index, u16, v16);
            }
            ++framebuffer_index;
            u16 += texture_du_dx16;
            v16 += texture_dv_dx16;
        }
    }
}

void el2d_raster_draw_mask_triangle(
    uint8_t *coverage,
    uint16_t width,
    uint16_t height,
    const el2d_mesh_texture *texture,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2
) {
    el2d_raster_draw_mask_triangle_clipped(
        coverage, width, height, 0u, height, texture, v0, v1, v2);
}

void el2d_raster_draw_mask_triangle_clipped(
    uint8_t *coverage,
    uint16_t width,
    uint16_t height,
    uint16_t clip_y_min,
    uint16_t clip_y_max,
    const el2d_mesh_texture *texture,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2
) {
    el2d_raster_draw_mask_triangle_clipped_alpha_mode(
        coverage, width, height, clip_y_min, clip_y_max, texture,
        EL2D_TRIANGLE_ALPHA_MIXED, v0, v1, v2);
}

void el2d_raster_draw_mask_triangle_clipped_alpha_mode(
    uint8_t *coverage,
    uint16_t width,
    uint16_t height,
    uint16_t clip_y_min,
    uint16_t clip_y_max,
    const el2d_mesh_texture *texture,
    uint8_t triangle_alpha_mode,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2
) {
    if (coverage == 0 || width == 0u || height == 0u) return;
    uint16_t unused_pixel = 0u;
    el2d_framebuffer framebuffer = {
        .pixels = &unused_pixel,
        .width = width,
        .height = height,
    };
    el2d_raster_draw_target target = {
        .framebuffer = &framebuffer,
        .texture = texture,
        .opacity = 255u,
        .mask_coverage = 0,
        .coverage_output = coverage,
        .triangle_alpha_mode = triangle_alpha_mode,
        .clip_y_min = clip_y_min,
        .clip_y_max = clip_y_max,
        .stats = 0,
    };
    el2d_raster_draw_triangle(&target, v0, v1, v2);
}
