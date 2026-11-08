/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_math.h"

#define PI_F 3.14159265358979f

static void check_affine_near(ax_affine2 m, ax_affine2 e, float tol, const char *what)
{
    if (fabsf(m.a - e.a) > tol || fabsf(m.b - e.b) > tol || fabsf(m.c - e.c) > tol ||
        fabsf(m.d - e.d) > tol || fabsf(m.tx - e.tx) > tol || fabsf(m.ty - e.ty) > tol) {
        ax_test_fail(__FILE__, __LINE__,
                     "%s: got (%g %g %g %g %g %g), expected (%g %g %g %g %g %g)", what, (double)m.a,
                     (double)m.b, (double)m.c, (double)m.d, (double)m.tx, (double)m.ty, (double)e.a,
                     (double)e.b, (double)e.c, (double)e.d, (double)e.tx, (double)e.ty);
    }
}

AX_TEST(rect_contains_is_half_open)
{
    const ax_rect r = ax_rect_make(10.0f, 20.0f, 30.0f, 40.0f);
    AX_CHECK(ax_rect_contains(r, ax_vec2_make(10.0f, 20.0f)));
    AX_CHECK(ax_rect_contains(r, ax_vec2_make(39.99f, 59.99f)));
    AX_CHECK(!ax_rect_contains(r, ax_vec2_make(40.0f, 30.0f)));
    AX_CHECK(!ax_rect_contains(r, ax_vec2_make(20.0f, 60.0f)));
    AX_CHECK(!ax_rect_contains(r, ax_vec2_make(9.99f, 30.0f)));
    AX_CHECK(ax_rect_is_empty(ax_rect_make(0, 0, 0, 5)));
    AX_CHECK(ax_rect_is_empty(ax_rect_make(0, 0, 5, -1)));
    AX_CHECK(ax_rect_is_empty(ax_rect_make(0, 0, NAN, 5)));
    AX_CHECK(!ax_rect_is_empty(r));
}

AX_TEST(rect_union_ignores_empty)
{
    const ax_rect a = ax_rect_make(0.0f, 0.0f, 10.0f, 10.0f);
    const ax_rect b = ax_rect_make(5.0f, -5.0f, 20.0f, 5.0f);
    ax_rect u = ax_rect_union(a, b);
    AX_CHECK_NEAR_F(u.x, 0.0f, 0.0f);
    AX_CHECK_NEAR_F(u.y, -5.0f, 0.0f);
    AX_CHECK_NEAR_F(u.w, 25.0f, 0.0f);
    AX_CHECK_NEAR_F(u.h, 15.0f, 0.0f);
    u = ax_rect_union(ax_rect_make(100, 100, 0, 0), a);
    AX_CHECK_NEAR_F(u.x, 0.0f, 0.0f);
    AX_CHECK_NEAR_F(u.w, 10.0f, 0.0f);
}

AX_TEST(affine_apply_and_compose)
{
    /* Scale 2, then translate by (10, 5): mul(translate, scale). */
    const ax_affine2 m = ax_affine2_mul(ax_affine2_translate(10.0f, 5.0f), ax_affine2_scale(2, 2));
    const ax_vec2 p = ax_affine2_apply(m, ax_vec2_make(3.0f, 4.0f));
    const ax_vec2 v = ax_affine2_apply_vector(m, ax_vec2_make(3.0f, 4.0f));
    AX_CHECK_NEAR_F(p.x, 16.0f, 0.0f);
    AX_CHECK_NEAR_F(p.y, 13.0f, 0.0f);
    AX_CHECK_NEAR_F(v.x, 6.0f, 0.0f);
    AX_CHECK_NEAR_F(v.y, 8.0f, 0.0f);
    /* Composition is associative and applies the right operand first. */
    {
        const ax_affine2 r = ax_affine2_rotate(0.3f);
        const ax_affine2 left = ax_affine2_mul(ax_affine2_mul(m, r), ax_affine2_scale(1.5f, 0.5f));
        const ax_affine2 right = ax_affine2_mul(m, ax_affine2_mul(r, ax_affine2_scale(1.5f, 0.5f)));
        const ax_vec2 q = ax_vec2_make(-7.0f, 2.5f);
        const ax_vec2 step = ax_affine2_apply(
            m, ax_affine2_apply(r, ax_affine2_apply(ax_affine2_scale(1.5f, 0.5f), q)));
        const ax_vec2 once = ax_affine2_apply(left, q);
        check_affine_near(left, right, 1e-5f, "associativity");
        AX_CHECK_NEAR_F(once.x, step.x, 1e-4);
        AX_CHECK_NEAR_F(once.y, step.y, 1e-4);
    }
}

