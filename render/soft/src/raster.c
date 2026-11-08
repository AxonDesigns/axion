/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft's tiles: coverage, blending, groups and the final store, the scalar path. This
 * is the reference the SIMD path (raster_simd.c) must match bit for bit: each lane there performs
 * these operations, in this order. A frame uses one path or the other for every item.
 *
 * A tile keeps four planes of floats (r, g, b, a), premultiplied and sRGB-encoded, for its base
 * and for each open group. Items blend in sRGB space, or decode, blend and encode again in linear
 * light (what a GPU's sRGB view does). The planes are rounded to 8 bits once, at the end.
 */
#include <math.h> /* sqrtf only */
#include <string.h>

#include "soft_internal.h"

static float clamp01(float v)
{
    return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
}

/* ---- coverage (decision 55) ------------------------------------------------------------------ */

/* Area of the unit pixel square on the inner side of a straight edge at signed distance d (device
 * pixels, positive inside), for a slab's precomputed normal. */
static float edge_coverage(float d, const soft_edge *e)
{
    const float t = d + e->half_sum;
    float u;
    if (e->axis) {
        return clamp01(t * e->inv_hi);
    }
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= e->sum) {
        return 1.0f;
    }
    if (t < e->lo) {
        return t * t * e->k;
    }
    if (t <= e->hi) {
        return (t - e->half_lo) * e->inv_hi;
    }
    u = e->sum - t;
    return 1.0f - u * u * e->k;
}

/* The same for a normal (nx, ny) known per pixel (a corner's arc). */
static float edge_coverage_n(float d, float nx, float ny)
{
    const float ax = nx < 0.0f ? -nx : nx;
    const float ay = ny < 0.0f ? -ny : ny;
    const float lo = ay < ax ? ay : ax;
    const float hi = ay > ax ? ay : ax;
    const float sum = lo + hi;
    const float t = d + 0.5f * sum;
    float u;
    if (lo < 1e-4f) {
        return clamp01(t / hi);
    }
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= sum) {
        return 1.0f;
    }
    if (t < lo) {
        return t * t / ((2.0f * lo) * hi);
    }
    if (t <= hi) {
        return (t - 0.5f * lo) / hi;
    }
    u = sum - t;
    return 1.0f - u * u / ((2.0f * lo) * hi);
}

static float slab(float x, float lo_edge, float hi_edge, const soft_edge *e)
{
    const float c = edge_coverage((x - lo_edge) * e->rlen, e) +
                    edge_coverage((hi_edge - x) * e->rlen, e) - 1.0f;
    return c > 0.0f ? c : 0.0f;
}

/* One rounded corner: 1 unless the point lies beyond the corner's centre on both axes; then the
 * circle's coverage, its edge treated as locally straight. */
static float corner(const soft_shape *s, int k, float lx, float ly)
{
    const float bx = (s->cx[k] - lx) * s->ix[k];
    const float by = (s->cy[k] - ly) * s->iy[k];
    float vx, vy, dist, gx, gy, grx, gry, len;
    if (bx <= 0.0f || by <= 0.0f) {
        return 1.0f;
    }
    vx = lx - s->cx[k];
    vy = ly - s->cy[k];
    dist = sqrtf(vx * vx + vy * vy);
    if (dist > 1e-6f) {
        gx = vx / dist;
        gy = vy / dist;
    } else {
        gx = 1.0f;
        gy = 0.0f;
    }
    /* Gradient of the distance to the centre, in device pixels. */
    grx = gx * s->ia + gy * s->ib;
    gry = gx * s->ic + gy * s->id;
    len = sqrtf(grx * grx + gry * gry);
    return edge_coverage_n((s->r[k] - dist) / len, grx / len, gry / len);
}

float ax__soft_shape_coverage(const soft_shape *s, float x, float y)
{
    const float lx = s->ia * x + s->ic * y + s->itx;
    const float ly = s->ib * x + s->id * y + s->ity;
    float cov = slab(lx, s->x0, s->x1, &s->ex) * slab(ly, s->y0, s->y1, &s->ey);
    int k;
    for (k = 0; k < 4; ++k) {
        if ((s->corners >> k) & 1u) {
            const float c = corner(s, k, lx, ly);
            cov = c < cov ? c : cov;
        }
    }
    return cov;
}

