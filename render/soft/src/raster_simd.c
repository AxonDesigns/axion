/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft's SIMD path: an item's coverage and its source-over blend, four pixels of a row
 * at a time. Each lane performs raster.c's scalar operations in the same order, and its branches
 * become selections with the same priority, so the result is the scalar path's, bit for bit
 * (tests/unit/render_soft/test_determinism.c checks it on every platform CI runs). Linear-light
 * blending, groups and the final store stay scalar.
 */
#include "simd4.h"
#include "soft_internal.h"

#if SOFT_SIMD

static v4f clamp01_4(v4f v)
{
    return v4_sel(v4_gt(v, v4_set1(0.0f)), v4_sel(v4_lt(v, v4_set1(1.0f)), v, v4_set1(1.0f)),
                  v4_set1(0.0f));
}

/* raster.c's edge_coverage. */
static v4f edge4(v4f d, const soft_edge *e)
{
    const v4f t = v4_add(d, v4_set1(e->half_sum));
    v4f q1, q2, q3, u, r;
    if (e->axis) {
        return clamp01_4(v4_mul(t, v4_set1(e->inv_hi)));
    }
    q1 = v4_mul(v4_mul(t, t), v4_set1(e->k));
    q2 = v4_mul(v4_sub(t, v4_set1(e->half_lo)), v4_set1(e->inv_hi));
    u = v4_sub(v4_set1(e->sum), t);
    q3 = v4_sub(v4_set1(1.0f), v4_mul(v4_mul(u, u), v4_set1(e->k)));
    r = q3;
    r = v4_sel(v4_le(t, v4_set1(e->hi)), q2, r);
    r = v4_sel(v4_lt(t, v4_set1(e->lo)), q1, r);
    r = v4_sel(v4_ge(t, v4_set1(e->sum)), v4_set1(1.0f), r);
    r = v4_sel(v4_le(t, v4_set1(0.0f)), v4_set1(0.0f), r);
    return r;
}

/* raster.c's edge_coverage_n. */
static v4f edge_n4(v4f d, v4f nx, v4f ny)
{
    const v4f zero = v4_set1(0.0f);
    const v4f ax = v4_sel(v4_lt(nx, zero), v4_neg(nx), nx);
    const v4f ay = v4_sel(v4_lt(ny, zero), v4_neg(ny), ny);
    const v4f lo = v4_sel(v4_lt(ay, ax), ay, ax);
    const v4f hi = v4_sel(v4_gt(ay, ax), ay, ax);
    const v4f sum = v4_add(lo, hi);
    const v4f t = v4_add(d, v4_mul(v4_set1(0.5f), sum));
    const v4f den = v4_mul(v4_mul(v4_set1(2.0f), lo), hi);
    const v4f u = v4_sub(sum, t);
    const v4f q1 = v4_div(v4_mul(t, t), den);
    const v4f q2 = v4_div(v4_sub(t, v4_mul(v4_set1(0.5f), lo)), hi);
    const v4f q3 = v4_sub(v4_set1(1.0f), v4_div(v4_mul(u, u), den));
    v4f r = q3;
    r = v4_sel(v4_le(t, hi), q2, r);
    r = v4_sel(v4_lt(t, lo), q1, r);
    r = v4_sel(v4_ge(t, sum), v4_set1(1.0f), r);
    r = v4_sel(v4_le(t, zero), zero, r);
    r = v4_sel(v4_lt(lo, v4_set1(1e-4f)), clamp01_4(v4_div(t, hi)), r);
    return r;
}

static v4f slab4(v4f x, float lo_edge, float hi_edge, const soft_edge *e)
{
    const v4f rlen = v4_set1(e->rlen);
    const v4f c = v4_sub(v4_add(edge4(v4_mul(v4_sub(x, v4_set1(lo_edge)), rlen), e),
                                edge4(v4_mul(v4_sub(v4_set1(hi_edge), x), rlen), e)),
                         v4_set1(1.0f));
    return v4_sel(v4_gt(c, v4_set1(0.0f)), c, v4_set1(0.0f));
}

static v4f corner4(const soft_shape *s, int k, v4f lx, v4f ly)
{
    const v4f zero = v4_set1(0.0f);
    const v4f cx = v4_set1(s->cx[k]), cy = v4_set1(s->cy[k]);
    const v4f bx = v4_mul(v4_sub(cx, lx), v4_set1(s->ix[k]));
    const v4f by = v4_mul(v4_sub(cy, ly), v4_set1(s->iy[k]));
    const v4m outside = m4_or(v4_le(bx, zero), v4_le(by, zero));
    const v4m inside = m4_and(v4_gt(bx, zero), v4_gt(by, zero));
    v4f vx, vy, dist, gx, gy, grx, gry, len, r;
    v4m far;
    if (!m4_any(inside)) {
        return v4_set1(1.0f);
    }
    vx = v4_sub(lx, cx);
    vy = v4_sub(ly, cy);
    dist = v4_sqrt(v4_add(v4_mul(vx, vx), v4_mul(vy, vy)));
    far = v4_gt(dist, v4_set1(1e-6f));
    gx = v4_sel(far, v4_div(vx, dist), v4_set1(1.0f));
    gy = v4_sel(far, v4_div(vy, dist), zero);
    grx = v4_add(v4_mul(gx, v4_set1(s->ia)), v4_mul(gy, v4_set1(s->ib)));
    gry = v4_add(v4_mul(gx, v4_set1(s->ic)), v4_mul(gy, v4_set1(s->id)));
    len = v4_sqrt(v4_add(v4_mul(grx, grx), v4_mul(gry, gry)));
    r = edge_n4(v4_div(v4_sub(v4_set1(s->r[k]), dist), len), v4_div(grx, len), v4_div(gry, len));
    return v4_sel(outside, v4_set1(1.0f), r);
}

