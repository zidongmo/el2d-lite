#ifndef EL2D_MODEL_H
#define EL2D_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "el2d/easing.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EL2D_MAX_PARAMETERS 64u

typedef enum el2d_result {
    EL2D_OK = 0,
    EL2D_ERROR_INVALID_ARGUMENT = 1,
    EL2D_ERROR_NOT_FOUND = 2,
    EL2D_ERROR_CAPACITY = 3
} el2d_result;

typedef struct el2d_parameter_target {
    const char *name;
    float value;
} el2d_parameter_target;

typedef struct el2d_model {
    const char *names[EL2D_MAX_PARAMETERS];
    float values[EL2D_MAX_PARAMETERS];
    float transition_start[EL2D_MAX_PARAMETERS];
    float transition_target[EL2D_MAX_PARAMETERS];
    size_t parameter_count;
    uint32_t transition_elapsed_ms;
    uint32_t transition_duration_ms;
    el2d_easing transition_easing;
    int transition_active;
} el2d_model;

el2d_result el2d_model_init(el2d_model *model, const char *const *parameter_names, size_t parameter_count);
el2d_result el2d_model_set_parameter(el2d_model *model, const char *name, float value);
float el2d_model_get_parameter(const el2d_model *model, const char *name);
el2d_result el2d_model_transition_to(el2d_model *model, const el2d_parameter_target *targets, size_t target_count, uint32_t duration_ms, el2d_easing easing);
void el2d_model_update(el2d_model *model, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif
