#include "el2d/mesh_renderer.h"

#include <stdlib.h>
#include <string.h>

#include "el2d_mesh_rasterizer.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

static int64_t el2d_mesh_now_us(void) {
#ifdef ESP_PLATFORM
    return esp_timer_get_time();
#else
    return 0;
#endif
}

static uint32_t el2d_mesh_elapsed_us(int64_t started_us) {
#ifdef ESP_PLATFORM
    int64_t elapsed = esp_timer_get_time() - started_us;
    return elapsed > 0 ? (uint32_t)elapsed : 0u;
#else
    (void)started_us;
    return 0u;
#endif
}

typedef struct el2d_mesh_mask_cache_entry {
    uint8_t *coverage;
    size_t capacity;
    uint16_t width;
    uint16_t height;
    uint16_t mask_count;
    const uint16_t *masks;
} el2d_mesh_mask_cache_entry;

struct el2d_mesh_render_context {
    el2d_mesh_mask_cache_entry *mask_cache_entries;
    size_t mask_cache_count;
    size_t mask_cache_capacity;
    el2d_mesh_render_stats stats;
    el2d_raster_vertex *vertex_scratch;
    size_t vertex_scratch_capacity;
    el2d_raster_vertex *mask_vertex_scratch;
    size_t mask_vertex_scratch_capacity;
    float *position_scratch;
    size_t position_scratch_capacity;
    uint16_t clip_y_min;
    uint16_t clip_y_max;
    const el2d_mesh_texture *texture_overrides;
    size_t texture_override_count;
};

static el2d_mesh_render_context s_el2d_mesh_default_context = {0};
static int s_el2d_mesh_reference_rasterizer_for_testing = 0;

el2d_mesh_render_context *el2d_mesh_render_context_create(void) {
    return (el2d_mesh_render_context *)calloc(1u, sizeof(el2d_mesh_render_context));
}

void el2d_mesh_render_context_destroy(el2d_mesh_render_context *context) {
    if (context == 0 || context == &s_el2d_mesh_default_context) return;
    for (size_t index = 0u; index < context->mask_cache_capacity; ++index) {
        free(context->mask_cache_entries[index].coverage);
    }
    free(context->mask_cache_entries);
    free(context->vertex_scratch);
    free(context->mask_vertex_scratch);
    free(context->position_scratch);
    free(context);
}

void el2d_mesh_render_context_get_stats(
    const el2d_mesh_render_context *context,
    el2d_mesh_render_stats *stats
) {
    if (context != 0 && stats != 0) *stats = context->stats;
}

void el2d_mesh_render_context_set_texture_overrides(
    el2d_mesh_render_context *context,
    const el2d_mesh_texture *textures,
    size_t texture_count
) {
    if (context == 0) return;
    context->texture_overrides = textures;
    context->texture_override_count = textures != 0 ? texture_count : 0u;
}

static const el2d_mesh_texture *el2d_mesh_resolve_texture(
    const el2d_mesh_render_context *context,
    const el2d_mesh_model *model,
    size_t texture_index
) {
    if (model == 0 || model->textures == 0 || texture_index >= model->texture_count) return 0;
    if (context != 0 && context->texture_overrides != 0 &&
        texture_index < context->texture_override_count) {
        const el2d_mesh_texture *source = &model->textures[texture_index];
        const el2d_mesh_texture *override = &context->texture_overrides[texture_index];
        if (override->rgb565 != 0 &&
            override->width == source->width && override->height == source->height) {
            return override;
        }
    }
    return &model->textures[texture_index];
}

void el2d_mesh_set_reference_rasterizer_for_testing(int enabled) {
    s_el2d_mesh_reference_rasterizer_for_testing = enabled != 0;
}

void el2d_mesh_get_last_render_stats(el2d_mesh_render_stats *stats) {
    if (stats != 0) {
        *stats = s_el2d_mesh_default_context.stats;
    }
}

static float el2d_mesh_min3(float a, float b, float c) {
    float min = a < b ? a : b;
    return min < c ? min : c;
}

static float el2d_mesh_max3(float a, float b, float c) {
    float max = a > b ? a : b;
    return max > c ? max : c;
}

static int el2d_mesh_clamp_int(int value, int min, int max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static float el2d_mesh_clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float el2d_mesh_lerp_float(float from, float to, float progress) {
    return from + ((to - from) * progress);
}

static uint8_t el2d_mesh_lerp_u8(uint8_t from, uint8_t to, float progress) {
    float value = el2d_mesh_lerp_float((float)from, (float)to, progress);
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 255.0f) {
        return 255u;
    }
    return (uint8_t)(value + 0.5f);
}

static float el2d_mesh_edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static float el2d_mesh_screen_x(float model_x, float model_cx, float scale, uint16_t fb_width) {
    return ((model_x - model_cx) * scale) + ((float)fb_width * 0.5f);
}

static float el2d_mesh_screen_y(float model_y, float model_cy, float scale, uint16_t fb_height) {
    return ((model_cy - model_y) * scale) + ((float)fb_height * 0.5f);
}

