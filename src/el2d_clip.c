#include "el2d/clip.h"

static float el2d_clamp01_local(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float el2d_sample_curve(const el2d_curve *curve, uint32_t time_ms) {
    if (curve->key_count == 0u) {
        return 0.0f;
    }
    if (curve->key_count == 1u || time_ms <= curve->keys[0].time_ms) {
        return curve->keys[0].value;
    }

    for (size_t i = 1; i < curve->key_count; ++i) {
        const el2d_curve_key *previous = &curve->keys[i - 1u];
        const el2d_curve_key *next = &curve->keys[i];
        if (time_ms <= next->time_ms) {
            uint32_t span = next->time_ms - previous->time_ms;
            if (span == 0u) {
                return next->value;
            }
            float t = (float)(time_ms - previous->time_ms) / (float)span;
            float eased = el2d_ease(curve->easing, t);
            return previous->value + (next->value - previous->value) * eased;
        }
    }

    return curve->keys[curve->key_count - 1u].value;
}

el2d_result el2d_clip_apply(const el2d_clip *clip, el2d_model *model, uint32_t time_ms, float weight) {
    if (clip == 0 || model == 0 || clip->curves == 0) {
        return EL2D_ERROR_INVALID_ARGUMENT;
    }

    float clamped_weight = el2d_clamp01_local(weight);
    uint32_t sample_time = time_ms;
    if (clip->duration_ms > 0u && sample_time > clip->duration_ms) {
        sample_time = clip->duration_ms;
    }

    for (size_t i = 0; i < clip->curve_count; ++i) {
        const el2d_curve *curve = &clip->curves[i];
        if (curve->parameter_name == 0 || curve->keys == 0) {
            return EL2D_ERROR_INVALID_ARGUMENT;
        }
        float current = el2d_model_get_parameter(model, curve->parameter_name);
        float sampled = el2d_sample_curve(curve, sample_time);
        float blended = current + (sampled - current) * clamped_weight;
        el2d_result result = el2d_model_set_parameter(model, curve->parameter_name, blended);
        if (result != EL2D_OK) {
            return result;
        }
    }

    return EL2D_OK;
}