/* ---- blending -------------------------------------------------------------------------------- */

static float flush(float v)
{
    return v < SOFT_FLUSH ? 0.0f : v;
}

/* Source-over of color * cov onto the pixel at index i of the planes. */
void ax__soft_blend_pixel(float *planes, size_t i, const float color[4], float cov, bool linear)
{
    const float t = 1.0f - color[3] * cov;
    int k;
    if (!linear) {
        for (k = 0; k < 4; ++k) {
            float *d = planes + (size_t)k * SOFT_TILE_PIXELS + i;
            *d = flush(color[k] * cov + *d * t);
        }
        return;
    }
    for (k = 0; k < 3; ++k) {
        float *d = planes + (size_t)k * SOFT_TILE_PIXELS + i;
        const float v = color[k] * cov + ax__soft_srgb_to_linear(*d) * t;
        *d = flush(ax__soft_linear_to_srgb(v));
    }
    {
        float *d = planes + 3u * SOFT_TILE_PIXELS + i;
        *d = flush(color[3] * cov + *d * t);
    }
}

typedef struct tile_rect {
    int32_t x0, y0, x1, y1; /* tile-relative, exclusive */
} tile_rect;

static void draw_item(const soft_frame *f, const soft_cmd *cmd, float *planes, int32_t ox,
                      int32_t oy, tile_rect r)
{
    int32_t x, y;
    for (y = r.y0; y < r.y1; ++y) {
        const float py = (float)(oy + y) + 0.5f;
        for (x = r.x0; x < r.x1; ++x) {
            const float px = (float)(ox + x) + 0.5f;
            float cov = ax__soft_shape_coverage(&cmd->shape, px, py);
            uint32_t c = cmd->clip;
            /* Clips multiply in, innermost first. Coverage only shrinks, so stopping once it is
             * below the threshold gives what the full product would. */
            while (c != AX_IR_NO_INDEX && cov >= SOFT_MIN_COVERAGE) {
                cov = cov * ax__soft_shape_coverage(&f->clips[c].shape, px, py);
                c = f->clips[c].parent;
            }
            if (cov < SOFT_MIN_COVERAGE) {
                continue; /* blending 0 leaves the pixel as it is */
            }
            ax__soft_blend_pixel(planes, (size_t)y * SOFT_TILE + (size_t)x, cmd->color, cov,
                                 cmd->linear != 0);
        }
    }
}

static void clear_rect(float *planes, tile_rect r)
{
    int32_t y;
    int k;
    for (k = 0; k < 4; ++k) {
        for (y = r.y0; y < r.y1; ++y) {
            memset(planes + (size_t)k * SOFT_TILE_PIXELS + (size_t)y * SOFT_TILE + (size_t)r.x0, 0,
                   sizeof(float) * (size_t)(r.x1 - r.x0));
        }
    }
}

/* Composites a group (premultiplied, its own opacity applied) onto the planes below it. */
static void composite(float *dst, const float *grp, float opacity, bool linear, tile_rect r)
{
    int32_t x, y;
    for (y = r.y0; y < r.y1; ++y) {
        for (x = r.x0; x < r.x1; ++x) {
            const size_t i = (size_t)y * SOFT_TILE + (size_t)x;
            const float ga = grp[3u * SOFT_TILE_PIXELS + i] * opacity;
            const float t = 1.0f - ga;
            int k;
            for (k = 0; k < 3; ++k) {
                const float g = grp[(size_t)k * SOFT_TILE_PIXELS + i];
                float *d = dst + (size_t)k * SOFT_TILE_PIXELS + i;
                if (linear) {
                    const float v =
                        ax__soft_srgb_to_linear(g) * opacity + ax__soft_srgb_to_linear(*d) * t;
                    *d = flush(ax__soft_linear_to_srgb(v));
                } else {
                    *d = flush(g * opacity + *d * t);
                }
            }
            dst[3u * SOFT_TILE_PIXELS + i] = flush(ga + dst[3u * SOFT_TILE_PIXELS + i] * t);
        }
    }
}