static int el2d_mesh_drawables_compatible(
    const el2d_mesh_drawable *from,
    const el2d_mesh_drawable *to
) {
    if (from == 0 || to == 0) {
        return 0;
    }
    return from->texture_index == to->texture_index &&
           from->vertex_count == to->vertex_count &&
           from->index_count == to->index_count &&
           from->positions != 0 &&
           to->positions != 0 &&
           from->uvs != 0 &&
           to->uvs != 0 &&
           from->indices != 0 &&
           to->indices != 0;
}

static const el2d_mesh_drawable *el2d_mesh_find_target_drawable(
    const el2d_mesh_model *target_model,
    const el2d_mesh_drawable *source,
    size_t fallback_index
) {
    if (target_model == 0 || target_model->drawables == 0 || source == 0) {
        return 0;
    }
    if (source->id != 0) {
        for (size_t i = 0u; i < target_model->drawable_count; ++i) {
            const el2d_mesh_drawable *candidate = &target_model->drawables[i];
            if (candidate->id != 0 &&
                strcmp(source->id, candidate->id) == 0 &&
                el2d_mesh_drawables_compatible(source, candidate)) {
                return candidate;
            }
        }
        return 0;
    }
    if (fallback_index < target_model->drawable_count) {
        const el2d_mesh_drawable *candidate = &target_model->drawables[fallback_index];
        if (el2d_mesh_drawables_compatible(source, candidate)) {
            return candidate;
        }
    }
    return 0;
}

static const el2d_mesh_drawable *el2d_mesh_find_expression_drawable(
    const el2d_mesh_model *model,
    const el2d_mesh_drawable *source,
    size_t fallback_index
) {
    if (model == 0 || model->drawables == 0 || source == 0) return 0;
    if (fallback_index < model->drawable_count) {
        const el2d_mesh_drawable *candidate = &model->drawables[fallback_index];
        if (source->id != 0 && candidate->id != 0 &&
            strcmp(source->id, candidate->id) == 0 &&
            el2d_mesh_drawables_compatible(source, candidate)) {
            return candidate;
        }
    }
    if (source->id == 0) {
        return fallback_index < model->drawable_count &&
                el2d_mesh_drawables_compatible(source, &model->drawables[fallback_index])
            ? &model->drawables[fallback_index]
            : 0;
    }
    for (size_t index = 0u; index < model->drawable_count; ++index) {
        const el2d_mesh_drawable *candidate = &model->drawables[index];
        if (candidate->id != 0 && strcmp(source->id, candidate->id) == 0 &&
            el2d_mesh_drawables_compatible(source, candidate)) {
            return candidate;
        }
    }
    return 0;
}

static const float *el2d_mesh_compose_expression_positions(
    el2d_mesh_render_context *context,
    const el2d_mesh_drawable *drawable,
    const el2d_mesh_drawable *target_drawable,
    size_t fallback_index,
    float progress,
    const el2d_mesh_expression_layer *expression_layers,
    size_t expression_layer_count
) {
    if (context == 0 || drawable == 0 || drawable->positions == 0 ||
        expression_layers == 0 || expression_layer_count == 0u) {
        return 0;
    }
    int has_active_layer = 0;
    for (size_t layer_index = 0u; layer_index < expression_layer_count; ++layer_index) {
        const el2d_mesh_expression_layer *layer = &expression_layers[layer_index];
        if (el2d_mesh_clamp01(layer->weight) > 0.0f) {
            has_active_layer = 1;
            break;
        }
    }
    if (!has_active_layer) return 0;

    size_t value_count = (size_t)drawable->vertex_count * 2u;
    if (value_count > context->position_scratch_capacity) {
        float *scratch = (float *)realloc(context->position_scratch, value_count * sizeof(float));
        if (scratch == 0) return 0;
        context->position_scratch = scratch;
        context->position_scratch_capacity = value_count;
    }
    float state_progress = el2d_mesh_clamp01(progress);
    for (size_t value_index = 0u; value_index < value_count; ++value_index) {
        float value = drawable->positions[value_index];
        if (target_drawable != 0) {
            value += (target_drawable->positions[value_index] - value) * state_progress;
        }
        context->position_scratch[value_index] = value;
    }
    for (size_t layer_index = 0u; layer_index < expression_layer_count; ++layer_index) {
        const el2d_mesh_expression_layer *layer = &expression_layers[layer_index];
        float weight = el2d_mesh_clamp01(layer->weight);
        if (weight <= 0.0f) continue;
        const el2d_mesh_drawable *reference = el2d_mesh_find_expression_drawable(
            layer->reference_model, drawable, fallback_index);
        const el2d_mesh_drawable *target = el2d_mesh_find_expression_drawable(
            layer->target_model, drawable, fallback_index);
        if (reference == 0 || target == 0 || !el2d_mesh_drawables_compatible(reference, target)) {
            continue;
        }
        for (size_t value_index = 0u; value_index < value_count; ++value_index) {
            context->position_scratch[value_index] +=
                (target->positions[value_index] - reference->positions[value_index]) * weight;
        }
    }
    return context->position_scratch;
}

