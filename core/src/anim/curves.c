/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Animation curves (ax_anim.h, decision 94): validation, easing, springs and decay. Only base's
 * deterministic elementary functions and IEEE operations, so the results are the same bits
 * everywhere.
 */
#include <math.h>
#include <string.h>

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_math.h>
#include <axion/core/ax_anim.h>

#define PI_F 3.14159265f

static bool finite_nonneg(float v)
{
    return v - v == 0.0f && v >= 0.0f;
}

static bool finite(float v)
{
    return v - v == 0.0f;
}

void ax_spring_perceptual(float duration, float bounce, float *stiffness, float *damping,
                          float *mass)
{
    const float w = 2.0f * PI_F / duration;
    const float zeta = bounce >= 0.0f ? 1.0f - bounce : 1.0f / (1.0f + bounce);
    *mass = 1.0f;
    *stiffness = w * w;
    *damping = 2.0f * zeta * w; /* 2 zeta sqrt(k m), m = 1 */
}

ax_curve ax_curve_none(void)
{
    ax_curve c;
    memset(&c, 0, sizeof c);
    c.kind = AX_TRANSITION_NONE;
    return c;
}

ax_curve ax_curve_default_spring(void)
{
    ax_curve c;
    memset(&c, 0, sizeof c);
    c.kind = AX_TRANSITION_SPRING;
    c.duration = 0.4f;
    ax_spring_perceptual(0.4f, 0.0f, &c.stiffness, &c.damping, &c.mass);
    return c;
}

ax_curve ax_curve_default_ease(void)
{
    ax_curve c;
    memset(&c, 0, sizeof c);
    c.kind = AX_TRANSITION_TIMING;
    c.easing = AX_EASING_CUBIC_BEZIER;
    c.duration = 0.2f;
    c.bezier[0] = 0.25f;
    c.bezier[1] = 0.1f;
    c.bezier[2] = 0.25f;
    c.bezier[3] = 1.0f;
    return c;
}

ax_result ax_curve_from_transition(const ax_transition *t, ax_curve *out)
{
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (t == NULL || t->struct_size < sizeof(ax_transition)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "transition: NULL, or struct_size smaller than %u",
                            (unsigned)sizeof(ax_transition));
    }
    if (t->kind < AX_TRANSITION_NONE || t->kind > AX_TRANSITION_DEFAULT) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "transition: unknown kind %u",
                            (unsigned)t->kind);
    }
    if (!finite_nonneg(t->delay) || !finite_nonneg(t->duration)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "transition: delay and duration must be finite and not negative");
    }
    out->kind = (uint8_t)t->kind;
    out->delay = t->delay;
    out->duration = t->duration;
    switch (t->kind) {
    case AX_TRANSITION_TIMING:
        out->easing = (uint8_t)t->easing;
        switch (t->easing) {
        case AX_EASING_CUBIC_BEZIER:
            for (i = 0; i < 4; ++i) {
                if (!finite(t->curve[i])) {
                    return ax_set_error(AX_ERR_INVALID_ARGUMENT, "transition: curve not finite");
                }
                out->bezier[i] = t->curve[i];
            }
            if (t->curve[0] < 0.0f || t->curve[0] > 1.0f || t->curve[2] < 0.0f ||
                t->curve[2] > 1.0f) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT, "transition: curve x outside 0..1");
            }
            break;
        case AX_EASING_STEPS:
            if (t->step_position > AX_STEP_JUMP_BOTH || t->steps < 1u ||
                (t->step_position == AX_STEP_JUMP_NONE && t->steps < 2u) || t->steps > 1000000u) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "transition: steps must be at least 1 (2 with jump-none)");
            }
            out->steps = t->steps;
            out->step_position = (uint8_t)t->step_position;
            break;
        case AX_EASING_LINEAR:
            if (t->stops == NULL || t->stop_count < 2u || t->stop_count > AX_TRANSITION_MAX_STOPS) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "transition: linear() needs 2 to %u stops",
                                    (unsigned)AX_TRANSITION_MAX_STOPS);
            }
            for (i = 0; i < t->stop_count; ++i) {
                const ax_linear_stop s = t->stops[i];
                if (!finite(s.input) || !finite(s.output) || s.input < 0.0f || s.input > 1.0f ||
                    (i > 0 && s.input < t->stops[i - 1u].input)) {
                    return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                        "transition: linear() stop %u: inputs in 0..1, not "
                                        "decreasing, finite outputs",
                                        (unsigned)i);
                }
                out->stops[i] = s;
            }
            out->stop_count = (uint8_t)t->stop_count;
            break;
        default:
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "transition: unknown easing %u",
                                (unsigned)t->easing);
        }
        break;
    case AX_TRANSITION_SPRING:
        if (t->stiffness > 0.0f) {
            const float mass = t->mass == 0.0f ? 1.0f : t->mass;
            if (!finite(t->stiffness) || !finite_nonneg(t->damping) || !finite(mass) ||
                !(mass > 0.0f)) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "transition: a physical spring needs stiffness > 0, "
                                    "damping >= 0 and mass > 0");
            }
            out->stiffness = t->stiffness;
            out->damping = t->damping;
            out->mass = mass;
        } else {
            if (!(t->duration > 0.0f) || !finite(t->bounce) || t->bounce <= -1.0f ||
                t->bounce >= 1.0f || t->stiffness < 0.0f) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "transition: a perceptual spring needs duration > 0 and "
                                    "-1 < bounce < 1");
            }
            ax_spring_perceptual(t->duration, t->bounce, &out->stiffness, &out->damping,
                                 &out->mass);
        }
        break;
    case AX_TRANSITION_DECAY:
        if (!finite(t->rate) || !(t->rate > 0.0f)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "transition: decay needs rate > 0");
        }
        out->rate = t->rate;
        break;
    default: /* NONE, DEFAULT */
        break;
    }
    return AX_OK;
}