AX_TEST(rotation_turns_clockwise_on_screen)
{
    /* y points down, so +90 degrees takes +x to +y (down on screen). */
    const ax_vec2 p = ax_affine2_apply(ax_affine2_rotate(PI_F / 2.0f), ax_vec2_make(1.0f, 0.0f));
    AX_CHECK_NEAR_F(p.x, 0.0f, 1e-6);
    AX_CHECK_NEAR_F(p.y, 1.0f, 1e-6);
    AX_CHECK_NEAR_F(ax_affine2_determinant(ax_affine2_rotate(1.234f)), 1.0f, 1e-6);
}

AX_TEST(affine_inverse)
{
    const ax_affine2 m = ax_affine2_mul(
        ax_affine2_translate(13.25f, -4.5f),
        ax_affine2_mul(ax_affine2_rotate(0.7f), ax_affine2_make(1.325f, 0.2f, -0.1f, 2.0f, 3, 4)));
    ax_affine2 inv;
    ax_affine2 untouched = ax_affine2_translate(1.0f, 2.0f);
    AX_REQUIRE(ax_affine2_invert(m, &inv));
    check_affine_near(ax_affine2_mul(inv, m), ax_affine2_identity(), 1e-5f, "inv * m");
    check_affine_near(ax_affine2_mul(m, inv), ax_affine2_identity(), 1e-5f, "m * inv");

    /* Singular and non-finite transforms are rejected and leave the output alone. */
    AX_CHECK(!ax_affine2_invert(ax_affine2_scale(0.0f, 3.0f), &untouched));
    AX_CHECK(!ax_affine2_invert(ax_affine2_make(1, 2, 2, 4, 0, 0), &untouched));
    AX_CHECK(!ax_affine2_invert(ax_affine2_make(INFINITY, 0, 0, 1, 0, 0), &untouched));
    check_affine_near(untouched, ax_affine2_translate(1.0f, 2.0f), 0.0f, "untouched");
}

AX_TEST(transformed_rect_bounds)
{
    const ax_rect r = ax_rect_make(-1.0f, -1.0f, 2.0f, 2.0f);
    ax_rect b = ax_affine2_transform_rect(ax_affine2_rotate(PI_F / 4.0f), r);
    AX_CHECK_NEAR_F(b.x, -sqrtf(2.0f), 1e-5);
    AX_CHECK_NEAR_F(b.y, -sqrtf(2.0f), 1e-5);
    AX_CHECK_NEAR_F(b.w, 2.0f * sqrtf(2.0f), 1e-5);
    AX_CHECK_NEAR_F(b.h, 2.0f * sqrtf(2.0f), 1e-5);
    /* A mirroring scale still gives positive extents. */
    b = ax_affine2_transform_rect(ax_affine2_scale(-2.0f, 1.0f), ax_rect_make(1, 2, 3, 4));
    AX_CHECK_NEAR_F(b.x, -8.0f, 0.0f);
    AX_CHECK_NEAR_F(b.w, 6.0f, 0.0f);
    AX_CHECK_NEAR_F(b.y, 2.0f, 0.0f);
    AX_CHECK_NEAR_F(b.h, 4.0f, 0.0f);
}

AX_TEST(srgb_transfer_reference_values)
{
    AX_CHECK_NEAR_F(ax_srgb_to_linear(0.0f), 0.0f, 0.0f);
    AX_CHECK_NEAR_F(ax_srgb_to_linear(1.0f), 1.0f, 1e-6);
    AX_CHECK_NEAR_F(ax_srgb_to_linear(0.5f), 0.214041144f, 1e-6);
    AX_CHECK_NEAR_F(ax_srgb_to_linear(0.04045f), 0.04045f / 12.92f, 1e-7);
    AX_CHECK_NEAR_F(ax_linear_to_srgb(0.214041144f), 0.5f, 1e-6);
    AX_CHECK_NEAR_F(ax_linear_to_srgb(0.0031308f), 0.0031308f * 12.92f, 1e-7);
    /* Extended range: odd-symmetric below 0, the same curve above 1. */
    AX_CHECK_NEAR_F(ax_srgb_to_linear(-0.5f), -0.214041144f, 1e-6);
    AX_CHECK_NEAR_F(ax_linear_to_srgb(-0.214041144f), -0.5f, 1e-6);
    AX_CHECK_NEAR_F(ax_srgb_to_linear(1.5f), powf(1.555f / 1.055f, 2.4f), 1e-5);
    AX_CHECK(ax_linear_to_srgb(4.0f) > 1.0f);
}