static int el2d_mesh_drawable_is_renderable(const el2d_mesh_drawable *drawable) {
    if (drawable == 0) {
        return 0;
    }
    if (drawable->id != 0 && strncmp(drawable->id, "HitArea", 7u) == 0) {
        return 0;
    }
    return 1;
}

static uint8_t el2d_mesh_alpha4_at(const el2d_mesh_texture *texture, int x, int y) {
    if (texture == 0 || texture->alpha4 == 0 ||
        texture->width == 0u || texture->height == 0u) {
        return 15u;
    }
    x = el2d_mesh_clamp_int(x, 0, (int)texture->width - 1);
    y = el2d_mesh_clamp_int(y, 0, (int)texture->height - 1);
    size_t pixel_index = (size_t)y * texture->width + (size_t)x;
    uint8_t packed = texture->alpha4[pixel_index / 2u];
    return (pixel_index & 1u) == 0u ? (uint8_t)(packed >> 4u) : (uint8_t)(packed & 0x0fu);
}

static uint16_t el2d_mesh_rgb565_blend(uint16_t dst, uint16_t src, uint8_t alpha) {
    if (alpha == 0u) {
        return dst;
    }
    if (alpha >= 255u) {
        return src;
    }
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

static void el2d_mesh_plot(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_texture *texture,
    uint8_t opacity,
    int x,
    int y,
    float u,
    float v
) {
    if (fb == 0 || texture == 0 || texture->rgb565 == 0 || opacity == 0u) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int)fb->width || y >= (int)fb->height || texture->width == 0u || texture->height == 0u) {
        return;
    }
    int tx = (int)(u * (float)(texture->width - 1u) + 0.5f);
    int ty = (int)((1.0f - v) * (float)(texture->height - 1u) + 0.5f);
    tx = el2d_mesh_clamp_int(tx, 0, (int)texture->width - 1);
    ty = el2d_mesh_clamp_int(ty, 0, (int)texture->height - 1);
    ++context->stats.texture_sample_count;
    uint8_t alpha4 = el2d_mesh_alpha4_at(texture, tx, ty);
    if (alpha4 == 0u) {
        ++context->stats.alpha_zero_sample_count;
        return;
    }
    size_t texture_index = (size_t)ty * texture->width + (size_t)tx;
    uint16_t source_color = texture->rgb565[texture_index];
    size_t fb_index = (size_t)y * fb->width + (size_t)x;
    if (alpha4 == 15u && opacity == 255u) {
        ++context->stats.opaque_sample_count;
        fb->pixels[fb_index] = source_color;
        return;
    }
    ++context->stats.blended_sample_count;
    uint8_t alpha = opacity == 255u
        ? (uint8_t)(alpha4 * 17u)
        : (uint8_t)(((uint16_t)alpha4 * 17u * opacity) / 255u);
    if (alpha == 0u) {
        return;
    }
    fb->pixels[fb_index] = el2d_mesh_rgb565_blend(fb->pixels[fb_index], source_color, alpha);
}

static uint8_t *el2d_mesh_mask_cache_begin(
    el2d_mesh_mask_cache_entry *entry,
    uint16_t width,
    uint16_t height
) {
    size_t needed = el2d_raster_coverage_bytes(width, height);
    if (entry == 0 || needed == 0u) {
        return 0;
    }
    if (needed > entry->capacity) {
        uint8_t *coverage = (uint8_t *)realloc(entry->coverage, needed);
        if (coverage == 0) {
            return 0;
        }
        entry->coverage = coverage;
        entry->capacity = needed;
    }
    entry->width = width;
    entry->height = height;
    memset(entry->coverage, 0, needed);
    return entry->coverage;
}

static int el2d_mesh_mask_sets_equal(
    const el2d_mesh_mask_cache_entry *entry,
    const el2d_mesh_drawable *drawable
) {
    if (entry == 0 || drawable == 0 || entry->mask_count != drawable->mask_count ||
        entry->masks == 0 || drawable->masks == 0) {
        return 0;
    }
    for (uint16_t index = 0u; index < drawable->mask_count; ++index) {
        if (entry->masks[index] != drawable->masks[index]) return 0;
    }
    return 1;
}

static el2d_mesh_mask_cache_entry *el2d_mesh_find_mask_cache_entry(
    el2d_mesh_render_context *context,
    const el2d_mesh_drawable *drawable
) {
    for (size_t index = 0u; index < context->mask_cache_count; ++index) {
        el2d_mesh_mask_cache_entry *entry = &context->mask_cache_entries[index];
        if (el2d_mesh_mask_sets_equal(entry, drawable)) return entry;
    }
    return 0;
}