static v4f shape_coverage4(const soft_shape *s, v4f x, v4f y)
{
    const v4f lx =
        v4_add(v4_add(v4_mul(v4_set1(s->ia), x), v4_mul(v4_set1(s->ic), y)), v4_set1(s->itx));
    const v4f ly =
        v4_add(v4_add(v4_mul(v4_set1(s->ib), x), v4_mul(v4_set1(s->id), y)), v4_set1(s->ity));
    v4f cov = v4_mul(slab4(lx, s->x0, s->x1, &s->ex), slab4(ly, s->y0, s->y1, &s->ey));
    int k;
    for (k = 0; k < 4; ++k) {
        if ((s->corners >> k) & 1u) {
            const v4f c = corner4(s, k, lx, ly);
            cov = v4_sel(v4_lt(c, cov), c, cov);
        }
    }
    return cov;
}

void ax__soft_shape_coverage4(const soft_shape *s, const float x[4], float y, float out[4])
{
    v4_store(out, shape_coverage4(s, v4_load(x), v4_set1(y)));
}

void ax__soft_draw_item_simd(const soft_frame *f, const soft_cmd *cmd, float *planes, int32_t ox,
                             int32_t oy, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    const v4f min_cov = v4_set1(SOFT_MIN_COVERAGE);
    const v4f flush = v4_set1(SOFT_FLUSH);
    const v4f zero = v4_set1(0.0f);
    const int32_t xa = x0 & ~3; /* groups of four never cross a row: SOFT_TILE is a multiple */
    int32_t x, y;
    for (y = y0; y < y1; ++y) {
        const v4f py = v4_set1((float)(oy + y) + 0.5f);
        for (x = xa; x < x1; x += 4) {
            const v4f px = v4_add(v4_iota(ox + x), v4_set1(0.5f));
            const size_t i = (size_t)y * SOFT_TILE + (size_t)x;
            v4f cov = shape_coverage4(&cmd->shape, px, py);
            v4m live = m4_and(m4_range(x, x0, x1), v4_ge(cov, min_cov));
            uint32_t c = cmd->clip;
            int k;
            while (c != AX_IR_NO_INDEX && m4_any(live)) {
                cov = v4_mul(cov, shape_coverage4(&f->clips[c].shape, px, py));
                live = m4_and(live, v4_ge(cov, min_cov));
                c = f->clips[c].parent;
            }
            if (!m4_any(live)) {
                continue;
            }
            if (cmd->linear) {
                float covs[4];
                int lane;
                v4_store(covs, v4_sel(live, cov, zero));
                for (lane = 0; lane < 4; ++lane) {
                    if (covs[lane] >= SOFT_MIN_COVERAGE) {
                        ax__soft_blend_pixel(planes, i + (size_t)lane, cmd->color, covs[lane],
                                             true);
                    }
                }
                continue;
            }
            {
                const v4f t = v4_sub(v4_set1(1.0f), v4_mul(v4_set1(cmd->color[3]), cov));
                for (k = 0; k < 4; ++k) {
                    float *p = planes + (size_t)k * SOFT_TILE_PIXELS + i;
                    const v4f d = v4_load(p);
                    v4f v = v4_add(v4_mul(v4_set1(cmd->color[k]), cov), v4_mul(d, t));
                    v = v4_sel(v4_lt(v, flush), zero, v);
                    v4_store(p, v4_sel(live, v, d));
                }
            }
        }
    }
}

bool ax__soft_simd_available(void)
{
    return true;
}

const char *ax__soft_simd_name(void)
{
    return SOFT_SIMD_NAME;
}

#else

void ax__soft_shape_coverage4(const soft_shape *s, const float x[4], float y, float out[4])
{
    int k;
    for (k = 0; k < 4; ++k) {
        out[k] = ax__soft_shape_coverage(s, x[k], y);
    }
}

void ax__soft_draw_item_simd(const soft_frame *f, const soft_cmd *cmd, float *planes, int32_t ox,
                             int32_t oy, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    (void)f;
    (void)cmd;
    (void)planes;
    (void)ox;
    (void)oy;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
}

bool ax__soft_simd_available(void)
{
    return false;
}

const char *ax__soft_simd_name(void)
{
    return SOFT_SIMD_NAME;
}

#endif
