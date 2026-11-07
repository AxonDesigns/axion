/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the 2D math the Render IR and the renderers share. Vectors, rectangles, 2D affine
 * transforms and colors, all in single precision. Coordinates are in logical units unless a
 * function says otherwise; nothing here knows about device pixels.
 *
 * Colors (ax_color) are linear-light, premultiplied, extended-range RGBA in the sRGB / Rec. 709
 * primaries: components may be negative or above 1 (wide gamut, HDR), as in scRGB or Apple's
 * extended linear sRGB. Alpha is in [0, 1]. The sRGB transfer functions below keep the sign of
 * negative inputs and extend the curve above 1, so out-of-range colors convert both ways.
 */
#ifndef AX_BASE_MATH_H
#define AX_BASE_MATH_H

#include <stdbool.h>
#include <stdint.h>

#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

typedef struct ax_vec2 {
    float x, y;
} ax_vec2;

/* Origin at (x, y), extending w to the right and h down. Empty when w <= 0 or h <= 0. */
typedef struct ax_rect {
    float x, y, w, h;
} ax_rect;

/*
 * A 2D affine transform, mapping (x, y) to
 *     x' = a * x + c * y + tx
 *     y' = b * x + d * y + ty
 * the same layout as CSS's matrix(a, b, c, d, tx, ty). With y pointing down, a positive rotation
 * turns clockwise on screen.
 */
typedef struct ax_affine2 {
    float a, b, c, d, tx, ty;
} ax_affine2;

/* Linear-light, premultiplied, extended-range RGBA (see the top of this file). */
typedef struct ax_color {
    float r, g, b, a;
} ax_color;

/* ---- vectors and rectangles ---------------------------------------------------------------- */

static inline ax_vec2 ax_vec2_make(float x, float y)
{
    ax_vec2 v;
    v.x = x;
    v.y = y;
    return v;
}

static inline ax_vec2 ax_vec2_add(ax_vec2 p, ax_vec2 q)
{
    return ax_vec2_make(p.x + q.x, p.y + q.y);
}

static inline ax_vec2 ax_vec2_sub(ax_vec2 p, ax_vec2 q)
{
    return ax_vec2_make(p.x - q.x, p.y - q.y);
}

static inline ax_vec2 ax_vec2_scale(ax_vec2 p, float s)
{
    return ax_vec2_make(p.x * s, p.y * s);
}

static inline ax_rect ax_rect_make(float x, float y, float w, float h)
{
    ax_rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static inline bool ax_rect_is_empty(ax_rect r)
{
    return !(r.w > 0.0f && r.h > 0.0f); /* NaN counts as empty */
}

/* Half-open: the left and top edges are inside, the right and bottom edges are not. */
static inline bool ax_rect_contains(ax_rect r, ax_vec2 p)
{
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}

/* The smallest rectangle containing both; an empty input is ignored. */
AX_API ax_rect ax_rect_union(ax_rect r, ax_rect s);

/* ---- affine transforms --------------------------------------------------------------------- */

static inline ax_affine2 ax_affine2_make(float a, float b, float c, float d, float tx, float ty)
{
    ax_affine2 m;
    m.a = a;
    m.b = b;
    m.c = c;
    m.d = d;
    m.tx = tx;
    m.ty = ty;
    return m;
}

static inline ax_affine2 ax_affine2_identity(void)
{
    return ax_affine2_make(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
}

static inline ax_affine2 ax_affine2_translate(float tx, float ty)
{
    return ax_affine2_make(1.0f, 0.0f, 0.0f, 1.0f, tx, ty);
}

static inline ax_affine2 ax_affine2_scale(float sx, float sy)
{
    return ax_affine2_make(sx, 0.0f, 0.0f, sy, 0.0f, 0.0f);
}

/* Rotation by `radians` about the origin (clockwise on screen, since y points down). */
AX_API ax_affine2 ax_affine2_rotate(float radians);

/* m * n: applies n first, then m. */
static inline ax_affine2 ax_affine2_mul(ax_affine2 m, ax_affine2 n)
{
    return ax_affine2_make(m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d,
                           m.b * n.c + m.d * n.d, m.a * n.tx + m.c * n.ty + m.tx,
                           m.b * n.tx + m.d * n.ty + m.ty);
}

static inline ax_vec2 ax_affine2_apply(ax_affine2 m, ax_vec2 p)
{
    return ax_vec2_make(m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty);
}

/* The linear part only: maps a direction or displacement, ignoring the translation. */
static inline ax_vec2 ax_affine2_apply_vector(ax_affine2 m, ax_vec2 v)
{
    return ax_vec2_make(m.a * v.x + m.c * v.y, m.b * v.x + m.d * v.y);
}

static inline float ax_affine2_determinant(ax_affine2 m)
{
    return m.a * m.d - m.b * m.c;
}

/* The inverse, so that ax_affine2_mul(*out, m) is the identity. Returns false and leaves *out
 * untouched when m is singular (or not finite). */
AX_API bool ax_affine2_invert(ax_affine2 m, ax_affine2 *out);

/* The axis-aligned bounds of r's four corners under m. */
AX_API ax_rect ax_affine2_transform_rect(ax_affine2 m, ax_rect r);

/* ---- colors -------------------------------------------------------------------------------- */

static inline ax_color ax_color_make(float r, float g, float b, float a)
{
    ax_color c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

/* The sRGB transfer functions (IEC 61966-2-1), extended: odd-symmetric for negative inputs and
 * following the same curve above 1. */
AX_API float ax_srgb_to_linear(float encoded);
AX_API float ax_linear_to_srgb(float linear);

/*
 * From sRGB-encoded, straight (non-premultiplied) components, as design tools and CSS write them,
 * to an ax_color: decoded to linear light, then premultiplied.
 */
AX_API ax_color ax_color_from_srgb(float r, float g, float b, float a);

/* From an 8-bit 0xAARRGGBB value (sRGB, straight alpha). */
AX_API ax_color ax_color_from_srgb8(uint32_t argb);

/*
 * To sRGB-encoded, premultiplied components: what a pass that blends in sRGB space writes. The
 * color is unpremultiplied, encoded and premultiplied again. A color with alpha 0 (additive
 * light) has its components encoded directly, so they survive the trip.
 * ax_color_from_srgb_premultiplied is the inverse.
 */
AX_API ax_color ax_color_to_srgb_premultiplied(ax_color c);
AX_API ax_color ax_color_from_srgb_premultiplied(ax_color encoded);

/* Scales every component, alpha included: the color drawn at `opacity`. */
static inline ax_color ax_color_scale(ax_color c, float opacity)
{
    return ax_color_make(c.r * opacity, c.g * opacity, c.b * opacity, c.a * opacity);
}

AX_EXTERN_C_END

#endif /* AX_BASE_MATH_H */