static el2d_mesh_mask_cache_entry *el2d_mesh_append_mask_cache_entry(
    el2d_mesh_render_context *context,
    const el2d_mesh_drawable *drawable
) {
    if (context->mask_cache_count >= context->mask_cache_capacity) {
        size_t next_capacity = context->mask_cache_capacity == 0u
            ? 4u
            : context->mask_cache_capacity * 2u;
        el2d_mesh_mask_cache_entry *entries = (el2d_mesh_mask_cache_entry *)realloc(
            context->mask_cache_entries,
            next_capacity * sizeof(el2d_mesh_mask_cache_entry));
        if (entries == 0) return 0;
        memset(
            entries + context->mask_cache_capacity,
            0,
            (next_capacity - context->mask_cache_capacity) * sizeof(el2d_mesh_mask_cache_entry));
        context->mask_cache_entries = entries;
        context->mask_cache_capacity = next_capacity;
    }
    el2d_mesh_mask_cache_entry *entry =
        &context->mask_cache_entries[context->mask_cache_count++];
    entry->mask_count = drawable->mask_count;
    entry->masks = drawable->masks;
    return entry;
}

static const uint8_t *el2d_mesh_build_mask_cache(
    el2d_mesh_render_context *context,
    const el2d_mesh_model *model,
    const el2d_mesh_model *target_model,
    const el2d_mesh_drawable *drawable,
    float progress,
    const el2d_mesh_expression_layer *expression_layers,
    size_t expression_layer_count,
    float model_cx,
    float model_cy,
    float scale,
    uint16_t fb_width,
    uint16_t fb_height
) {
    if (model == 0 || drawable == 0 || drawable->mask_count == 0u || drawable->masks == 0) {
        return 0;
    }
    el2d_mesh_mask_cache_entry *entry = el2d_mesh_find_mask_cache_entry(context, drawable);
    if (entry != 0) {
        ++context->stats.mask_reuse_count;
        return entry->coverage;
    }
    entry = el2d_mesh_append_mask_cache_entry(context, drawable);
    if (entry == 0) return 0;
    uint8_t *coverage = el2d_mesh_mask_cache_begin(entry, fb_width, fb_height);
    if (coverage == 0) {
        if (context->mask_cache_count > 0u) --context->mask_cache_count;
        return 0;
    }
    ++context->stats.mask_build_count;
    for (uint16_t mask_id = 0; mask_id < drawable->mask_count; ++mask_id) {
        uint16_t mask_index = drawable->masks[mask_id];
        if (mask_index >= model->drawable_count) {
            continue;
        }
        const el2d_mesh_drawable *mask = &model->drawables[mask_index];
        const el2d_mesh_drawable *target_mask =
            el2d_mesh_find_target_drawable(target_model, mask, mask_index);
        if (mask->texture_index >= model->texture_count) {
            continue;
        }
        const el2d_mesh_texture *texture = el2d_mesh_resolve_texture(context, model, mask->texture_index);
        if (texture == 0) continue;
        if (mask->vertex_count > context->mask_vertex_scratch_capacity) {
            el2d_raster_vertex *scratch = (el2d_raster_vertex *)realloc(
                context->mask_vertex_scratch,
                (size_t)mask->vertex_count * sizeof(el2d_raster_vertex));
            if (scratch == 0) continue;
            context->mask_vertex_scratch = scratch;
            context->mask_vertex_scratch_capacity = mask->vertex_count;
        }
        const float *composed_positions = el2d_mesh_compose_expression_positions(
            context,
            mask,
            target_mask,
            mask_index,
            progress,
            expression_layers,
            expression_layer_count);
        el2d_raster_transform transform = {
            .source_positions = composed_positions != 0 ? composed_positions : mask->positions,
            .target_positions = composed_positions == 0 && target_mask != 0 ? target_mask->positions : 0,
            .uvs = mask->uvs,
            .vertex_count = mask->vertex_count,
            .progress = composed_positions != 0 ? 0.0f : progress,
            .model_cx = model_cx,
            .model_cy = model_cy,
            .scale = scale,
            .framebuffer_width = fb_width,
            .framebuffer_height = fb_height,
        };
        if (!el2d_raster_transform_vertices(
                &transform,
                context->mask_vertex_scratch,
                context->mask_vertex_scratch_capacity)) {
            continue;
        }
        for (uint16_t i = 0; i + 2u < mask->index_count; i += 3u) {
            uint16_t i0 = mask->indices[i];
            uint16_t i1 = mask->indices[i + 1u];
            uint16_t i2 = mask->indices[i + 2u];
            if (i0 >= mask->vertex_count || i1 >= mask->vertex_count || i2 >= mask->vertex_count) continue;
            uint8_t triangle_alpha_mode = mask->triangle_alpha_modes != 0
                ? mask->triangle_alpha_modes[i / 3u]
                : EL2D_TRIANGLE_ALPHA_MIXED;
            el2d_raster_draw_mask_triangle_clipped_alpha_mode(
                coverage,
                fb_width,
                fb_height,
                context->clip_y_min,
                context->clip_y_max,
                texture,
                triangle_alpha_mode,
                &context->mask_vertex_scratch[i0],
                &context->mask_vertex_scratch[i1],
                &context->mask_vertex_scratch[i2]);
        }
    }
    return coverage;
}