/* ---- Easing --------------------------------------------------------------------------------- */

static float bez(float p1, float p2, float u)
{
    /* 3(1-u)^2 u p1 + 3(1-u) u^2 p2 + u^3, as ((a u + b) u + c) u */
    const float a = 1.0f + 3.0f * p1 - 3.0f * p2;
    const float b = 3.0f * p2 - 6.0f * p1;
    const float c = 3.0f * p1;
    return ((a * u + b) * u + c) * u;
}

static float bez_d(float p1, float p2, float u)
{
    const float a = 1.0f + 3.0f * p1 - 3.0f * p2;
    const float b = 3.0f * p2 - 6.0f * p1;
    const float c = 3.0f * p1;
    return (3.0f * a * u + 2.0f * b) * u + c;
}

float ax_ease_bezier(const float c[4], float x, float *slope)
{
    float u = x, lo = 0.0f, hi = 1.0f;
    int i;
    if (x <= 0.0f || x >= 1.0f) {
        float u0 = x <= 0.0f ? 0.0f : 1.0f;
        if (slope != NULL) {
            float dx = bez_d(c[0], c[2], u0);
            *slope = dx != 0.0f ? bez_d(c[1], c[3], u0) / dx : 0.0f;
        }
        return x <= 0.0f ? 0.0f : 1.0f;
    }
    /* Newton from u = x, then bisection if it did not converge: a fixed sequence of operations. */
    for (i = 0; i < 8; ++i) {
        float err = bez(c[0], c[2], u) - x;
        float d = bez_d(c[0], c[2], u);
        if (fabsf(err) < 1e-7f) {
            break;
        }
        if (fabsf(d) < 1e-6f) {
            i = 8;
            break;
        }
        u -= err / d;
        if (u < 0.0f || u > 1.0f) {
            i = 8;
            break;
        }
    }
    if (i == 8) {
        for (i = 0; i < 30; ++i) {
            u = 0.5f * (lo + hi);
            if (bez(c[0], c[2], u) < x) {
                lo = u;
            } else {
                hi = u;
            }
        }
        u = 0.5f * (lo + hi);
    }
    if (slope != NULL) {
        float dx = bez_d(c[0], c[2], u);
        *slope = dx != 0.0f ? bez_d(c[1], c[3], u) / dx : 0.0f;
    }
    return bez(c[1], c[3], u);
}