AX_TEST(srgb8_round_trips_exactly)
{
    /* Every 8-bit component value, opaque and translucent, comes back to the same byte. */
    uint32_t v, a;
    for (a = 1; a <= 255; a += 254 / 6) {
        for (v = 0; v <= 255; ++v) {
            const ax_color c = ax_color_from_srgb8((a << 24) | (v << 16) | (v << 8) | v);
            const ax_color e = ax_color_to_srgb_premultiplied(c);
            const float alpha = (float)a / 255.0f;
            AX_CHECK_NEAR_F(c.a, alpha, 0.0f);
            AX_REQUIRE(lroundf(e.r / e.a * 255.0f) == (long)v);
            AX_REQUIRE(lroundf(e.g / e.a * 255.0f) == (long)v);
            AX_REQUIRE(lroundf(e.b / e.a * 255.0f) == (long)v);
        }
    }
    /* Premultiplied: 50% white is (0.5, 0.5, 0.5, 0.5) in linear light too (white is 1.0). */
    {
        const ax_color c = ax_color_from_srgb8(0x80FFFFFFu);
        AX_CHECK_NEAR_F(c.r, 128.0f / 255.0f, 1e-6);
        AX_CHECK_NEAR_F(c.a, 128.0f / 255.0f, 1e-6);
    }
}

AX_TEST(premultiplied_round_trip_extended_and_alpha_zero)
{
    /* sRGB-encoded premultiplied -> linear -> back, including alpha 0 (additive light), negative
     * components (outside the sRGB gamut) and components above 1 (HDR). */
    static const float comps[] = {-2.0f, -1.0f, -0.25f, -0.001f, 0.0f, 0.002f,
                                  0.04f, 0.3f,  0.5f,   1.0f,    1.7f, 6.0f};
    static const float alphas[] = {0.0f, 0.001f, 0.25f, 0.5f, 1.0f};
    size_t i, j;
    for (j = 0; j < AX_COUNTOF(alphas); ++j) {
        for (i = 0; i < AX_COUNTOF(comps); ++i) {
            const float a = alphas[j];
            const float straight = comps[i];
            const ax_color enc = ax_color_make(a > 0.0f ? straight * a : straight,
                                               a > 0.0f ? -straight * a : -straight, 0.5f * a, a);
            const ax_color lin = ax_color_from_srgb_premultiplied(enc);
            const ax_color back = ax_color_to_srgb_premultiplied(lin);
            const float tol = 2e-6f * (1.0f + fabsf(enc.r));
            AX_CHECK_NEAR_F(back.r, enc.r, tol);
            AX_CHECK_NEAR_F(back.g, enc.g, tol);
            AX_CHECK_NEAR_F(back.b, enc.b, 2e-6);
            AX_CHECK_NEAR_F(back.a, enc.a, 0.0f);
            /* Signs survive in both directions. */
            AX_CHECK((lin.r < 0.0f) == (enc.r < 0.0f));
            AX_CHECK((lin.g < 0.0f) == (enc.g < 0.0f));
        }
    }
}

AX_TEST(straight_srgb_to_premultiplied_linear)
{
    const ax_color c = ax_color_from_srgb(1.0f, 0.5f, 0.0f, 0.5f);
    AX_CHECK_NEAR_F(c.r, 0.5f, 1e-6);
    AX_CHECK_NEAR_F(c.g, 0.214041144f * 0.5f, 1e-6);
    AX_CHECK_NEAR_F(c.b, 0.0f, 0.0f);
    AX_CHECK_NEAR_F(c.a, 0.5f, 0.0f);
    {
        const ax_color s = ax_color_scale(c, 0.5f);
        AX_CHECK_NEAR_F(s.r, 0.25f, 1e-6);
        AX_CHECK_NEAR_F(s.a, 0.25f, 0.0f);
    }
}

/* ---- Deterministic elementary functions (decision 81) ---------------------------------------- */

AX_TEST(rotation_matches_sin_and_cos_within_a_few_ulps)
{
    double worst = 0.0;
    int i;
    for (i = -20000; i <= 20000; ++i) {
        const float x = (float)i * 0.000731f; /* about +-14.6 rad */
        const ax_affine2 m = ax_affine2_rotate(x);
        const double es = fabs((double)m.b - sin((double)x));
        const double ec = fabs((double)m.a - cos((double)x));
        worst = es > worst ? es : worst;
        worst = ec > worst ? ec : worst;
        AX_CHECK(m.c == -m.b && m.d == m.a);
    }
    printf("rotation: worst absolute error %.3g\n", worst);
    AX_CHECK(worst < 1.5e-7);
    /* Exact where it matters. */
    AX_CHECK(ax_affine2_rotate(0.0f).a == 1.0f && ax_affine2_rotate(0.0f).b == 0.0f);
}

