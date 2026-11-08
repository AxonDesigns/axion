/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Animation curves (decision 94): CSS easings at known points, springs against a fine numerical
 * integration in the three damping regimes, decay, the transition's validation, and a hash of the
 * curves' bits that CI checks on every platform.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/core/ax_anim.h"

AX_TEST(css_easings_at_known_points)
{
    const float ease[4] = {0.25f, 0.1f, 0.25f, 1.0f};
    const float linear[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const float zeros[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const ax_linear_stop stops[3] = {{0.0f, 0.0f}, {0.5f, 0.8f}, {1.0f, 1.0f}};
    float slope = 0.0f;
    int i;
    /* CSS ease at 0.5 is 0.8024034 (Chrome and Firefox). */
    AX_CHECK_NEAR_F(ax_ease_bezier(ease, 0.5f, &slope), 0.8024034f, 2e-6);
    AX_CHECK_NEAR_F(ax_ease_bezier(ease, 0.0f, NULL), 0.0f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_bezier(ease, 1.0f, NULL), 1.0f, 0.0);
    for (i = 1; i < 20; ++i) {
        const float x = (float)i / 20.0f;
        float s0, a, b;
        AX_CHECK_NEAR_F(ax_ease_bezier(linear, x, NULL), x, 1e-6);
        AX_CHECK_NEAR_F(ax_ease_bezier(zeros, x, NULL), x, 1e-6); /* all zeros is linear */
        /* The slope is the derivative: compare with a central difference. */
        (void)ax_ease_bezier(ease, x, &s0);
        a = ax_ease_bezier(ease, x - 1e-3f, NULL);
        b = ax_ease_bezier(ease, x + 1e-3f, NULL);
        AX_CHECK_NEAR_F(s0, (b - a) / 2e-3f, 2e-3);
    }
    AX_CHECK_NEAR_F(ax_ease_steps(4, AX_STEP_JUMP_END, 0.3f), 0.25f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_steps(4, AX_STEP_JUMP_START, 0.3f), 0.5f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_steps(5, AX_STEP_JUMP_NONE, 0.5f), 0.5f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_steps(3, AX_STEP_JUMP_BOTH, 0.5f), 0.5f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_steps(4, AX_STEP_JUMP_END, 1.0f), 1.0f, 0.0);
    AX_CHECK_NEAR_F(ax_ease_linear(stops, 3, 0.25f, &slope), 0.4f, 1e-6);
    AX_CHECK_NEAR_F(slope, 1.6f, 1e-5);
    AX_CHECK_NEAR_F(ax_ease_linear(stops, 3, 0.75f, &slope), 0.9f, 1e-6);
    AX_CHECK_NEAR_F(slope, 0.4f, 1e-5);
}

/* x'' = -(k x + c x') / m, RK4 with a 10 us step. */
static void integrate(float k, float c, float m, float x0, float v0, float t, double *x, double *v)
{
    double xs = x0, vs = v0;
    const double h = 1e-5;
    long n = (long)(t / h + 0.5), i;
    for (i = 0; i < n; ++i) {
        double k1x = vs, k1v = -(k * xs + c * vs) / m;
        double k2x = vs + 0.5 * h * k1v, k2v = -(k * (xs + 0.5 * h * k1x) + c * k2x) / m;
        double k3x = vs + 0.5 * h * k2v, k3v = -(k * (xs + 0.5 * h * k2x) + c * k3x) / m;
        double k4x = vs + h * k3v, k4v = -(k * (xs + h * k3x) + c * k4x) / m;
        xs += h / 6.0 * (k1x + 2 * k2x + 2 * k3x + k4x);
        vs += h / 6.0 * (k1v + 2 * k2v + 2 * k3v + k4v);
    }
    *x = xs;
    *v = vs;
}

AX_TEST(springs_match_a_numerical_integration_in_every_regime)
{
    /* Under-damped (bounce 0.3), critical (bounce 0), over-damped (bounce -0.5), a physical one. */
    const float bounces[3] = {0.3f, 0.0f, -0.5f};
    int r, i;
    for (r = 0; r < 4; ++r) {
        float k, c, m;
        if (r < 3) {
            ax_spring_perceptual(0.5f, bounces[r], &k, &c, &m);
        } else {
            k = 300.0f;
            c = 10.0f;
            m = 2.0f;
        }
        for (i = 1; i <= 8; ++i) {
            const float t = 0.1f * (float)i;
            float x, v;
            double xe, ve;
            ax_spring_eval(k, c, m, 40.0f, -120.0f, t, &x, &v);
            integrate(k, c, m, 40.0f, -120.0f, t, &xe, &ve);
            AX_CHECK_NEAR_F(x, (float)xe, 2e-3);
            AX_CHECK_NEAR_F(v, (float)ve, 2e-2);
        }
    }
    {
        float k, c, m;
        ax_spring_perceptual(0.4f, 0.0f, &k, &c, &m);
        AX_CHECK_NEAR_F(k, 246.74011f, 1e-3); /* (2 pi / 0.4)^2 */
        AX_CHECK_NEAR_F(c, 31.4159f, 1e-3);   /* critically damped: 2 sqrt(k) */
        AX_CHECK_NEAR_F(m, 1.0f, 0.0);
    }
}

AX_TEST(decay_and_fling)
{
    float x, v;
    ax_decay_eval(4.0f, 10.0f, 0.25f, &x, &v);
    AX_CHECK_NEAR_F(x, 10.0f / 2.7182818f, 1e-4);
    AX_CHECK_NEAR_F(v, -4.0f * x, 1e-4);
    ax_decay_fling(2.0f, 100.0f, 1e3f, &x, &v);
    AX_CHECK_NEAR_F(x, 50.0f, 1e-3); /* v0 / rate */
    AX_CHECK_NEAR_F(v, 0.0f, 1e-3);
}

AX_TEST(transitions_are_validated_and_progress_ends)
{
    ax_transition t;
    ax_curve c;
    float p, rate;
    const ax_linear_stop bad[2] = {{0.5f, 0.0f}, {0.25f, 1.0f}};
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.kind = AX_TRANSITION_TIMING;
    t.duration = 0.5f;
    t.delay = 0.1f;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_OK);
    AX_CHECK(!ax_curve_progress(&c, 0.05f, &p, &rate) && p == 0.0f); /* in the delay */
    AX_CHECK(!ax_curve_progress(&c, 0.35f, &p, &rate));
    AX_CHECK_NEAR_F(p, 0.5f, 1e-6); /* all-zero curve: linear */
    AX_CHECK_NEAR_F(rate, 2.0f, 1e-4);
    AX_CHECK(ax_curve_progress(&c, 0.6f, &p, &rate) && p == 1.0f);
    t.easing = AX_EASING_LINEAR;
    t.stops = bad;
    t.stop_count = 2;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT);
    t.stop_count = AX_TRANSITION_MAX_STOPS + 1u;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT);
    t.easing = AX_EASING_STEPS;
    t.steps = 1;
    t.step_position = AX_STEP_JUMP_NONE;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT);
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.kind = AX_TRANSITION_SPRING;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT); /* no duration */
    t.duration = 0.3f;
    t.bounce = 1.0f;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT);
    t.bounce = 0.0f;
    t.stiffness = 100.0f; /* physical: mass 0 means 1 */
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_OK);
    AX_CHECK(c.mass == 1.0f && c.stiffness == 100.0f);
    t.kind = AX_TRANSITION_DECAY;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT); /* rate 0 */
    t.kind = 9;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_INVALID_ARGUMENT);
    t.kind = AX_TRANSITION_NONE;
    t.struct_size = 8;
    AX_CHECK_RESULT(ax_curve_from_transition(&t, &c), AX_ERR_ABI_MISMATCH);
}

AX_TEST(curves_give_the_same_bits_everywhere)
{
    const ax_curve ease = ax_curve_default_ease();
    const ax_curve spring = ax_curve_default_spring();
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 40; ++i) {
        const float t = 0.013f * (float)i;
        float v[5];
        int k;
        (void)ax_curve_progress(&ease, t, &v[0], &v[1]);
        ax_spring_eval(spring.stiffness, spring.damping, spring.mass, 30.0f, 200.0f, t, &v[2],
                       &v[3]);
        ax_spring_eval(400.0f, 8.0f, 1.0f, -1.0f, 0.0f, t, &v[4], &v[4]);
        for (k = 0; k < 5; ++k) {
            uint32_t bits;
            memcpy(&bits, &v[k], sizeof bits);
            h = (h ^ bits) * 16777619u;
        }
    }
    printf("hash %08x\n", (unsigned)h);
    AX_CHECK_EQ_U(h, 0xa84de1dbu);
}
