#ifndef EL2D_MESH_RENDERER_H
#define EL2D_MESH_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "el2d/framebuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_mesh_texture {
    const uint16_t *rgb565;
    const uint8_t *alpha4;
    uint16_t width;
    uint16_t height;
} el2d_mesh_texture;

typedef enum el2d_triangle_alpha_mode {
    EL2D_TRIANGLE_ALPHA_MIXED = 0,
    EL2D_TRIANGLE_ALPHA_OPAQUE = 1,
    EL2D_TRIANGLE_ALPHA_TRANSPARENT = 2,
} el2d_triangle_alpha_mode;

typedef struct el2d_mesh_drawable {
    const char *id;
    uint16_t texture_index;
    int16_t render_order;
    uint8_t visible;
    uint8_t opacity;
    uint16_t vertex_count;
    uint16_t index_count;
    const float *positions;
    const float *uvs;
    const uint16_t *indices;
    uint16_t mask_count;
    const uint16_t *masks;
    const uint8_t *triangle_alpha_modes;
} el2d_mesh_drawable;

typedef struct el2d_mesh_model {
    const el2d_mesh_texture *textures;
    size_t texture_count;
    const el2d_mesh_drawable *drawables;
    size_t drawable_count;
    float min_x;
    float max_x;
    float min_y;
    float max_y;
} el2d_mesh_model;

typedef struct el2d_mesh_camera {
    float zoom;
    float center_x;
    float center_y;
} el2d_mesh_camera;

typedef struct el2d_mesh_expression_layer {
    const el2d_mesh_model *reference_model;
    const el2d_mesh_model *target_model;
    float weight;
} el2d_mesh_expression_layer;

typedef struct el2d_mesh_render_stats {
    uint32_t triangle_count;
    uint32_t transformed_vertex_count;
    uint32_t rejected_triangle_count;
    uint32_t candidate_pixel_count;
    uint32_t covered_pixel_count;
    uint32_t texture_sample_count;
    uint32_t mask_build_count;
    uint32_t mask_reuse_count;
    uint32_t scanline_count;
    uint32_t division_count;
    uint32_t alpha_zero_sample_count;
    uint32_t opaque_sample_count;
    uint32_t blended_sample_count;
    uint32_t clear_us;
    uint32_t transform_us;
    uint32_t mask_us;
    uint32_t raster_us;
} el2d_mesh_render_stats;

typedef struct el2d_mesh_render_context el2d_mesh_render_context;

el2d_mesh_render_context *el2d_mesh_render_context_create(void);
void el2d_mesh_render_context_destroy(el2d_mesh_render_context *context);
void el2d_mesh_render_context_get_stats(
    const el2d_mesh_render_context *context,
    el2d_mesh_render_stats *stats);
void el2d_mesh_render_context_set_texture_overrides(
    el2d_mesh_render_context *context,
    const el2d_mesh_texture *textures,
    size_t texture_count);
void el2d_mesh_render_context_rgb565_blended_clipped(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    uint16_t background,
    uint16_t clip_y_min,
    uint16_t clip_y_max,
    uint8_t clear_background);
void el2d_mesh_render_context_rgb565_blended_expressions_clipped(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    const el2d_mesh_expression_layer *expression_layers,
    size_t expression_layer_count,
    uint16_t background,
    uint16_t clip_y_min,
    uint16_t clip_y_max,
    uint8_t clear_background);

void el2d_mesh_get_last_render_stats(el2d_mesh_render_stats *stats);

void el2d_mesh_render_rgb565(el2d_framebuffer *fb, const el2d_mesh_model *model, uint16_t background);
void el2d_mesh_render_rgb565_with_camera(
    el2d_framebuffer *fb,
    const el2d_mesh_model *model,
    const el2d_mesh_camera *camera,
    uint16_t background);
void el2d_mesh_render_rgb565_blended(
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    uint16_t background);
void el2d_mesh_render_rgb565_blended_range(
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    uint16_t background,
    int16_t min_render_order,
    int16_t max_render_order,
    uint8_t clear_background);

#ifdef __cplusplus
}
#endif

#endif
