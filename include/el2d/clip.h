#ifndef EL2D_CLIP_H
#define EL2D_CLIP_H

#include <stddef.h>
#include <stdint.h>

#include "el2d/easing.h"
#include "el2d/model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el2d_curve_key {
    uint32_t time_ms;
    float value;
} el2d_curve_key;

typedef struct el2d_curve {
    const char *parameter_name;
    const el2d_curve_key *keys;
    size_t key_count;
    el2d_easing easing;
} el2d_curve;

typedef struct el2d_clip {
    const char *name;
    const el2d_curve *curves;
    size_t curve_count;
    uint32_t duration_ms;
} el2d_clip;

el2d_result el2d_clip_apply(const el2d_clip *clip, el2d_model *model, uint32_t time_ms, float weight);

#ifdef __cplusplus
}
#endif

#endif
