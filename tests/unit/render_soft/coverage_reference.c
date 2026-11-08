/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "coverage_reference.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct shape {
    float x0, y0, x1, y1;
    float radii[4];      /* tl, tr, br, bl, clamped */
    float color[4];      /* sRGB-encoded premultiplied */
    ax_affine2 to_local; /* device -> local */
    float footprint;     /* radius of a pixel's footprint in local units (half diagonal) */
} shape;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void clamp_radii(const float in[4], float w, float h, float out[4])
{
    float f = 1.0f;
    int i;
    for (i = 0; i < 4; ++i) {
        out[i] = in[i] > 0.0f ? in[i] : 0.0f;
    }
    if (out[0] + out[1] > w)
        f = fminf(f, w / (out[0] + out[1]));
    if (out[3] + out[2] > w)
        f = fminf(f, w / (out[3] + out[2]));
    if (out[0] + out[3] > h)
        f = fminf(f, h / (out[0] + out[3]));
    if (out[1] + out[2] > h)
        f = fminf(f, h / (out[1] + out[2]));
    for (i = 0; i < 4; ++i) {
        out[i] *= f;
    }
}

static const float k_inward[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}; /* tl tr br bl */

static void corner_vertex(const shape *s, int k, float *vx, float *vy)
{
    *vx = (k == 0 || k == 3) ? s->x0 : s->x1;
    *vy = (k == 0 || k == 1) ? s->y0 : s->y1;
}

/* Inside the rect and, for every corner whose centre p lies beyond, inside its circle. */
static bool inside(const shape *s, float px, float py)
{
    int k;
    if (px < s->x0 || px > s->x1 || py < s->y0 || py > s->y1) {
        return false;
    }
    for (k = 0; k < 4; ++k) {
        float vx, vy, cx, cy;
        const float r = s->radii[k];
        if (r <= 0.0f) {
            continue;
        }
        corner_vertex(s, k, &vx, &vy);
        cx = vx + k_inward[k][0] * r;
        cy = vy + k_inward[k][1] * r;
        if ((cx - px) * k_inward[k][0] > 0.0f && (cy - py) * k_inward[k][1] > 0.0f &&
            (px - cx) * (px - cx) + (py - cy) * (py - cy) > r * r) {
            return false;
        }
    }
    return true;
}

static float segment_distance(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return hypotf(px - (ax + t * dx), py - (ay + t * dy));
}

/* Exact distance to the outline (unsigned): four straight segments and four quarter arcs. */
static float outline_distance(const shape *s, float px, float py)
{
    const float *r = s->radii;
    float d = segment_distance(px, py, s->x0 + r[0], s->y0, s->x1 - r[1], s->y0);
    int k;
    d = fminf(d, segment_distance(px, py, s->x1, s->y0 + r[1], s->x1, s->y1 - r[2]));
    d = fminf(d, segment_distance(px, py, s->x1 - r[2], s->y1, s->x0 + r[3], s->y1));
    d = fminf(d, segment_distance(px, py, s->x0, s->y1 - r[3], s->x0, s->y0 + r[0]));
    for (k = 0; k < 4; ++k) {
        float vx, vy, cx, cy;
        if (r[k] <= 0.0f) {
            continue;
        }
        corner_vertex(s, k, &vx, &vy);
        cx = vx + k_inward[k][0] * r[k];
        cy = vy + k_inward[k][1] * r[k];
        if ((cx - px) * k_inward[k][0] >= 0.0f && (cy - py) * k_inward[k][1] >= 0.0f) {
            d = fminf(d, fabsf(hypotf(px - cx, py - cy) - r[k])); /* within the quarter */
        } else {                                                  /* nearest is an end of the arc */
            d = fminf(d, hypotf(px - vx, py - (cy)));
            d = fminf(d, hypotf(px - (cx), py - vy));
        }
    }
    return d;
}

static float coverage(const shape *s, uint32_t x, uint32_t y, uint32_t samples)
{
    const ax_vec2 c = ax_affine2_apply(s->to_local, ax_vec2_make((float)x + 0.5f, (float)y + 0.5f));
    uint32_t i, j, in = 0;
    if (outline_distance(s, c.x, c.y) > s->footprint) {
        return inside(s, c.x, c.y) ? 1.0f : 0.0f;
    }
    for (j = 0; j < samples; ++j) {
        for (i = 0; i < samples; ++i) {
            const ax_vec2 p = ax_affine2_apply(
                s->to_local, ax_vec2_make((float)x + ((float)i + 0.5f) / (float)samples,
                                          (float)y + ((float)j + 0.5f) / (float)samples));
            in += inside(s, p.x, p.y) ? 1u : 0u;
        }
    }
    return (float)in / (float)(samples * samples);
}

/* Near a vertex or a corner arc: within one footprint of the corner's square region. */
static bool near_corner(const shape *s, float px, float py)
{
    const float m = 2.0f * s->footprint;
    const float rl = fmaxf(s->radii[0], s->radii[3]), rr = fmaxf(s->radii[1], s->radii[2]);
    const float rt = fmaxf(s->radii[0], s->radii[1]), rb = fmaxf(s->radii[3], s->radii[2]);
    const bool nx =
        (px > s->x0 - m && px < s->x0 + rl + m) || (px > s->x1 - rr - m && px < s->x1 + m);
    const bool ny =
        (py > s->y0 - m && py < s->y0 + rt + m) || (py > s->y1 - rb - m && py < s->y1 + m);
    return nx && ny;
}