static int el2d_mesh_point_in_mask(
    const el2d_mesh_model *model,
    const el2d_mesh_drawable *mask,
    float model_cx,
    float model_cy,
    float scale,
    uint16_t fb_width,
    uint16_t fb_height,
    float px,
    float py
) {
    if (model == 0 || mask == 0 ||
        mask->positions == 0 || mask->uvs == 0 || mask->indices == 0 ||
        mask->texture_index >= model->texture_count) {
        return 0;
    }
    const el2d_mesh_texture *texture = &model->textures[mask->texture_index];
    for (uint16_t i = 0; i + 2u < mask->index_count; i += 3u) {
        uint16_t i0 = mask->indices[i];
        uint16_t i1 = mask->indices[i + 1u];
        uint16_t i2 = mask->indices[i + 2u];
        if (i0 >= mask->vertex_count || i1 >= mask->vertex_count || i2 >= mask->vertex_count) {
            continue;
        }
        float x0 = el2d_mesh_screen_x(mask->positions[(size_t)i0 * 2u], model_cx, scale, fb_width);
        float y0 = el2d_mesh_screen_y(mask->positions[(size_t)i0 * 2u + 1u], model_cy, scale, fb_height);
        float x1 = el2d_mesh_screen_x(mask->positions[(size_t)i1 * 2u], model_cx, scale, fb_width);
        float y1 = el2d_mesh_screen_y(mask->positions[(size_t)i1 * 2u + 1u], model_cy, scale, fb_height);
        float x2 = el2d_mesh_screen_x(mask->positions[(size_t)i2 * 2u], model_cx, scale, fb_width);
        float y2 = el2d_mesh_screen_y(mask->positions[(size_t)i2 * 2u + 1u], model_cy, scale, fb_height);
        float area = el2d_mesh_edge(x0, y0, x1, y1, x2, y2);
        if (area == 0.0f) {
            continue;
        }
        float w0 = el2d_mesh_edge(x1, y1, x2, y2, px, py) / area;
        float w1 = el2d_mesh_edge(x2, y2, x0, y0, px, py) / area;
        float w2 = el2d_mesh_edge(x0, y0, x1, y1, px, py) / area;
        if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
            float u0 = mask->uvs[(size_t)i0 * 2u];
            float v0 = mask->uvs[(size_t)i0 * 2u + 1u];
            float u1 = mask->uvs[(size_t)i1 * 2u];
            float v1 = mask->uvs[(size_t)i1 * 2u + 1u];
            float u2 = mask->uvs[(size_t)i2 * 2u];
            float v2 = mask->uvs[(size_t)i2 * 2u + 1u];
            int tx = (int)((w0 * u0 + w1 * u1 + w2 * u2) * (float)(texture->width - 1u) + 0.5f);
            int ty = (int)((1.0f - (w0 * v0 + w1 * v1 + w2 * v2)) * (float)(texture->height - 1u) + 0.5f);
            if (el2d_mesh_alpha4_at(texture, tx, ty) > 0u) {
                return 1;
            }
        }
    }
    return 0;
}

static int el2d_mesh_point_in_any_mask(
    const el2d_mesh_model *model,
    const el2d_mesh_drawable *drawable,
    float model_cx,
    float model_cy,
    float scale,
    uint16_t fb_width,
    uint16_t fb_height,
    float px,
    float py
) {
    if (drawable == 0 || drawable->mask_count == 0u || drawable->masks == 0) {
        return 1;
    }
    for (uint16_t i = 0; i < drawable->mask_count; ++i) {
        uint16_t mask_index = drawable->masks[i];
        if (model != 0 && mask_index < model->drawable_count &&
            el2d_mesh_point_in_mask(model, &model->drawables[mask_index], model_cx, model_cy, scale, fb_width, fb_height, px, py)) {
            return 1;
        }
    }
    return 0;
}

