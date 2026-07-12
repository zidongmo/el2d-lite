#ifndef EL2D_MESH_RASTERIZER_H
#define EL2D_MESH_RASTERIZER_H

#include <stddef.h>
#include <stdint.h>

#include "el2d/mesh_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_raster_vertex {
    int32_t x16;
    int32_t y16;
    int32_t u16;
    int32_t v16;
} el2d_raster_vertex;

typedef struct el2d_raster_transform {
    const float *source_positions;
    const float *target_positions;
    const float *uvs;
    size_t vertex_count;
    float progress;
    float model_cx;
    float model_cy;
    float scale;
    uint16_t framebuffer_width;
    uint16_t framebuffer_height;
} el2d_raster_transform;

int el2d_raster_transform_vertices(
    const el2d_raster_transform *transform,
    el2d_raster_vertex *output,
    size_t output_capacity);

size_t el2d_raster_coverage_bytes(uint16_t width, uint16_t height);

typedef struct el2d_raster_draw_target {
    el2d_framebuffer *framebuffer;
    const el2d_mesh_texture *texture;
    uint8_t opacity;
    const uint8_t *mask_coverage;
    uint8_t *coverage_output;
    uint8_t triangle_alpha_mode;
    uint16_t clip_y_min;
    uint16_t clip_y_max;
    el2d_mesh_render_stats *stats;
} el2d_raster_draw_target;

void el2d_raster_draw_triangle(
    const el2d_raster_draw_target *target,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2);

void el2d_raster_draw_mask_triangle(
    uint8_t *coverage,
    uint16_t width,
    uint16_t height,
    const el2d_mesh_texture *texture,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2);

void el2d_raster_draw_mask_triangle_clipped(
    uint8_t *coverage,
    uint16_t width,
    uint16_t height,
    uint16_t clip_y_min,
    uint16_t clip_y_max,
    const el2d_mesh_texture *texture,
    const el2d_raster_vertex *v0,
    const el2d_raster_vertex *v1,
    const el2d_raster_vertex *v2);

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
    const el2d_raster_vertex *v2);

void el2d_mesh_set_reference_rasterizer_for_testing(int enabled);

#ifdef __cplusplus
}
#endif

#endif
