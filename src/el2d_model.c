#include "el2d/model.h"

#include <string.h>

static int el2d_find_parameter(const el2d_model *model, const char *name) {
    if (model == 0 || name == 0) {
        return -1;
    }
    for (size_t i = 0; i < model->parameter_count; ++i) {
        if (strcmp(model->names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

el2d_result el2d_model_init(el2d_model *model, const char *const *parameter_names, size_t parameter_count) {
    if (model == 0 || parameter_names == 0) {
        return EL2D_ERROR_INVALID_ARGUMENT;
    }
    if (parameter_count > EL2D_MAX_PARAMETERS) {
        return EL2D_ERROR_CAPACITY;
    }

    memset(model, 0, sizeof(*model));
    model->parameter_count = parameter_count;
    for (size_t i = 0; i < parameter_count; ++i) {
        if (parameter_names[i] == 0) {
            return EL2D_ERROR_INVALID_ARGUMENT;
        }
        model->names[i] = parameter_names[i];
    }
    return EL2D_OK;
}

el2d_result el2d_model_set_parameter(el2d_model *model, const char *name, float value) {
    int index = el2d_find_parameter(model, name);
    if (index < 0) {
        return EL2D_ERROR_NOT_FOUND;
    }
    model->values[index] = value;
    model->transition_start[index] = value;
    model->transition_target[index] = value;
    return EL2D_OK;
}

float el2d_model_get_parameter(const el2d_model *model, const char *name) {
    int index = el2d_find_parameter(model, name);
    if (index < 0) {
        return 0.0f;
    }
    return model->values[index];
}

el2d_result el2d_model_transition_to(el2d_model *model, const el2d_parameter_target *targets, size_t target_count, uint32_t duration_ms, el2d_easing easing) {
    if (model == 0 || targets == 0) {
        return EL2D_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < model->parameter_count; ++i) {
        model->transition_start[i] = model->values[i];
        model->transition_target[i] = model->values[i];
    }

    for (size_t i = 0; i < target_count; ++i) {
        int index = el2d_find_parameter(model, targets[i].name);
        if (index < 0) {
            return EL2D_ERROR_NOT_FOUND;
        }
        model->transition_target[index] = targets[i].value;
    }

    model->transition_elapsed_ms = 0u;
    model->transition_duration_ms = duration_ms;
    model->transition_easing = easing;
    model->transition_active = duration_ms > 0u;

    if (duration_ms == 0u) {
        for (size_t i = 0; i < model->parameter_count; ++i) {
            model->values[i] = model->transition_target[i];
        }
    }

    return EL2D_OK;
}

void el2d_model_update(el2d_model *model, uint32_t dt_ms) {
    if (model == 0 || !model->transition_active) {
        return;
    }

    model->transition_elapsed_ms += dt_ms;
    if (model->transition_elapsed_ms >= model->transition_duration_ms) {
        model->transition_elapsed_ms = model->transition_duration_ms;
        model->transition_active = 0;
    }

    float t = (float)model->transition_elapsed_ms / (float)model->transition_duration_ms;
    float eased = el2d_ease(model->transition_easing, t);
    for (size_t i = 0; i < model->parameter_count; ++i) {
        float start = model->transition_start[i];
        float target = model->transition_target[i];
        model->values[i] = start + (target - start) * eased;
    }
}