static void el2d_mesh_render_triangle_reference(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *model,
    const el2d_mesh_texture *texture,
    const el2d_mesh_drawable *drawable,
    const el2d_raster_vertex *vertices,
    uint8_t opacity,
    const uint8_t *mask_coverage,
    float model_cx,
    float model_cy,
    float scale,
    uint8_t triangle_alpha_mode,
    uint16_t i0,
    uint16_t i1,
    uint16_t i2
) {
    if (triangle_alpha_mode == EL2D_TRIANGLE_ALPHA_TRANSPARENT) {
        return;
    }
    ++context->stats.triangle_count;
    if (i0 >= drawable->vertex_count || i1 >= drawable->vertex_count || i2 >= drawable->vertex_count) {
        ++context->stats.rejected_triangle_count;
        return;
    }
    float x0 = (float)vertices[i0].x16 / 65536.0f;
    float y0 = (float)vertices[i0].y16 / 65536.0f;
    float x1 = (float)vertices[i1].x16 / 65536.0f;
    float y1 = (float)vertices[i1].y16 / 65536.0f;
    float x2 = (float)vertices[i2].x16 / 65536.0f;
    float y2 = (float)vertices[i2].y16 / 65536.0f;
    float area = el2d_mesh_edge(x0, y0, x1, y1, x2, y2);
    if (area == 0.0f) {
        ++context->stats.rejected_triangle_count;
        return;
    }

    int min_x = el2d_mesh_clamp_int((int)el2d_mesh_min3(x0, x1, x2), 0, (int)fb->width - 1);
    int max_x = el2d_mesh_clamp_int((int)(el2d_mesh_max3(x0, x1, x2) + 1.0f), 0, (int)fb->width - 1);
    int min_y = el2d_mesh_clamp_int((int)el2d_mesh_min3(y0, y1, y2), 0, (int)fb->height - 1);
    int max_y = el2d_mesh_clamp_int((int)(el2d_mesh_max3(y0, y1, y2) + 1.0f), 0, (int)fb->height - 1);
    if (context->clip_y_max > context->clip_y_min) {
        if (min_y < context->clip_y_min) min_y = context->clip_y_min;
        if (max_y >= context->clip_y_max) max_y = context->clip_y_max - 1;
    }
    if (min_y > max_y) return;

    float u0 = drawable->uvs[(size_t)i0 * 2u];
    float v0 = drawable->uvs[(size_t)i0 * 2u + 1u];
    float u1 = drawable->uvs[(size_t)i1 * 2u];
    float v1 = drawable->uvs[(size_t)i1 * 2u + 1u];
    float u2 = drawable->uvs[(size_t)i2 * 2u];
    float v2 = drawable->uvs[(size_t)i2 * 2u + 1u];
    int has_mask = drawable->mask_count > 0u && drawable->masks != 0;
    float inv_area = 1.0f / area;
    float w0n_dx = y2 - y1;
    float w1n_dx = y0 - y2;
    float w2n_dx = y1 - y0;
    float w0n_dy = -(x2 - x1);
    float w1n_dy = -(x0 - x2);
    float w2n_dy = -(x1 - x0);
    float start_x = (float)min_x + 0.5f;
    float start_y = (float)min_y + 0.5f;
    float w0n_row = el2d_mesh_edge(x1, y1, x2, y2, start_x, start_y);
    float w1n_row = el2d_mesh_edge(x2, y2, x0, y0, start_x, start_y);
    float w2n_row = el2d_mesh_edge(x0, y0, x1, y1, start_x, start_y);
    for (int y = min_y; y <= max_y; ++y) {
        float w0n = w0n_row;
        float w1n = w1n_row;
        float w2n = w2n_row;
        for (int x = min_x; x <= max_x; ++x) {
            ++context->stats.candidate_pixel_count;
            int inside = area > 0.0f
                ? (w0n >= 0.0f && w1n >= 0.0f && w2n >= 0.0f)
                : (w0n <= 0.0f && w1n <= 0.0f && w2n <= 0.0f);
            if (inside) {
                ++context->stats.covered_pixel_count;
                size_t mask_pixel_index = (size_t)y * fb->width + (size_t)x;
                if (has_mask && mask_coverage != 0 &&
                    (mask_coverage[mask_pixel_index >> 3u] &
                     (uint8_t)(1u << (mask_pixel_index & 7u))) == 0u) {
                    continue;
                }
                float px = (float)x + 0.5f;
                float py = (float)y + 0.5f;
                if (has_mask && mask_coverage == 0 && !el2d_mesh_point_in_any_mask(model, drawable, model_cx, model_cy, scale, fb->width, fb->height, px, py)) {
                    continue;
                }
                float u = (w0n * u0 + w1n * u1 + w2n * u2) * inv_area;
                float v = (w0n * v0 + w1n * v1 + w2n * v2) * inv_area;
                el2d_mesh_plot(context, fb, texture, opacity, x, y, u, v);
            }
            w0n += w0n_dx;
            w1n += w1n_dx;
            w2n += w2n_dx;
        }
        w0n_row += w0n_dy;
        w1n_row += w1n_dy;
        w2n_row += w2n_dy;
    }
}

static void el2d_mesh_render_triangle(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *model,
    const el2d_mesh_texture *texture,
    const el2d_mesh_drawable *drawable,
    const el2d_raster_vertex *vertices,
    uint8_t opacity,
    const uint8_t *mask_coverage,
    float model_cx,
    float model_cy,
    float scale,
    uint8_t triangle_alpha_mode,
    uint16_t i0,
    uint16_t i1,
    uint16_t i2
) {
    int needs_reference_mask_fallback =
        drawable->mask_count > 0u && drawable->masks != 0 && mask_coverage == 0;
    if (s_el2d_mesh_reference_rasterizer_for_testing || needs_reference_mask_fallback) {
        el2d_mesh_render_triangle_reference(
            context,
            fb,
            model,
            texture,
            drawable,
            vertices,
            opacity,
            mask_coverage,
            model_cx,
            model_cy,
            scale,
            triangle_alpha_mode,
            i0,
            i1,
            i2);
        return;
    }
    if (i0 >= drawable->vertex_count || i1 >= drawable->vertex_count || i2 >= drawable->vertex_count) {
        ++context->stats.triangle_count;
        ++context->stats.rejected_triangle_count;
        return;
    }
    el2d_raster_draw_target target = {
        .framebuffer = fb,
        .texture = texture,
        .opacity = opacity,
        .mask_coverage = mask_coverage,
        .triangle_alpha_mode = triangle_alpha_mode,
        .clip_y_min = context->clip_y_min,
        .clip_y_max = context->clip_y_max,
        .stats = &context->stats,
    };
    el2d_raster_draw_triangle(&target, &vertices[i0], &vertices[i1], &vertices[i2]);
}

