/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Flattens a Render IR snapshot into ax_render_soft's commands: items with their device-space
 * shapes, colors and pixel bounds, clips, and the isolated groups that give exact group opacity.
 *
 * Layers are in pre-order, so walking them in index order and emitting each layer's own items
 * gives the painting order (a layer's items, then its children). A group opened for a layer
 * closes before the first layer past its subtree.
 */
#include <math.h> /* sqrtf only: IEEE requires it correctly rounded */
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "soft_internal.h"

typedef struct layer_state {
    soft_affine world; /* local -> device pixels */
    bool skip;
    bool linear;
    uint32_t clip;     /* innermost clip, or AX_IR_NO_INDEX */
    soft_box clip_box; /* pixels the clips leave (the target's when none) */
} layer_state;

typedef struct open_group {
    uint32_t cmd;         /* the PUSH */
    uint32_t subtree_end; /* the first layer past its subtree */
} open_group;

static bool finite(float v)
{
    return v - v == 0.0f;
}

static float minf(float a, float b)
{
    return b < a ? b : a;
}

static float maxf(float a, float b)
{
    return b > a ? b : a;
}

static float absf(float v)
{
    return v < 0.0f ? -v : v;
}

static float clamp01(float v)
{
    return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
}

static bool box_empty(soft_box b)
{
    return b.x0 >= b.x1 || b.y0 >= b.y1;
}

static soft_box box_intersect(soft_box a, soft_box b)
{
    soft_box r;
    r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
    return r;
}

static soft_box box_union(soft_box a, soft_box b)
{
    soft_box r;
    if (box_empty(a)) {
        return b;
    }
    if (box_empty(b)) {
        return a;
    }
    r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
    r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
    r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
    r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
    return r;
}

/* The pixel bounds [floor(lo), ceil(hi)) of a span, within [0, limit]. */
static void span_to_pixels(float lo, float hi, uint32_t limit, int32_t *out_lo, int32_t *out_hi)
{
    const float l = minf(maxf(lo, 0.0f), (float)limit);
    const float h = minf(maxf(hi, 0.0f), (float)limit);
    int32_t il = (int32_t)l; /* truncation is floor for non-negative values */
    int32_t ih = (int32_t)h;
    if ((float)ih < h) {
        ih++;
    }
    *out_lo = il;
    *out_hi = ih;
}

static void edge_constants(float gx, float gy, soft_edge *e)
{
    const float len = sqrtf(gx * gx + gy * gy);
    const float nx = absf(gx / len), ny = absf(gy / len);
    e->rlen = 1.0f / len;
    e->lo = minf(nx, ny);
    e->hi = maxf(nx, ny);
    e->half_sum = 0.5f * (e->lo + e->hi);
    e->sum = e->lo + e->hi;
    e->half_lo = 0.5f * e->lo;
    e->inv_hi = 1.0f / e->hi;
    e->axis = e->lo < 1e-4f ? 1u : 0u;
    e->k = e->axis ? 0.0f : 1.0f / ((2.0f * e->lo) * e->hi);
}

/* CSS: when the radii on one side add up to more than the side, all of them shrink by the same
 * factor. Negative radii are 0. */
static void clamp_radii(const float in[4], float w, float h, float out[4])
{
    float f = 1.0f;
    int i;
    for (i = 0; i < 4; ++i) {
        out[i] = in[i] > 0.0f ? in[i] : 0.0f;
    }
    if (out[0] + out[1] > w) {
        f = minf(f, w / (out[0] + out[1]));
    }
    if (out[3] + out[2] > w) {
        f = minf(f, w / (out[3] + out[2]));
    }
    if (out[0] + out[3] > h) {
        f = minf(f, h / (out[0] + out[3]));
    }
    if (out[1] + out[2] > h) {
        f = minf(f, h / (out[1] + out[2]));
    }
    for (i = 0; i < 4; ++i) {
        out[i] *= f;
    }
}

static bool within_scale(soft_affine m)
{
    return absf(m.a) <= SOFT_MAX_SCALE && absf(m.b) <= SOFT_MAX_SCALE &&
           absf(m.c) <= SOFT_MAX_SCALE && absf(m.d) <= SOFT_MAX_SCALE && finite(m.tx) &&
           finite(m.ty) && absf(m.tx) <= 1.0e9f && absf(m.ty) <= 1.0e9f;
}

/* Builds a shape for `rect` (and `radii`, or NULL) drawn through `world`, and the pixels it may
 * touch. False when the geometry cannot be drawn (non-finite, empty or degenerate). */
static bool make_shape(ax_rect rect, const float *radii, soft_affine world, uint32_t width,
                       uint32_t height, soft_shape *s, soft_box *box)
{
    static const float inward[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}; /* tl tr br bl */
    soft_affine inv;
    float hx, hy, xs[4], ys[4], lo_x, hi_x, lo_y, hi_y;
    int k;

    if (!finite(rect.x) || !finite(rect.y) || !finite(rect.w) || !finite(rect.h) ||
        !(rect.w > 0.0f) || !(rect.h > 0.0f) || absf(rect.x) > 1.0e9f || absf(rect.y) > 1.0e9f ||
        rect.w > 1.0e9f || rect.h > 1.0e9f) {
        return false;
    }
    if (!within_scale(world) || !ax__soft_affine_invert(world, &inv) || !within_scale(inv)) {
        return false;
    }
    memset(s, 0, sizeof *s);
    s->ia = inv.a;
    s->ib = inv.b;
    s->ic = inv.c;
    s->id = inv.d;
    s->itx = inv.tx;
    s->ity = inv.ty;
    s->x0 = rect.x;
    s->y0 = rect.y;
    s->x1 = rect.x + rect.w;
    s->y1 = rect.y + rect.h;
    /* Gradients in device pixels of local x, (ia, ic), and of local y, (ib, id). */
    edge_constants(inv.a, inv.c, &s->ex);
    edge_constants(inv.b, inv.d, &s->ey);
    if (radii != NULL) {
        clamp_radii(radii, rect.w, rect.h, s->r);
        for (k = 0; k < 4; ++k) {
            const float vx = (k == 0 || k == 3) ? s->x0 : s->x1;
            const float vy = (k == 0 || k == 1) ? s->y0 : s->y1;
            if (!(s->r[k] > 0.0f)) {
                s->r[k] = 0.0f;
                continue;
            }
            s->corners |= 1u << k;
            s->ix[k] = inward[k][0];
            s->iy[k] = inward[k][1];
            s->cx[k] = vx + s->ix[k] * s->r[k];
            s->cy[k] = vy + s->iy[k] * s->r[k];
        }
    }

    /* Grow the rect by twice the pixel footprint's half extent in local units, as the GPU
     * renderer's quads do; every pixel outside gets coverage 0. */
    hx = 2.0f * (0.5f * (absf(inv.a) + absf(inv.c)));
    hy = 2.0f * (0.5f * (absf(inv.b) + absf(inv.d)));
    for (k = 0; k < 4; ++k) {
        const float lx = (k == 0 || k == 3) ? s->x0 - hx : s->x1 + hx;
        const float ly = (k == 0 || k == 1) ? s->y0 - hy : s->y1 + hy;
        xs[k] = world.a * lx + world.c * ly + world.tx;
        ys[k] = world.b * lx + world.d * ly + world.ty;
    }
    lo_x = minf(minf(xs[0], xs[1]), minf(xs[2], xs[3]));
    hi_x = maxf(maxf(xs[0], xs[1]), maxf(xs[2], xs[3]));
    lo_y = minf(minf(ys[0], ys[1]), minf(ys[2], ys[3]));
    hi_y = maxf(maxf(ys[0], ys[1]), maxf(ys[2], ys[3]));
    span_to_pixels(lo_x, hi_x, width, &box->x0, &box->x1);
    span_to_pixels(lo_y, hi_y, height, &box->y0, &box->y1);
    return true;
}

/* A premultiplied linear color, times `opacity`, in the frame's storage encoding. False when it
 * is invisible. */
static bool encode_color(ax_color c, float opacity, bool linear, float out[4])
{
    const float a_raw = c.a * opacity;
    float a;
    int k;
    const float ch[3] = {c.r, c.g, c.b};
    if (!(a_raw >= SOFT_MIN_COLOR)) {
        return false;
    }
    a = a_raw < 1.0f ? a_raw : 1.0f;
    for (k = 0; k < 3; ++k) {
        float v;
        if (linear) {
            v = clamp01(ch[k] * opacity);
            v = v < a ? v : a;
        } else {
            v = ax__soft_linear_to_srgb(ch[k] / c.a) * a;
        }
        out[k] = v < SOFT_MIN_COLOR ? 0.0f : v;
    }
    out[3] = a;
    return true;
}

static soft_affine to_soft(ax_affine2 m)
{
    soft_affine r;
    r.a = m.a;
    r.b = m.b;
    r.c = m.c;
    r.d = m.d;
    r.tx = m.tx;
    r.ty = m.ty;
    return r;
}

typedef struct builder {
    soft_cmd *cmds;
    uint32_t count, capacity;
    open_group *groups;
    uint32_t depth, max_depth;
} builder;

static void close_group(builder *b)
{
    open_group g = b->groups[--b->depth];
    soft_cmd *push = &b->cmds[g.cmd];
    soft_cmd *pop = &b->cmds[b->count];
    memset(pop, 0, sizeof *pop);
    pop->kind = SOFT_CMD_POP;
    pop->clip = AX_IR_NO_INDEX;
    pop->linear = push->linear;
    pop->box = push->box;
    pop->color[3] = push->color[3];
    push->end = b->count;
    b->count++;
    if (b->depth > 0) {
        soft_cmd *outer = &b->cmds[b->groups[b->depth - 1].cmd];
        outer->box = box_union(outer->box, push->box);
    }
}

static void add_item_box(builder *b, soft_box box)
{
    if (b->depth > 0) {
        soft_cmd *g = &b->cmds[b->groups[b->depth - 1].cmd];
        g->box = box_union(g->box, box);
    }
}

ax_result ax__soft_prepare(const ax_render_ir *ir, uint32_t width, uint32_t height, float scale,
                           bool linear_default, ax_arena *arena, soft_frame *out,
                           ax_render_counts *counts)
{
    const soft_box target = {0, 0, (int32_t)width, (int32_t)height};
    soft_affine to_device;
    layer_state *ls;
    soft_clip *clips;
    uint32_t clip_count = 0;
    builder b;
    uint32_t i, max_items;

    memset(&b, 0, sizeof b);
    to_device.a = scale;
    to_device.b = 0.0f;
    to_device.c = 0.0f;
    to_device.d = scale;
    to_device.tx = 0.0f;
    to_device.ty = 0.0f;

    max_items = ir->items_size / (uint32_t)sizeof(ax_ir_item_header) + 1u;
    b.capacity = max_items + 2u * ir->layer_count;
    ls = AX_ARENA_NEW_ARRAY(arena, layer_state, ir->layer_count);
    clips = AX_ARENA_NEW_ARRAY(arena, soft_clip, ir->layer_count);
    b.cmds = AX_ARENA_NEW_ARRAY(arena, soft_cmd, b.capacity);
    b.groups = AX_ARENA_NEW_ARRAY(arena, open_group, ir->layer_count);
    if (ls == NULL || clips == NULL || b.cmds == NULL || b.groups == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft: out of memory");
    }

    for (i = 0; i < ir->layer_count; ++i) {
        const ax_ir_layer *l = ax_ir_layer_at(ir, i);
        const layer_state *p = l->parent != AX_IR_NO_LAYER ? &ls[l->parent] : NULL;
        layer_state *s = &ls[i];
        ax_ir_item_iter it;
        const ax_ir_item_header *h;
        uint32_t index = 0;
        float opacity = l->opacity;
        float item_opacity;
        bool isolated;

        while (b.depth > 0 && b.groups[b.depth - 1].subtree_end <= i) {
            close_group(&b);
        }

        s->skip = p != NULL && p->skip;
        if (s->skip) {
            continue;
        }
        if (!(opacity > 0.0f)) {
            s->skip = true; /* invisible, NaN included */
            continue;
        }
        if (opacity > 1.0f) {
            opacity = 1.0f;
        }
        s->world = ax__soft_affine_mul(p != NULL ? p->world : to_device, to_soft(l->transform));
        s->linear = (p != NULL && p->linear) || (l->flags & AX_IR_LAYER_LINEAR_BLEND) != 0 ||
                    linear_default;
        s->clip = p != NULL ? p->clip : AX_IR_NO_INDEX;
        s->clip_box = p != NULL ? p->clip_box : target;
        if (l->blend_mode != AX_IR_BLEND_NORMAL || l->effects != AX_IR_NO_INDEX) {
            counts->neutralized++;
        }

        if (l->clip.kind == AX_IR_CLIP_RECT || l->clip.kind == AX_IR_CLIP_ROUNDED_RECT) {
            soft_clip *c = &clips[clip_count];
            soft_box cb;
            const bool rounded = l->clip.kind == AX_IR_CLIP_ROUNDED_RECT;
            const ax_rect r = l->clip.rect;
            if (!finite(r.x) || !finite(r.y) || !finite(r.w) || !finite(r.h) ||
                (rounded && !(finite(l->clip.radii[0]) && finite(l->clip.radii[1]) &&
                              finite(l->clip.radii[2]) && finite(l->clip.radii[3])))) {
                counts->skipped_layers++; /* a clip it cannot apply */
                s->skip = true;
                continue;
            }
            if (!make_shape(r, rounded ? l->clip.radii : NULL, s->world, width, height, &c->shape,
                            &cb)) {
                s->skip = true; /* empty or degenerate: the clip hides everything */
                continue;
            }
            c->parent = s->clip;
            s->clip = clip_count++;
            s->clip_box = box_intersect(s->clip_box, cb);
            if (box_empty(s->clip_box)) {
                s->skip = true;
                continue;
            }
        } else if (l->clip.kind != AX_IR_CLIP_NONE) {
            counts->skipped_layers++; /* path clips and unknown kinds */
            s->skip = true;
            continue;
        }

        /* Group opacity: exact by isolation when the layer has more than one item or a child;
         * with a single item, multiplying the item's color is the same thing. */
        isolated = opacity < 1.0f && l->item_count + (l->subtree_end - i - 1u) > 1u;
        if (isolated) {
            soft_cmd *push = &b.cmds[b.count];
            memset(push, 0, sizeof *push);
            push->kind = SOFT_CMD_PUSH;
            push->clip = AX_IR_NO_INDEX;
            push->linear = s->linear ? 1u : 0u;
            push->box.x0 = push->box.y0 = push->box.x1 = push->box.y1 = 0;
            push->color[3] = opacity;
            b.groups[b.depth].cmd = b.count;
            b.groups[b.depth].subtree_end = l->subtree_end;
            b.depth++;
            if (b.depth > b.max_depth) {
                b.max_depth = b.depth;
            }
            b.count++;
            item_opacity = 1.0f;
        } else {
            item_opacity = opacity;
        }

        it = ax_ir_items(ir, l);
        while ((h = ax_ir_item_next(&it)) != NULL) {
            soft_cmd *cmd = &b.cmds[b.count];
            ax_rect rect;
            ax_color color;
            float radii[4] = {0, 0, 0, 0};
            bool rounded = false;
            soft_box box;
            ++index;
            if (h->type == AX_IR_ITEM_RECT) {
                ax_ir_rect_item item;
                memcpy(&item, h, sizeof item);
                rect = item.rect;
                color = item.color;
            } else if (h->type == AX_IR_ITEM_ROUNDED_RECT) {
                ax_ir_rounded_rect_item item;
                memcpy(&item, h, sizeof item);
                rect = item.rect;
                color = item.color;
                memcpy(radii, item.radii, sizeof radii);
                if (!finite(radii[0]) || !finite(radii[1]) || !finite(radii[2]) ||
                    !finite(radii[3])) {
                    counts->invalid_items++;
                    continue;
                }
                rounded = true;
            } else {
                counts->unknown_items++;
                if ((h->flags & AX_IR_ITEM_REQUIRED) != 0) {
                    counts->skipped_items += l->item_count - index;
                    break;
                }
                continue;
            }
            if (!finite(color.r) || !finite(color.g) || !finite(color.b) || !finite(color.a)) {
                counts->invalid_items++;
                continue;
            }
            memset(cmd, 0, sizeof *cmd);
            if (!make_shape(rect, rounded ? radii : NULL, s->world, width, height, &cmd->shape,
                            &box)) {
                counts->invalid_items++;
                continue;
            }
            if (!encode_color(color, item_opacity, s->linear, cmd->color)) {
                continue; /* invisible with source-over blending */
            }
            counts->instances++;
            box = box_intersect(box, s->clip_box);
            if (box_empty(box)) {
                continue; /* drawn, but nothing of it lands on the target */
            }
            cmd->kind = SOFT_CMD_ITEM;
            cmd->clip = s->clip;
            cmd->linear = s->linear ? 1u : 0u;
            cmd->end = 0;
            cmd->box = box;
            add_item_box(&b, box);
            b.count++;
        }
    }
    while (b.depth > 0) {
        close_group(&b);
    }

    out->cmds = b.cmds;
    out->cmd_count = b.count;
    out->clips = clips;
    out->group_depth = b.max_depth;
    return AX_OK;
}
