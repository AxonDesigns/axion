/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic math for ax_render_soft: affine transforms, and the sRGB transfer functions on
 * [0, 1] (base's, which use +, -, *, / and bit manipulation only: decision 81).
 */
#include "axion/base/ax_math.h"

#include "soft_internal.h"

soft_affine ax__soft_affine_mul(soft_affine m, soft_affine n)
{
    soft_affine r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.tx = m.a * n.tx + m.c * n.ty + m.tx;
    r.ty = m.b * n.tx + m.d * n.ty + m.ty;
    return r;
}

static bool finite(float v)
{
    return v - v == 0.0f; /* false for infinities and NaN */
}

bool ax__soft_affine_invert(soft_affine m, soft_affine *out)
{
    const float det = m.a * m.d - m.b * m.c;
    float inv;
    if (det == 0.0f || !finite(det)) {
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

static float clamp01(float v)
{
    return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; /* NaN gives 0 */
}

float ax__soft_linear_to_srgb(float linear)
{
    return ax_linear_to_srgb(clamp01(linear));
}

float ax__soft_srgb_to_linear(float encoded)
{
    return ax_srgb_to_linear(clamp01(encoded));
}
