#include "el2d/easing.h"

static float el2d_clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float el2d_ease(el2d_easing easing, float t) {
    float x = el2d_clamp01(t);
    switch (easing) {
    case EL2D_EASING_EASE_IN_QUAD:
        return x * x;
    case EL2D_EASING_EASE_OUT_QUAD:
        return 1.0f - ((1.0f - x) * (1.0f - x));
    case EL2D_EASING_EASE_IN_OUT_QUAD:
        if (x < 0.5f) {
            return 2.0f * x * x;
        }
        return 1.0f - ((-2.0f * x + 2.0f) * (-2.0f * x + 2.0f) * 0.5f);
    case EL2D_EASING_EASE_OUT_CUBIC: {
        float inv = 1.0f - x;
        return 1.0f - inv * inv * inv;
    }
    case EL2D_EASING_LINEAR:
    default:
        return x;
    }
}
