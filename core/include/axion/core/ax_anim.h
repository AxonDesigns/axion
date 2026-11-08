/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: animation curves (decision 94). Pure functions of their parameters and the time,
 * using base's deterministic elementary functions, so the same sequence of times gives the same
 * bits on every platform.
 *
 * A curve is the compact, validated form of an ax_transition (ax_txn.h), as transactions carry
 * it and the scene stores it:
 *   - TIMING: progress from 0 to 1 over `duration` through an easing (cubic Bezier, steps, or
 *     linear with stops); the value moves by progress;
 *   - SPRING: a damped spring (stiffness, damping, mass; a perceptual duration and bounce are
 *     resolved to them when recorded, as SwiftUI does), solved in closed form;
 *   - DECAY: the remaining distance shrinks exponentially at `rate`.
 * Every curve is used as an offset that decays to zero: an animation's value is its target plus an
 * offset, so the target may move (a retarget, a relayout every frame) without a jump.
 */
#ifndef AX_CORE_ANIM_H
#define AX_CORE_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_txn.h"
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

typedef struct ax_curve {
    uint8_t kind;          /* ax_transition_kind */
    uint8_t easing;        /* ax_easing (TIMING) */
    uint8_t step_position; /* ax_step_position (STEPS) */
    uint8_t stop_count;    /* LINEAR */
    uint32_t steps;        /* STEPS */
    float delay;           /* seconds */
    float duration;        /* TIMING: seconds */
    float bezier[4];       /* CUBIC_BEZIER: x1 y1 x2 y2 */
    float stiffness;       /* SPRING, resolved */
    float damping;
    float mass;
    float rate; /* DECAY: 1/s */
    ax_linear_stop stops[AX_TRANSITION_MAX_STOPS];
} ax_curve;

/*
 * Validates an ax_transition and converts it to a curve (a perceptual spring resolved to its
 * stiffness and damping, mass 1). AX_ERR_INVALID_ARGUMENT with ax_last_error naming the field.
 * thread: any
 */
AX_API ax_result ax_curve_from_transition(const ax_transition *t, ax_curve *out);

/* The built-in curves (decision 94's defaults). thread: any */
AX_API ax_curve ax_curve_default_spring(void); /* perceptual, 0.4 s, bounce 0 */
AX_API ax_curve ax_curve_default_ease(void);   /* timing, 0.2 s, cubic-bezier(.25, .1, .25, 1) */
AX_API ax_curve ax_curve_none(void);

/* The animator's counters (ax_world_get_anim_counters, ax_frame.h). */
typedef struct ax_anim_counters {
    uint64_t started;    /* animations started from rest */
    uint64_t retargeted; /* running animations given a new target */
    uint64_t finished;   /* channels that came to rest */
    uint64_t snapped;    /* changes made at once (NONE, not animated, reduce motion) */
    uint64_t folded;     /* timing curves folded into the next, past three on a channel */
    uint64_t samples;    /* node samples */
    uint32_t active;     /* nodes animating after the last sample */
} ax_anim_counters;

/* ---- Easing (TIMING) ---------------------------------------------------------------------------
 */

/* CSS cubic-bezier(x1, y1, x2, y2) at input x in [0, 1]; *slope (may be NULL) is dy/dx. */
AX_API float ax_ease_bezier(const float c[4], float x, float *slope);

/* CSS steps(n, position) at input x in [0, 1]. Its slope is 0. */
AX_API float ax_ease_steps(uint32_t steps, uint32_t position, float x);

/* CSS linear() with explicit inputs: piecewise linear, flat outside the stops. */
AX_API float ax_ease_linear(const ax_linear_stop *stops, uint32_t count, float x, float *slope);

/* A timing curve's progress at t seconds after its start (delay included), and its derivative per
 * second. Done (true) once the progress is final. */
AX_API bool ax_curve_progress(const ax_curve *c, float t, float *progress, float *rate);

/* ---- Springs and decay --------------------------------------------------------------------------
 */

/* The perceptual spring (SwiftUI's model): mass 1, stiffness (2 pi / duration)^2, damping ratio
 * 1 - bounce (bounce >= 0) or 1 / (1 + bounce) (bounce < 0). */
AX_API void ax_spring_perceptual(float duration, float bounce, float *stiffness, float *damping,
                                 float *mass);

/* The displacement from rest and the velocity t seconds after starting at displacement x0 with
 * velocity v0 (closed form: under-, critically or over-damped). */
AX_API void ax_spring_eval(float stiffness, float damping, float mass, float x0, float v0, float t,
                           float *x, float *v);

/* DECAY toward rest: x0 e^(-rate t) and its velocity. */
AX_API void ax_decay_eval(float rate, float x0, float t, float *x, float *v);

/* A fling with friction (scrolling, M2): the distance travelled and the velocity t seconds after
 * leaving at velocity v0, slowing at `rate`: v0 (1 - e^(-rate t)) / rate. */
AX_API void ax_decay_fling(float rate, float v0, float t, float *x, float *v);

AX_EXTERN_C_END

#endif /* AX_CORE_ANIM_H */