AX_TEST(srgb_transfer_functions_match_the_formulas)
{
    double worst_dec = 0.0, worst_enc = 0.0;
    int i;
    for (i = 0; i <= 100000; ++i) {
        const float x = (float)i / 100000.0f;
        const double xd = (double)x;
        const double dec = xd <= 0.04045 ? xd / 12.92 : pow((xd + 0.055) / 1.055, 2.4);
        const double enc = xd <= 0.0031308 ? xd * 12.92 : 1.055 * pow(xd, 1.0 / 2.4) - 0.055;
        const double ed = fabs((double)ax_srgb_to_linear(x) - dec);
        const double ee = fabs((double)ax_linear_to_srgb(x) - enc);
        worst_dec = ed > worst_dec ? ed : worst_dec;
        worst_enc = ee > worst_enc ? ee : worst_enc;
    }
    printf("sRGB: worst decode error %.3g, encode error %.3g\n", worst_dec, worst_enc);
    AX_CHECK(worst_dec < 3e-7);
    AX_CHECK(worst_enc < 3e-7);
    /* Extended range, signs and the ends. */
    AX_CHECK(fabs((double)ax_srgb_to_linear(2.0f) - pow(2.055 / 1.055, 2.4)) < 1e-5);
    AX_CHECK(ax_srgb_to_linear(-0.5f) == -ax_srgb_to_linear(0.5f));
    AX_CHECK(ax_linear_to_srgb(1.0f) > 0.9999998f && ax_linear_to_srgb(1.0f) < 1.0000002f);
    AX_CHECK(ax_linear_to_srgb(0.0f) == 0.0f);
}

/* Bit patterns that must not change across platforms or compilers: computed once, here, and
 * checked everywhere CI runs (a change means the IR of every rotated or sRGB-specified node
 * changes; update the constants only together with the goldens). */
AX_TEST(elementary_functions_give_the_same_bits_everywhere)
{
    const float in[4] = {0.3f, 1.0f, 2.5f, -0.7f};
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < 4; ++i) {
        const ax_affine2 m = ax_affine2_rotate(in[i]);
        const float v[4] = {m.a, m.b, ax_srgb_to_linear(in[i] * 0.37f + 0.4f),
                            ax_linear_to_srgb(in[i] * 0.21f + 0.3f)};
        int k;
        for (k = 0; k < 4; ++k) {
            uint32_t bits;
            memcpy(&bits, &v[k], sizeof bits);
            h = (h ^ bits) * 16777619u;
        }
    }
    printf("hash %08x\n", (unsigned)h);
    AX_CHECK_EQ_U(h, 0x028cc3c1u);
}

/* The deterministic elementary functions against libm (decisions 81 and 94), and a hash of their
 * bits that CI checks on every platform. */
AX_TEST(deterministic_exp_log_atan2_and_sin_cos_are_accurate_and_stable)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = -400; i <= 400; ++i) {
        const float x = (float)i * 0.05f; /* -20 .. 20 */
        float s, c;
        const float e = ax_expf(x * 0.5f);
        ax_sin_cos(x, &s, &c);
        AX_CHECK(fabs((double)e - exp((double)x * 0.5)) <= 2e-6 * exp((double)x * 0.5));
        AX_CHECK(fabs((double)s - sin((double)x)) < 3e-6);
        AX_CHECK(fabs((double)c - cos((double)x)) < 3e-6);
        if (x > 0.0f) {
            AX_CHECK(fabs((double)ax_log2f(x) - log2((double)x)) < 3e-6);
        }
        {
            const float yy = (float)((i * 37) % 23) - 11.0f, xx = (float)((i * 11) % 19) - 9.0f;
            AX_CHECK(fabs((double)ax_atan2f(yy, xx) - atan2((double)yy, (double)xx)) < 2e-6);
        }
    }
    AX_CHECK(ax_expf(-200.0f) == 0.0f);
    AX_CHECK(ax_log2f(-1.0f) != ax_log2f(-1.0f));
    AX_CHECK(ax_atan2f(0.0f, 0.0f) == 0.0f);
    for (i = 0; i < 16; ++i) {
        const float x = (float)i * 0.37f - 2.5f;
        float v[5];
        int k;
        ax_sin_cos(x, &v[0], &v[1]);
        v[2] = ax_expf(x);
        v[3] = ax_log2f(x + 3.0f);
        v[4] = ax_atan2f(x, 1.3f - x);
        for (k = 0; k < 5; ++k) {
            uint32_t bits;
            memcpy(&bits, &v[k], sizeof bits);
            h = (h ^ bits) * 16777619u;
        }
    }
    printf("hash %08x\n", (unsigned)h);
    AX_CHECK_EQ_U(h, 0x27c11065u);
}
