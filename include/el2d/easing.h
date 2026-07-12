#ifndef EL2D_EASING_H
#define EL2D_EASING_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum el2d_easing {
    EL2D_EASING_LINEAR = 0,
    EL2D_EASING_EASE_IN_QUAD = 1,
    EL2D_EASING_EASE_OUT_QUAD = 2,
    EL2D_EASING_EASE_IN_OUT_QUAD = 3,
    EL2D_EASING_EASE_OUT_CUBIC = 4
} el2d_easing;

float el2d_ease(el2d_easing easing, float t);

#ifdef __cplusplus
}
#endif

#endif