static void el2d_mesh_render_drawable(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *model,
    const el2d_mesh_model *target_model,
    const el2d_mesh_drawable *drawable,
    const el2d_mesh_drawable *target_drawable,
    size_t drawable_index,
    float progress,
    const el2d_mesh_expression_layer *expression_layers,
    size_t expression_layer_count,
    float model_cx,
    float model_cy,
    float scale
) {
    if (!el2d_mesh_drawable_is_renderable(drawable)) {
        return;
    }
    if (drawable == 0 || drawable->positions == 0 || drawable->uvs == 0 || drawable->indices == 0) {
        return;
    }
    if (drawable->texture_index >= model->texture_count) {
        return;
    }
    uint8_t source_opacity = drawable->visible != 0u ? drawable->opacity : 0u;
    uint8_t opacity = source_opacity;
    if (target_drawable != 0) {
        uint8_t target_opacity = target_drawable->visible != 0u ? target_drawable->opacity : 0u;
        opacity = el2d_mesh_lerp_u8(source_opacity, target_opacity, progress);
    }
    if (opacity == 0u) {
        return;
    }
    const el2d_mesh_texture *texture = el2d_mesh_resolve_texture(context, model, drawable->texture_index);
    if (texture == 0) return;
    if (drawable->vertex_count > context->vertex_scratch_capacity) {
        size_t capacity = drawable->vertex_count;
        el2d_raster_vertex *scratch = (el2d_raster_vertex *)realloc(
            context->vertex_scratch,
            capacity * sizeof(el2d_raster_vertex));
        if (scratch == 0) {
            return;
        }
        context->vertex_scratch = scratch;
        context->vertex_scratch_capacity = capacity;
    }
    int64_t transform_started_us = el2d_mesh_now_us();
    const float *composed_positions = el2d_mesh_compose_expression_positions(
        context,
        drawable,
        target_drawable,
        drawable_index,
        progress,
        expression_layers,
        expression_layer_count);
    el2d_raster_transform transform = {
        .source_positions = composed_positions != 0 ? composed_positions : drawable->positions,
        .target_positions = composed_positions == 0 && target_drawable != 0 ? target_drawable->positions : 0,
        .uvs = drawable->uvs,
        .vertex_count = drawable->vertex_count,
        .progress = composed_positions != 0 ? 0.0f : progress,
        .model_cx = model_cx,
        .model_cy = model_cy,
        .scale = scale,
        .framebuffer_width = fb->width,
        .framebuffer_height = fb->height,
    };
    if (!el2d_raster_transform_vertices(
            &transform,
            context->vertex_scratch,
            context->vertex_scratch_capacity)) {
        context->stats.transform_us += el2d_mesh_elapsed_us(transform_started_us);
        return;
    }
    context->stats.transform_us += el2d_mesh_elapsed_us(transform_started_us);
    context->stats.transformed_vertex_count += drawable->vertex_count;
    const uint8_t *mask_coverage = 0;
    if (drawable->mask_count > 0u && drawable->masks != 0) {
        int64_t mask_started_us = el2d_mesh_now_us();
        mask_coverage = el2d_mesh_build_mask_cache(
            context,
            model,
            target_model,
            drawable,
            progress,
            expression_layers,
            expression_layer_count,
            model_cx,
            model_cy,
            scale,
            fb->width,
            fb->height);
        context->stats.mask_us += el2d_mesh_elapsed_us(mask_started_us);
    }
    int64_t raster_started_us = el2d_mesh_now_us();
    for (uint16_t i = 0; i + 2u < drawable->index_count; i += 3u) {
        uint8_t triangle_alpha_mode = drawable->triangle_alpha_modes != 0
            ? drawable->triangle_alpha_modes[i / 3u]
            : EL2D_TRIANGLE_ALPHA_MIXED;
        el2d_mesh_render_triangle(
            context,
            fb,
            model,
            texture,
            drawable,
            context->vertex_scratch,
            opacity,
            mask_coverage,
            model_cx,
            model_cy,
            scale,
            triangle_alpha_mode,
            drawable->indices[i],
            drawable->indices[i + 1u],
            drawable->indices[i + 2u]);
    }
    context->stats.raster_us += el2d_mesh_elapsed_us(raster_started_us);
}