static uint8_t to_u8(float v)
{
    return (uint8_t)(clamp01(v) * 255.0f + 0.5f);
}

static void store(const soft_frame *f, const float *planes, int32_t ox, int32_t oy, int32_t w,
                  int32_t h)
{
    const int swap = f->format == AX_SOFT_BGRA8;
    int32_t x, y;
    for (y = 0; y < h; ++y) {
        uint8_t *row = (uint8_t *)f->pixels + (size_t)(oy + y) * f->stride + (size_t)ox * 4u;
        for (x = 0; x < w; ++x) {
            const size_t i = (size_t)y * SOFT_TILE + (size_t)x;
            const uint8_t r = to_u8(planes[i]);
            const uint8_t g = to_u8(planes[SOFT_TILE_PIXELS + i]);
            const uint8_t b = to_u8(planes[2u * SOFT_TILE_PIXELS + i]);
            row[x * 4 + 0] = swap ? b : r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = swap ? r : b;
            row[x * 4 + 3] = to_u8(planes[3u * SOFT_TILE_PIXELS + i]);
        }
    }
}

void ax__soft_draw_tile(const soft_frame *f, soft_tile_ctx *ctx, uint32_t tx, uint32_t ty)
{
    const int32_t ox = (int32_t)(tx * SOFT_TILE), oy = (int32_t)(ty * SOFT_TILE);
    const int32_t w =
        (int32_t)f->width - ox < (int32_t)SOFT_TILE ? (int32_t)f->width - ox : (int32_t)SOFT_TILE;
    const int32_t h =
        (int32_t)f->height - oy < (int32_t)SOFT_TILE ? (int32_t)f->height - oy : (int32_t)SOFT_TILE;
    const size_t group_stride = 4u * SOFT_TILE_PIXELS;
    uint32_t depth = 0;
    uint32_t i;
    int32_t y;
    int k;

    for (k = 0; k < 4; ++k) {
        float *plane = ctx->planes + (size_t)k * SOFT_TILE_PIXELS;
        for (y = 0; y < h; ++y) {
            int32_t x;
            for (x = 0; x < w; ++x) {
                plane[(size_t)y * SOFT_TILE + (size_t)x] = f->background[k];
            }
        }
    }

    for (i = 0; i < f->cmd_count; ++i) {
        const soft_cmd *cmd = &f->cmds[i];
        tile_rect r;
        r.x0 = cmd->box.x0 - ox > 0 ? cmd->box.x0 - ox : 0;
        r.y0 = cmd->box.y0 - oy > 0 ? cmd->box.y0 - oy : 0;
        r.x1 = cmd->box.x1 - ox < w ? cmd->box.x1 - ox : w;
        r.y1 = cmd->box.y1 - oy < h ? cmd->box.y1 - oy : h;
        if (cmd->kind == SOFT_CMD_ITEM) {
            if (r.x0 < r.x1 && r.y0 < r.y1) {
                if (f->simd) {
                    ax__soft_draw_item_simd(f, cmd, ctx->planes + depth * group_stride, ox, oy,
                                            r.x0, r.y0, r.x1, r.y1);
                } else {
                    draw_item(f, cmd, ctx->planes + depth * group_stride, ox, oy, r);
                }
            }
        } else if (cmd->kind == SOFT_CMD_PUSH) {
            if (!(r.x0 < r.x1 && r.y0 < r.y1)) {
                i = cmd->end; /* the whole group misses this tile */
                continue;
            }
            depth++;
            clear_rect(ctx->planes + depth * group_stride, r);
        } else {
            composite(ctx->planes + (depth - 1u) * group_stride, ctx->planes + depth * group_stride,
                      cmd->color[3], cmd->linear != 0, r);
            depth--;
        }
    }
    store(f, ctx->planes, ox, oy, w, h);
}
