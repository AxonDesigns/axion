/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "axion/base/ax_math.h"

#include <math.h>
#include <string.h>

ax_rect ax_rect_union(ax_rect r, ax_rect s)
{
    float x0, y0, x1, y1;
    if (ax_rect_is_empty(r)) {
        return s;
    }
    if (ax_rect_is_empty(s)) {
        return r;
    }
    x0 = fminf(r.x, s.x);
    y0 = fminf(r.y, s.y);
    x1 = fmaxf(r.x + r.w, s.x + s.w);
    y1 = fmaxf(r.y + r.h, s.y + s.h);
    return ax_rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* ---- Deterministic elementary functions ------------------------------------------------------
 *
 * libm's sinf, cosf and powf may differ in the last bit between platforms, and a Render IR built
 * from them would draw differently. These use only +, -, *, / and bit manipulation (and every
 * AXION target is built without floating-point contraction), so they give the same bits
 * everywhere (decision 81). Their error is below 1e-7 relative over the ranges UI code uses.
 */

/* sin and cos of x. Cody-Waite reduction by pi/2 in three parts (the first two have few
 * significant bits, so k times them is exact for |k| < 2^12), then Taylor polynomials on
 * [-pi/4, pi/4] (errors below 2e-9 there). Precision degrades for |x| beyond about 6000, but the
 * result stays deterministic. */
static void sin_cos(float x, float *out_s, float *out_c)
{
    const float p1 = 1.5703125f; /* pi/2 in three parts (Cephes' DP1-DP3, x2) */
    const float p2 = 4.837512969970703125e-4f;
    const float p3 = 7.54978995489188216e-8f;
    float k, r, r2, s, c;
    int32_t q;
    if (!(x - x == 0.0f)) {
        *out_s = *out_c = x - x; /* NaN for infinities and NaN */
        return;
    }
    k = x * 0.636619772f; /* 2 / pi */
    k = (float)(int64_t)(k < 0.0f ? k - 0.5f : k + 0.5f);
    q = (int32_t)((int64_t)k & 3);
    r = ((x - k * p1) - k * p2) - k * p3;
    r2 = r * r;
    s = r2 * (1.0f / 362880.0f) - 1.0f / 5040.0f;
    s = s * r2 + 1.0f / 120.0f;
    s = s * r2 - 1.0f / 6.0f;
    s = s * r2 * r + r;
    c = r2 * (-1.0f / 3628800.0f) + 1.0f / 40320.0f;
    c = c * r2 - 1.0f / 720.0f;
    c = c * r2 + 1.0f / 24.0f;
    c = c * r2 - 0.5f;
    c = c * r2 + 1.0f;
    switch (q) {
    case 0:
        *out_s = s;
        *out_c = c;
        break;
    case 1:
        *out_s = c;
        *out_c = -s;
        break;
    case 2:
        *out_s = -s;
        *out_c = -c;
        break;
    default:
        *out_s = -c;
        *out_c = s;
        break;
    }
}

/* log2 of a positive normal float: the mantissa brought to [sqrt(1/2), sqrt(2)], where
 * ln(m) = 2 atanh(t) with t = (m - 1) / (m + 1), |t| <= 0.172; five terms of the series leave an
 * error below 1e-9. */
static float log2_pos(float x)
{
    uint32_t bits;
    int32_t e;
    float m, t, t2, p;
    memcpy(&bits, &x, sizeof bits);
    e = (int32_t)((bits >> 23) & 0xFFu) - 127;
    bits = (bits & 0x007FFFFFu) | 0x3F800000u;
    memcpy(&m, &bits, sizeof m);
    if (m > 1.41421356f) {
        m = m * 0.5f;
        e += 1;
    }
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    p = t2 * (1.0f / 9.0f);
    p = (p + 1.0f / 7.0f) * t2;
    p = (p + 1.0f / 5.0f) * t2;
    p = (p + 1.0f / 3.0f) * t2;
    p = (p + 1.0f) * t;
    return (float)e + (2.0f * p) * 1.44269504f; /* 1 / ln 2 */
}

/* 2^y: y = n + f with n the nearest integer and |f| <= 1/2; e^(f ln 2) by its Taylor series to
 * degree 7 (error below 1e-8), then the exponent bits. 0 below 2^-126 (no denormals), infinity
 * above 2^127. */
static float exp2_det(float y)
{
    int32_t n;
    float z, p, scale;
    uint32_t bits;
    if (y < -126.0f) {
        return 0.0f;
    }
    if (y > 127.0f) {
        return INFINITY;
    }
    n = (int32_t)(y < 0.0f ? y - 0.5f : y + 0.5f);
    z = (y - (float)n) * 0.693147181f;
    p = 1.0f / 5040.0f;
    p = p * z + 1.0f / 720.0f;
    p = p * z + 1.0f / 120.0f;
    p = p * z + 1.0f / 24.0f;
    p = p * z + 1.0f / 6.0f;
    p = p * z + 0.5f;
    p = p * z + 1.0f;
    p = p * z + 1.0f;
    if (n < -125) { /* keep the scale normal: two steps */
        p = p * 0.5f;
        n += 1;
    }
    bits = (uint32_t)(n + 127) << 23;
    memcpy(&scale, &bits, sizeof scale);
    return p * scale;
}

/* x^e for x > 0 finite. */
static float pow_pos(float x, float e)
{
    return exp2_det(e * log2_pos(x));
}

void ax_sin_cos(float x, float *out_sin, float *out_cos)
{
    sin_cos(x, out_sin, out_cos);
}

float ax_exp2f(float y)
{
    if (y != y) {
        return y;
    }
    return exp2_det(y);
}

float ax_expf(float x)
{
    return ax_exp2f(x * 1.44269504f); /* log2(e) */
}

float ax_log2f(float x)
{
    if (!(x >= 1.17549435e-38f) || x - x != 0.0f) {
        return NAN;
    }
    return log2_pos(x);
}

/* atan on [0, inf): Cephes' reduction (tan(3pi/8) and tan(pi/8)) and polynomial. */
static float atan_pos(float x)
{
    float y = 0.0f, z;
    if (x > 2.414213562f) {
        y = 1.570796327f;
        x = -1.0f / x;
    } else if (x > 0.414213562f) {
        y = 0.785398163f;
        x = (x - 1.0f) / (x + 1.0f);
    }
    z = x * x;
    return y + ((((8.05374449538e-2f * z - 1.38776856032e-1f) * z + 1.99777106478e-1f) * z -
                 3.33329491539e-1f) *
                    z * x +
                x);
}

float ax_atan2f(float y, float x)
{
    float a;
    if (y != y || x != x) {
        return y + x;
    }
    if (x == 0.0f) {
        return y > 0.0f ? 1.570796327f : (y < 0.0f ? -1.570796327f : 0.0f);
    }
    a = atan_pos(fabsf(y / x));
    if (x < 0.0f) {
        a = 3.141592654f - a;
    }
    return y < 0.0f ? -a : a;
}

ax_affine2 ax_affine2_rotate(float radians)
{
    float c, s;
    sin_cos(radians, &s, &c);
    return ax_affine2_make(c, s, -s, c, 0.0f, 0.0f);
}

bool ax_affine2_invert(ax_affine2 m, ax_affine2 *out)
{
    const float det = ax_affine2_determinant(m);
    float inv;
    if (det == 0.0f || !isfinite(det)) {
        return false;
    }
    inv = 1.0f / det;
    out->a = m.d * inv;
    out->b = -m.b * inv;
    out->c = -m.c * inv;
    out->d = m.a * inv;
    out->tx = (m.c * m.ty - m.d * m.tx) * inv;
    out->ty = (m.b * m.tx - m.a * m.ty) * inv;
    return true;
}

ax_rect ax_affine2_transform_rect(ax_affine2 m, ax_rect r)
{
    const ax_vec2 p[4] = {
        ax_affine2_apply(m, ax_vec2_make(r.x, r.y)),
        ax_affine2_apply(m, ax_vec2_make(r.x + r.w, r.y)),
        ax_affine2_apply(m, ax_vec2_make(r.x, r.y + r.h)),
        ax_affine2_apply(m, ax_vec2_make(r.x + r.w, r.y + r.h)),
    };
    float x0 = p[0].x, y0 = p[0].y, x1 = p[0].x, y1 = p[0].y;
    int i;
    for (i = 1; i < 4; ++i) {
        x0 = fminf(x0, p[i].x);
        y0 = fminf(y0, p[i].y);
        x1 = fmaxf(x1, p[i].x);
        y1 = fmaxf(y1, p[i].y);
    }
    return ax_rect_make(x0, y0, x1 - x0, y1 - y0);
}

float ax_srgb_to_linear(float encoded)
{
    const float x = fabsf(encoded);
    float y;
    if (!(x <= 3.402823466e+38f)) {
        return encoded; /* infinities and NaN */
    }
    y = x <= 0.04045f ? x / 12.92f : pow_pos((x + 0.055f) / 1.055f, 2.4f);
    return encoded < 0.0f ? -y : y;
}

float ax_linear_to_srgb(float linear)
{
    const float x = fabsf(linear);
    float y;
    if (!(x <= 3.402823466e+38f)) {
        return linear;
    }
    y = x <= 0.0031308f ? x * 12.92f : 1.055f * pow_pos(x, 1.0f / 2.4f) - 0.055f;
    return linear < 0.0f ? -y : y;
}

ax_color ax_color_from_srgb(float r, float g, float b, float a)
{
    return ax_color_make(ax_srgb_to_linear(r) * a, ax_srgb_to_linear(g) * a,
                         ax_srgb_to_linear(b) * a, a);
}

ax_color ax_color_from_srgb8(uint32_t argb)
{
    return ax_color_from_srgb((float)((argb >> 16) & 0xFFu) / 255.0f,
                              (float)((argb >> 8) & 0xFFu) / 255.0f, (float)(argb & 0xFFu) / 255.0f,
                              (float)(argb >> 24) / 255.0f);
}

ax_color ax_color_to_srgb_premultiplied(ax_color c)
{
    if (!(c.a > 0.0f)) {
        return ax_color_make(ax_linear_to_srgb(c.r), ax_linear_to_srgb(c.g), ax_linear_to_srgb(c.b),
                             c.a);
    }
    return ax_color_make(ax_linear_to_srgb(c.r / c.a) * c.a, ax_linear_to_srgb(c.g / c.a) * c.a,
                         ax_linear_to_srgb(c.b / c.a) * c.a, c.a);
}

ax_color ax_color_from_srgb_premultiplied(ax_color e)
{
    if (!(e.a > 0.0f)) {
        return ax_color_make(ax_srgb_to_linear(e.r), ax_srgb_to_linear(e.g), ax_srgb_to_linear(e.b),
                             e.a);
    }
    return ax_color_make(ax_srgb_to_linear(e.r / e.a) * e.a, ax_srgb_to_linear(e.g / e.a) * e.a,
                         ax_srgb_to_linear(e.b / e.a) * e.a, e.a);
}