static void draw_shape(const shape *s, const ax_affine2 *to_device, axt_reference *ref,
                       uint32_t samples)
{
    const ax_rect b = ax_affine2_transform_rect(
        *to_device, ax_rect_make(s->x0, s->y0, s->x1 - s->x0, s->y1 - s->y0));
    const int x_lo = (int)floorf(b.x) - 2, y_lo = (int)floorf(b.y) - 2;
    const int x_hi = (int)ceilf(b.x + b.w) + 2, y_hi = (int)ceilf(b.y + b.h) + 2;
    int x, y;
    for (y = y_lo < 0 ? 0 : y_lo; y < y_hi && y < (int)ref->height; ++y) {
        for (x = x_lo < 0 ? 0 : x_lo; x < x_hi && x < (int)ref->width; ++x) {
            const float cov = coverage(s, (uint32_t)x, (uint32_t)y, samples);
            float *px = &ref->rgba[((size_t)y * ref->width + (size_t)x) * 4];
            const float a = s->color[3] * cov;
            const ax_vec2 c =
                ax_affine2_apply(s->to_local, ax_vec2_make((float)x + 0.5f, (float)y + 0.5f));
            int k;
            for (k = 0; k < 4; ++k) {
                px[k] = s->color[k] * cov + px[k] * (1.0f - a);
            }
            if (cov > 0.0f && cov < 1.0f && near_corner(s, c.x, c.y)) {
                ref->corner[(size_t)y * ref->width + (size_t)x] = 1;
            }
        }
    }
}

bool axt_reference_render(const ax_render_ir *ir, uint32_t width, uint32_t height, float scale,
                          uint32_t samples, axt_reference *out)
{
    ax_affine2 *world;
    float *opacity;
    bool *skip;
    uint32_t i;
    const ax_color bg = ax_color_to_srgb_premultiplied(ir->background);

    out->width = width;
    out->height = height;
    out->rgba = (float *)malloc(sizeof(float) * 4u * width * height);
    out->corner = (uint8_t *)calloc((size_t)width * height, 1);
    world = (ax_affine2 *)malloc(sizeof *world * ir->layer_count);
    opacity = (float *)malloc(sizeof *opacity * ir->layer_count);
    skip = (bool *)malloc(sizeof *skip * ir->layer_count);
    if (out->rgba == NULL || out->corner == NULL || world == NULL || opacity == NULL ||
        skip == NULL) {
        free(world);
        free(opacity);
        free(skip);
        axt_reference_free(out);
        return false;
    }
    for (i = 0; i < width * height; ++i) {
        out->rgba[i * 4 + 0] = clampf(bg.r, 0, 1);
        out->rgba[i * 4 + 1] = clampf(bg.g, 0, 1);
        out->rgba[i * 4 + 2] = clampf(bg.b, 0, 1);
        out->rgba[i * 4 + 3] = clampf(bg.a, 0, 1);
    }
    for (i = 0; i < ir->layer_count; ++i) {
        const ax_ir_layer *l = ax_ir_layer_at(ir, i);
        const bool has_parent = l->parent != AX_IR_NO_LAYER;
        ax_ir_item_iter it;
        const ax_ir_item_header *h;
        ax_affine2 inv;
        world[i] = ax_affine2_mul(has_parent ? world[l->parent] : ax_affine2_scale(scale, scale),
                                  l->transform);
        opacity[i] = (has_parent ? opacity[l->parent] : 1.0f) * l->opacity;
        skip[i] = (has_parent && skip[l->parent]) || l->clip.kind != AX_IR_CLIP_NONE;
        if (skip[i] || !ax_affine2_invert(world[i], &inv)) {
            continue;
        }
        it = ax_ir_items(ir, l);
        while ((h = ax_ir_item_next(&it)) != NULL) {
            shape s;
            ax_rect r;
            ax_color c;
            float radii[4] = {0, 0, 0, 0};
            if (h->type == AX_IR_ITEM_RECT) {
                const ax_ir_rect_item *item = (const ax_ir_rect_item *)(const void *)h;
                r = item->rect;
                c = item->color;
            } else if (h->type == AX_IR_ITEM_ROUNDED_RECT) {
                const ax_ir_rounded_rect_item *item =
                    (const ax_ir_rounded_rect_item *)(const void *)h;
                r = item->rect;
                c = item->color;
                memcpy(radii, item->radii, sizeof radii);
            } else {
                if (h->flags & AX_IR_ITEM_REQUIRED) {
                    break;
                }
                continue;
            }
            if (!(r.w > 0.0f) || !(r.h > 0.0f)) {
                continue;
            }
            memset(&s, 0, sizeof s);
            s.x0 = r.x;
            s.y0 = r.y;
            s.x1 = r.x + r.w;
            s.y1 = r.y + r.h;
            clamp_radii(radii, r.w, r.h, s.radii);
            c = ax_color_to_srgb_premultiplied(ax_color_scale(c, opacity[i]));
            s.color[3] = clampf(c.a, 0, 1);
            s.color[0] = clampf(c.r, 0, s.color[3]);
            s.color[1] = clampf(c.g, 0, s.color[3]);
            s.color[2] = clampf(c.b, 0, s.color[3]);
            s.to_local = inv;
            {
                const ax_vec2 u = ax_affine2_apply_vector(inv, ax_vec2_make(0.5f, 0.5f));
                const ax_vec2 v = ax_affine2_apply_vector(inv, ax_vec2_make(0.5f, -0.5f));
                s.footprint = fmaxf(sqrtf(u.x * u.x + u.y * u.y), sqrtf(v.x * v.x + v.y * v.y));
            }
            draw_shape(&s, &world[i], out, samples);
        }
    }
    free(world);
    free(opacity);
    free(skip);
    return true;
}

void axt_reference_free(axt_reference *ref)
{
    free(ref->rgba);
    free(ref->corner);
    ref->rgba = NULL;
    ref->corner = NULL;
}
