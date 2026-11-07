/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "axion/base/ax_math.h"

#include <math.h>

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

ax_affine2 ax_affine2_rotate(float radians)
{
    const float c = cosf(radians);
    const float s = sinf(radians);
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
    const float y = x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
    return encoded < 0.0f ? -y : y;
}

float ax_linear_to_srgb(float linear)
{
    const float x = fabsf(linear);
    const float y = x <= 0.0031308f ? x * 12.92f : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
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