static void el2d_mesh_render_context_rgb565_blended_range(
    el2d_mesh_render_context *context,
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    const el2d_mesh_expression_layer *expression_layers,
    size_t expression_layer_count,
    uint16_t background,
    int16_t min_render_order,
    int16_t max_render_order,
    uint8_t clear_background,
    uint16_t clip_y_min,
    uint16_t clip_y_max
) {
    if (context == 0) return;
    memset(&context->stats, 0, sizeof(context->stats));
    context->mask_cache_count = 0u;
    const el2d_mesh_model *model = from_model;
    progress = el2d_mesh_clamp01(progress);
    if (progress >= 1.0f && to_model != 0) {
        model = to_model;
        to_model = 0;
        progress = 0.0f;
    } else if (progress <= 0.0f) {
        to_model = 0;
    }
    if (fb == 0 || fb->pixels == 0 || model == 0 || model->textures == 0 || model->drawables == 0) {
        return;
    }
    context->clip_y_min = (uint16_t)el2d_mesh_clamp_int(clip_y_min, 0, fb->height);
    context->clip_y_max = (uint16_t)el2d_mesh_clamp_int(clip_y_max, context->clip_y_min, fb->height);
    if (context->clip_y_max <= context->clip_y_min) {
        context->clip_y_min = 0u;
        context->clip_y_max = fb->height;
    }
    if (clear_background != 0u) {
        int64_t clear_started_us = el2d_mesh_now_us();
        for (uint16_t y = context->clip_y_min; y < context->clip_y_max; ++y) {
            for (uint16_t x = 0u; x < fb->width; ++x) {
                fb->pixels[(size_t)y * fb->width + x] = background;
            }
        }
        context->stats.clear_us += el2d_mesh_elapsed_us(clear_started_us);
    }
    float range_x = model->max_x - model->min_x;
    float range_y = model->max_y - model->min_y;
    if (range_x <= 0.0f || range_y <= 0.0f) {
        return;
    }
    float scale_x = ((float)fb->width * 0.94f) / range_x;
    float scale_y = ((float)fb->height * 0.94f) / range_y;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (camera != 0 && camera->zoom > 0.1f) {
        scale *= camera->zoom;
    }
    float model_cx = camera != 0 ? camera->center_x : (model->min_x + model->max_x) * 0.5f;
    float model_cy = camera != 0 ? camera->center_y : (model->min_y + model->max_y) * 0.5f;
    int has_previous = 0;
    int16_t previous_order = 0;
    size_t previous_index = 0u;
    for (size_t pass = 0u; pass < model->drawable_count; ++pass) {
        int found = 0;
        int16_t selected_order = 0;
        size_t selected_index = 0u;
        for (size_t i = 0u; i < model->drawable_count; ++i) {
            const el2d_mesh_drawable *drawable = &model->drawables[i];
            int follows_previous = !has_previous ||
                drawable->render_order > previous_order ||
                (drawable->render_order == previous_order && i > previous_index);
            if (!follows_previous ||
                drawable->render_order < min_render_order ||
                drawable->render_order > max_render_order) {
                continue;
            }
            if (!found ||
                drawable->render_order < selected_order ||
                (drawable->render_order == selected_order && i < selected_index)) {
                found = 1;
                selected_order = drawable->render_order;
                selected_index = i;
            }
        }
        if (!found) {
            break;
        }
        const el2d_mesh_drawable *drawable = &model->drawables[selected_index];
        const el2d_mesh_drawable *target_drawable =
            el2d_mesh_find_target_drawable(to_model, drawable, selected_index);
        el2d_mesh_render_drawable(
            context,
            fb,
            model,
            to_model,
            drawable,
            target_drawable,
            selected_index,
            progress,
            expression_layers,
            expression_layer_count,
            model_cx,
            model_cy,
            scale);
        has_previous = 1;
        previous_order = selected_order;
        previous_index = selected_index;
    }
}

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
    uint8_t clear_background
) {
    el2d_mesh_render_context_rgb565_blended_range(
        context, fb, from_model, to_model, camera, progress, 0, 0u, background,
        INT16_MIN, INT16_MAX, clear_background, clip_y_min, clip_y_max);
}

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
    uint8_t clear_background
) {
    el2d_mesh_render_context_rgb565_blended_range(
        context, fb, from_model, to_model, camera, progress,
        expression_layers, expression_layer_count, background,
        INT16_MIN, INT16_MAX, clear_background, clip_y_min, clip_y_max);
}

void el2d_mesh_render_rgb565_blended_range(
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    uint16_t background,
    int16_t min_render_order,
    int16_t max_render_order,
    uint8_t clear_background
) {
    el2d_mesh_render_context_rgb565_blended_range(
        &s_el2d_mesh_default_context, fb, from_model, to_model, camera, progress,
        0, 0u, background,
        min_render_order, max_render_order, clear_background, 0u, fb != 0 ? fb->height : 0u);
}

void el2d_mesh_render_rgb565_blended(
    el2d_framebuffer *fb,
    const el2d_mesh_model *from_model,
    const el2d_mesh_model *to_model,
    const el2d_mesh_camera *camera,
    float progress,
    uint16_t background
) {
    el2d_mesh_render_rgb565_blended_range(fb, from_model, to_model, camera, progress, background, INT16_MIN, INT16_MAX, 1u);
}

void el2d_mesh_render_rgb565_with_camera(
    el2d_framebuffer *fb,
    const el2d_mesh_model *model,
    const el2d_mesh_camera *camera,
    uint16_t background
) {
    el2d_mesh_render_rgb565_blended(fb, model, 0, camera, 0.0f, background);
}

void el2d_mesh_render_rgb565(el2d_framebuffer *fb, const el2d_mesh_model *model, uint16_t background) {
    el2d_mesh_render_rgb565_with_camera(fb, model, 0, background);
}