float ax_ease_steps(uint32_t steps, uint32_t position, float x)
{
    float jumps = (float)steps;
    float step;
    if (position == AX_STEP_JUMP_NONE) {
        jumps = (float)steps - 1.0f;
    } else if (position == AX_STEP_JUMP_BOTH) {
        jumps = (float)steps + 1.0f;
    }
    if (x < 0.0f) {
        x = 0.0f;
    }
    step = floorf(x * (float)steps);
    if (position == AX_STEP_JUMP_START || position == AX_STEP_JUMP_BOTH) {
        step += 1.0f;
    }
    if (step > jumps) {
        step = jumps;
    }
    return step / jumps;
}

float ax_ease_linear(const ax_linear_stop *stops, uint32_t count, float x, float *slope)
{
    uint32_t i;
    if (slope != NULL) {
        *slope = 0.0f;
    }
    if (x <= stops[0].input) {
        return stops[0].output;
    }
    for (i = 1; i < count; ++i) {
        if (x <= stops[i].input) {
            const float span = stops[i].input - stops[i - 1u].input;
            const float k = span > 0.0f ? (stops[i].output - stops[i - 1u].output) / span : 0.0f;
            if (slope != NULL) {
                *slope = k;
            }
            return span > 0.0f ? stops[i - 1u].output + (x - stops[i - 1u].input) * k
                               : stops[i].output;
        }
    }
    return stops[count - 1u].output;
}

bool ax_curve_progress(const ax_curve *c, float t, float *progress, float *rate)
{
    float x, slope = 0.0f, p;
    t -= c->delay;
    if (t <= 0.0f) {
        *progress = 0.0f;
        *rate = 0.0f;
        return false;
    }
    if (!(c->duration > 0.0f) || t >= c->duration) {
        *progress = 1.0f;
        *rate = 0.0f;
        return true;
    }
    x = t / c->duration;
    switch (c->easing) {
    case AX_EASING_STEPS:
        p = ax_ease_steps(c->steps, c->step_position, x);
        break;
    case AX_EASING_LINEAR:
        p = ax_ease_linear(c->stops, c->stop_count, x, &slope);
        break;
    default:
        p = ax_ease_bezier(c->bezier, x, &slope);
        break;
    }
    *progress = p;
    *rate = slope / c->duration;
    return false;
}

/* ---- Springs and decay ---------------------------------------------------------------------- */

void ax_spring_eval(float k, float c, float m, float x0, float v0, float t, float *x, float *v)
{
    const float w0 = sqrtf(k / m);
    const float zeta = c / (2.0f * sqrtf(k * m));
    if (t <= 0.0f) {
        *x = x0;
        *v = v0;
        return;
    }
    if (zeta < 0.9999f) {
        const float a = zeta * w0;
        const float wd = w0 * sqrtf(1.0f - zeta * zeta);
        const float e = ax_expf(-a * t);
        float s, co;
        const float b = (v0 + a * x0) / wd;
        ax_sin_cos(wd * t, &s, &co);
        *x = e * (x0 * co + b * s);
        *v = e * (v0 * co - ((a * v0 + w0 * w0 * x0) / wd) * s);
    } else if (zeta <= 1.0001f) {
        const float e = ax_expf(-w0 * t);
        const float b = v0 + w0 * x0;
        *x = e * (x0 + b * t);
        *v = e * (v0 - w0 * b * t);
    } else {
        const float root = sqrtf(zeta * zeta - 1.0f);
        const float r1 = -w0 * (zeta - root);
        const float r2 = -w0 * (zeta + root);
        const float c2 = (v0 - r1 * x0) / (r2 - r1);
        const float c1 = x0 - c2;
        const float e1 = ax_expf(r1 * t), e2 = ax_expf(r2 * t);
        *x = c1 * e1 + c2 * e2;
        *v = r1 * c1 * e1 + r2 * c2 * e2;
    }
}

void ax_decay_eval(float rate, float x0, float t, float *x, float *v)
{
    const float e = t > 0.0f ? ax_expf(-rate * t) : 1.0f;
    *x = x0 * e;
    *v = -rate * x0 * e;
}

void ax_decay_fling(float rate, float v0, float t, float *x, float *v)
{
    const float e = t > 0.0f ? ax_expf(-rate * t) : 1.0f;
    *x = v0 * (1.0f - e) / rate;
    *v = v0 * e;
}
