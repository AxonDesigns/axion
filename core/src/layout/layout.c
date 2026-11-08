/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The layout engine's core (decision 90): the entry for every node with its measurement cache, the
 * leaf cases, the pass over layout roots and relayout boundaries (in parallel when they are
 * independent), rounding to the pixel grid, and the changed frames.
 */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "layout_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_assert.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_jobs.h>
#include <axion/base/ax_memory.h>

/* Protocols by DISPLAY value. AX_DISPLAY_NONE never reaches a protocol: the parent zeroes it. */
static const ax_layout_protocol *const PROTOCOLS[] = {
    &ax__layout_flex, /* AX_DISPLAY_FLEX */
    NULL,             /* AX_DISPLAY_NONE */
};

typedef struct measure_component {
    ax_layout_measure_fn fn;
    void *user;
} measure_component;

/* ---- Context accessors (SDK) ---------------------------------------------------------------- */

ax_node_store *ax_layout_store(ax_layout_ctx *ctx)
{
    return ctx->s;
}

ax_node_layout *ax_layout_box(ax_layout_ctx *ctx, uint32_t row)
{
    return axl_box(ctx, row);
}

const ax_node_style *ax_layout_style(ax_layout_ctx *ctx, uint32_t row)
{
    return axl_style(ctx, row);
}

bool ax_layout_is_tree_root(ax_layout_ctx *ctx, uint32_t row)
{
    return ctx->s->links[row].parent == AX_NODE_NONE;
}

const ax_layout_measure_fn *axl_measure_of(ax_layout_ctx *c, uint32_t row, void **out_user)
{
    const ax_node_store *s = c->s;
    measure_component *m;
    if ((s->component_mask[row] & (1u << c->scene->measure_component)) == 0) {
        return NULL;
    }
    m = (measure_component *)ax_node_store_peek_component(s, s->slot_of_row[row],
                                                          c->scene->measure_component);
    if (m == NULL || m->fn == NULL) {
        return NULL;
    }
    *out_user = m->user;
    return &m->fn;
}

static void reset_cache(ax_node_layout *b)
{
    uint32_t i;
    b->next_measure = 0;
    b->last.valid = 0;
    for (i = 0; i < AX_LAYOUT_MEASURE_CACHE; ++i) {
        b->measures[i].valid = 0;
    }
}

void ax_layout_zero_subtree(ax_layout_ctx *ctx, uint32_t row)
{
    ax_node_store *s = ctx->s;
    uint32_t slot;
    ax_node_layout *b = axl_box(ctx, row);
    memset(b->pos, 0, sizeof b->pos);
    b->measured_w = b->measured_h = 0.0f;
    b->flex_basis = AXL_UNDEF;
    memset(b->margin, 0, sizeof b->margin);
    memset(b->padding, 0, sizeof b->padding);
    memset(b->border, 0, sizeof b->border);
    reset_cache(b);
    b->pass = ctx->pass;
    for (slot = s->links[row].first_child; slot != AX_NODE_NONE;
         slot = s->links[axl_row(s, slot)].next_sibling) {
        ax_layout_zero_subtree(ctx, axl_row(s, slot));
    }
}

/* ---- Leaves --------------------------------------------------------------------------------- */

static float pb_row(const ax_node_layout *b)
{
    return b->padding[AX_EDGE_LEFT] + b->padding[AX_EDGE_RIGHT] + b->border[AX_EDGE_LEFT] +
           b->border[AX_EDGE_RIGHT];
}

static float pb_col(const ax_node_layout *b)
{
    return b->padding[AX_EDGE_TOP] + b->padding[AX_EDGE_BOTTOM] + b->border[AX_EDGE_TOP] +
           b->border[AX_EDGE_BOTTOM];
}

static void measure_leaf_fn(ax_layout_ctx *ctx, uint32_t row, ax_layout_measure_fn fn, void *user,
                            float aw, float ah, ax_layout_mode wm, ax_layout_mode hm, float ow,
                            float oh)
{
    const ax_node_style *st = axl_style(ctx, row);
    ax_node_layout *b = axl_box(ctx, row);
    float pbr = pb_row(b);
    float pbc = pb_col(b);
    float inner_w, inner_h;
    if (wm == AX_LAYOUT_UNDEFINED) {
        aw = AXL_UNDEF;
    }
    if (hm == AX_LAYOUT_UNDEFINED) {
        ah = AXL_UNDEF;
    }
    inner_w = axl_undef(aw) ? aw : axl_max_or_def(0.0f, aw - pbr);
    inner_h = axl_undef(ah) ? ah : axl_max_or_def(0.0f, ah - pbc);
    if (wm == AX_LAYOUT_EXACT && hm == AX_LAYOUT_EXACT) {
        b->measured_w = axl_bound(st, AX_FLEX_DIRECTION_ROW, aw, ow, ow);
        b->measured_h = axl_bound(st, AX_FLEX_DIRECTION_COLUMN, ah, oh, ow);
        return;
    }
    {
        float mw = 0.0f, mh = 0.0f;
        ax_node n = ax_node_store_handle(ctx->s, ctx->s->slot_of_row[row]);
        ax_node_store_enter_measure(); /* the contract's check (ax_layout.h) */
        fn(user, n, inner_w, wm, inner_h, hm, &mw, &mh);
        ax_node_store_leave_measure();
        ctx->counters.measure_calls++;
        b->measured_w =
            axl_bound(st, AX_FLEX_DIRECTION_ROW, wm == AX_LAYOUT_EXACT ? aw : mw + pbr, ow, ow);
        b->measured_h =
            axl_bound(st, AX_FLEX_DIRECTION_COLUMN, hm == AX_LAYOUT_EXACT ? ah : mh + pbc, oh, ow);
    }
}

static void measure_leaf_empty(ax_layout_ctx *ctx, uint32_t row, float aw, float ah,
                               ax_layout_mode wm, ax_layout_mode hm, float ow, float oh)
{
    const ax_node_style *st = axl_style(ctx, row);
    ax_node_layout *b = axl_box(ctx, row);
    float w = aw;
    float h = ah;
    if (wm != AX_LAYOUT_EXACT) {
        w = pb_row(b);
    }
    if (hm != AX_LAYOUT_EXACT) {
        h = pb_col(b);
    }
    b->measured_w = axl_bound(st, AX_FLEX_DIRECTION_ROW, w, ow, ow);
    b->measured_h = axl_bound(st, AX_FLEX_DIRECTION_COLUMN, h, oh, ow);
}

/* A measure (not a layout) whose answer needs no children: both sizes exact, or no space. */
static bool measure_fixed(ax_layout_ctx *ctx, uint32_t row, float aw, float ah, ax_layout_mode wm,
                          ax_layout_mode hm, float ow, float oh)
{
    const ax_node_style *st = axl_style(ctx, row);
    ax_node_layout *b = axl_box(ctx, row);
    if ((axl_def(aw) && wm == AX_LAYOUT_AT_MOST && aw <= 0.0f) ||
        (axl_def(ah) && hm == AX_LAYOUT_AT_MOST && ah <= 0.0f) ||
        (wm == AX_LAYOUT_EXACT && hm == AX_LAYOUT_EXACT)) {
        float w = axl_undef(aw) || (wm == AX_LAYOUT_AT_MOST && aw < 0.0f) ? 0.0f : aw;
        float h = axl_undef(ah) || (hm == AX_LAYOUT_AT_MOST && ah < 0.0f) ? 0.0f : ah;
        b->measured_w = axl_bound(st, AX_FLEX_DIRECTION_ROW, w, ow, ow);
        b->measured_h = axl_bound(st, AX_FLEX_DIRECTION_COLUMN, h, oh, ow);
        return true;
    }
    return false;
}

/* ---- The entry for every node --------------------------------------------------------------- */

static void layout_impl(ax_layout_ctx *ctx, uint32_t row, const ax_layout_request *req)
{
    const ax_node_style *st = axl_style(ctx, row);
    ax_node_layout *b = axl_box(ctx, row);
    ax_node_store *s = ctx->s;
    float ow = req->owner_w;
    float margin_row, margin_col;
    const ax_layout_measure_fn *fn;
    void *user = NULL;
    int e;

    if (req->perform) {
        ctx->counters.layouts++;
    } else {
        ctx->counters.measures++;
    }
    for (e = 0; e < 4; ++e) {
        b->margin[e] = axl_margin(st, e, ow);
        b->border[e] = axl_border(st, e);
        b->padding[e] = axl_padding(st, e, ow);
    }
    margin_row = b->margin[AX_EDGE_LEFT] + b->margin[AX_EDGE_RIGHT];
    margin_col = b->margin[AX_EDGE_TOP] + b->margin[AX_EDGE_BOTTOM];

    fn = axl_measure_of(ctx, row, &user);
    if (fn != NULL) {
        measure_leaf_fn(ctx, row, *fn, user, req->avail_w - margin_row, req->avail_h - margin_col,
                        req->mode_w, req->mode_h, ow, req->owner_h);
        return;
    }
    if (s->links[row].first_child == AX_NODE_NONE) {
        measure_leaf_empty(ctx, row, req->avail_w - margin_row, req->avail_h - margin_col,
                           req->mode_w, req->mode_h, ow, req->owner_h);
        return;
    }
    if (!req->perform &&
        measure_fixed(ctx, row, req->avail_w - margin_row, req->avail_h - margin_col, req->mode_w,
                      req->mode_h, ow, req->owner_h)) {
        return;
    }
    {
        uint32_t display = st->display;
        const ax_layout_protocol *p = display < AX_COUNTOF(PROTOCOLS) ? PROTOCOLS[display] : NULL;
        if (p == NULL) {
            ax_layout_zero_subtree(ctx, row);
            return;
        }
        p->layout(ctx, row, req);
    }
}

static float round_grid(double value, double scale, bool force_ceil, bool force_floor);

/* Whether an older measurement of a measure-function leaf still answers the request (Yoga's
 * rules). */
static bool size_exact_and_matches(ax_layout_mode mode, float size, float last_computed)
{
    return mode == AX_LAYOUT_EXACT && axl_inexact_eq(size, last_computed);
}

static bool old_max_content_still_fits(ax_layout_mode mode, float size, ax_layout_mode last_mode,
                                       float last_computed)
{
    return mode == AX_LAYOUT_AT_MOST && last_mode == AX_LAYOUT_UNDEFINED &&
           (size >= last_computed || axl_inexact_eq(size, last_computed));
}

static bool new_stricter_still_valid(ax_layout_mode mode, float size, ax_layout_mode last_mode,
                                     float last_size, float last_computed)
{
    return last_mode == AX_LAYOUT_AT_MOST && mode == AX_LAYOUT_AT_MOST && axl_def(last_size) &&
           axl_def(size) && axl_def(last_computed) && last_size > size &&
           (last_computed <= size || axl_inexact_eq(size, last_computed));
}

static bool can_use_measurement(const ax_layout_ctx *ctx, const ax_layout_request *req,
                                const ax_layout_cache_entry *e, float margin_row, float margin_col)
{
    float aw = req->avail_w, ah = req->avail_h, law = e->avail_w, lah = e->avail_h;
    bool same_w, same_h, w_ok, h_ok;
    if (!e->valid || (axl_def(e->h) && e->h < 0.0f) || (axl_def(e->w) && e->w < 0.0f)) {
        return false;
    }
    if (ctx->scale != 0.0f) {
        aw = round_grid(aw, ctx->scale, false, false);
        ah = round_grid(ah, ctx->scale, false, false);
        law = round_grid(law, ctx->scale, false, false);
        lah = round_grid(lah, ctx->scale, false, false);
    }
    same_w = e->mode_w == req->mode_w && axl_inexact_eq(law, aw);
    same_h = e->mode_h == req->mode_h && axl_inexact_eq(lah, ah);
    w_ok = same_w || size_exact_and_matches(req->mode_w, req->avail_w - margin_row, e->w) ||
           old_max_content_still_fits(req->mode_w, req->avail_w - margin_row,
                                      (ax_layout_mode)e->mode_w, e->w) ||
           new_stricter_still_valid(req->mode_w, req->avail_w - margin_row,
                                    (ax_layout_mode)e->mode_w, e->avail_w, e->w);
    h_ok = same_h || size_exact_and_matches(req->mode_h, req->avail_h - margin_col, e->h) ||
           old_max_content_still_fits(req->mode_h, req->avail_h - margin_col,
                                      (ax_layout_mode)e->mode_h, e->h) ||
           new_stricter_still_valid(req->mode_h, req->avail_h - margin_col,
                                    (ax_layout_mode)e->mode_h, e->avail_h, e->h);
    return w_ok && h_ok;
}

static bool same_request(const ax_layout_cache_entry *e, const ax_layout_request *req)
{
    return e->valid && axl_inexact_eq(e->avail_w, req->avail_w) &&
           axl_inexact_eq(e->avail_h, req->avail_h) && e->mode_w == req->mode_w &&
           e->mode_h == req->mode_h;
}

static void store_entry(ax_layout_cache_entry *e, const ax_layout_request *req,
                        const ax_node_layout *b)
{
    e->avail_w = req->avail_w;
    e->avail_h = req->avail_h;
    e->mode_w = (uint8_t)req->mode_w;
    e->mode_h = (uint8_t)req->mode_h;
    e->valid = 1;
    e->w = b->measured_w;
    e->h = b->measured_h;
}

bool ax_layout_node(ax_layout_ctx *ctx, uint32_t row, const ax_layout_request *req)
{
    ax_node_store *s = ctx->s;
    ax_node_layout *b = axl_box(ctx, row);
    bool dirty = ctx->force || (s->dirty[row] & (AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_CHILD)) != 0;
    bool visit = dirty && b->pass != ctx->pass;
    const ax_layout_cache_entry *cached = NULL;
    void *user = NULL;
    uint32_t i;

    if (visit) {
        reset_cache(b);
    }
    if (axl_measure_of(ctx, row, &user) != NULL) {
        const ax_node_style *st = axl_style(ctx, row);
        float mr = axl_margin_axis(st, AX_FLEX_DIRECTION_ROW, req->owner_w);
        float mc = axl_margin_axis(st, AX_FLEX_DIRECTION_COLUMN, req->owner_w);
        if (can_use_measurement(ctx, req, &b->last, mr, mc)) {
            cached = &b->last;
        } else {
            for (i = 0; i < b->next_measure && i < AX_LAYOUT_MEASURE_CACHE; ++i) {
                if (can_use_measurement(ctx, req, &b->measures[i], mr, mc)) {
                    cached = &b->measures[i];
                    break;
                }
            }
        }
    } else if (req->perform) {
        if (same_request(&b->last, req)) {
            cached = &b->last;
        }
    } else {
        for (i = 0; i < b->next_measure && i < AX_LAYOUT_MEASURE_CACHE; ++i) {
            if (same_request(&b->measures[i], req)) {
                cached = &b->measures[i];
                break;
            }
        }
    }

    if (!visit && cached != NULL) {
        b->measured_w = cached->w;
        b->measured_h = cached->h;
        ctx->counters.cached++;
    } else {
        layout_impl(ctx, row, req);
        if (cached == NULL) {
            if (req->perform) {
                store_entry(&b->last, req, b);
            } else {
                if (b->next_measure == AX_LAYOUT_MEASURE_CACHE) {
                    b->next_measure = 0;
                }
                store_entry(&b->measures[b->next_measure++], req, b);
            }
        }
    }
    if (req->perform) {
        store_entry(&b->performed, req, b);
        b->owner_w = req->owner_w;
        b->owner_h = req->owner_h;
        b->laid_out = 1;
    }
    b->pass = ctx->pass;
    return visit || cached == NULL;
}

/* ---- Rounding and output -------------------------------------------------------------------- */

/* Yoga's rounding to the pixel grid, with floor() for the fraction (exact everywhere). */
static float round_grid(double value, double scale, bool force_ceil, bool force_floor)
{
    double scaled = value * scale;
    double frac;
    if (scaled != scaled) {
        return AXL_UNDEF;
    }
    frac = scaled - floor(scaled);
    if (fabs(frac) < 0.0001) {
        scaled = scaled - frac;
    } else if (fabs(frac - 1.0) < 0.0001) {
        scaled = scaled - frac + 1.0;
    } else if (force_ceil) {
        scaled = scaled - frac + 1.0;
    } else if (force_floor) {
        scaled = scaled - frac;
    } else {
        scaled = scaled - frac + ((frac > 0.5 || fabs(frac - 0.5) < 0.0001) ? 1.0 : 0.0);
    }
    return (float)(scaled / scale);
}

static void note_changed(ax_layout_ctx *ctx, uint32_t row)
{
    ax_node_layout *b = axl_box(ctx, row);
    uint32_t slot = ctx->s->slot_of_row[row];
    if (b->changed_pass == ctx->pass) {
        return; /* listed already in this pass */
    }
    ctx->counters.frames_changed++;
    b->changed_pass = ctx->pass;
    b->changed_next = AX_NODE_NONE;
    if (ctx->changed_tail == AX_NODE_NONE) {
        ctx->changed_head = slot;
    } else {
        axl_box(ctx, axl_row(ctx->s, ctx->changed_tail))->changed_next = slot;
    }
    ctx->changed_tail = slot;
}

/* Rounds one node from its unrounded results and its parent's unrounded absolute position, and
 * writes the frame. Returns whether its absolute position changed. */
static bool round_node(ax_layout_ctx *ctx, uint32_t row, double abs_left, double abs_top)
{
    ax_node_layout *b = axl_box(ctx, row);
    double left = b->pos[AX_EDGE_LEFT];
    double top = b->pos[AX_EDGE_TOP];
    double w = b->measured_w;
    double h = b->measured_h;
    double ax = abs_left + left;
    double ay = abs_top + top;
    bool moved = ax != b->abs_x || ay != b->abs_y;
    ax_rect frame;
    if (ctx->scale != 0.0f) {
        double sc = ctx->scale;
        frame.x = round_grid(left, sc, false, false);
        frame.y = round_grid(top, sc, false, false);
        frame.w = round_grid(ax + w, sc, false, false) - round_grid(ax, sc, false, false);
        frame.h = round_grid(ay + h, sc, false, false) - round_grid(ay, sc, false, false);
    } else {
        frame = ax_rect_make((float)left, (float)top, (float)w, (float)h);
    }
    b->abs_x = ax;
    b->abs_y = ay;
    if (memcmp(&b->frame, &frame, sizeof frame) != 0) {
        /* What layout animations start from (decision 94): bit 0 of had_frame says a frame was
         * written before, bit 1 that prev_frame holds one. */
        b->prev_frame = b->frame;
        b->prev_parent = b->frame_parent;
        b->had_frame = 1u | ((b->had_frame & 1u) << 1);
        b->frame = frame;
        note_changed(ctx, row);
    }
    b->frame_parent = ctx->s->links[row].parent;
    return moved;
}

/* Rounds a subtree: descends into a child laid out or visited in this pass, or whose absolute
 * position moved; any other subtree is unchanged and keeps its frames. Iterative, pre-order. */
static void round_subtree(ax_layout_ctx *ctx, uint32_t root_row, double abs_left, double abs_top,
                          bool everything)
{
    ax_node_store *s = ctx->s;
    uint32_t root_slot = s->slot_of_row[root_row];
    uint32_t cur = s->links[root_row].first_child;
    (void)round_node(ctx, root_row, abs_left, abs_top);
    while (cur != AX_NODE_NONE) {
        uint32_t row = axl_row(s, cur);
        ax_node_layout *cb = axl_box(ctx, row);
        const ax_node_layout *pb = axl_box(ctx, axl_row(s, s->links[row].parent));
        bool need = everything || cb->pass == ctx->pass ||
                    pb->abs_x + (double)cb->pos[AX_EDGE_LEFT] != cb->abs_x ||
                    pb->abs_y + (double)cb->pos[AX_EDGE_TOP] != cb->abs_y;
        if (need) {
            (void)round_node(ctx, row, pb->abs_x, pb->abs_y);
            if (s->links[row].first_child != AX_NODE_NONE) {
                cur = s->links[row].first_child;
                continue;
            }
        }
        while (cur != root_slot && s->links[axl_row(s, cur)].next_sibling == AX_NODE_NONE) {
            cur = s->links[axl_row(s, cur)].parent;
        }
        if (cur == root_slot) {
            break;
        }
        cur = s->links[axl_row(s, cur)].next_sibling;
    }
}

/* ---- Roots ---------------------------------------------------------------------------------- */

static void ctx_init(ax_layout_ctx *ctx, ax_scene *scene)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->scene = scene;
    ctx->s = &scene->store;
    ctx->box = scene->store.layout;
    ctx->pass = scene->layout_pass;
    ctx->viewport_row = AX_NODE_NONE;
    ctx->tree_root_row = AX_NODE_NONE;
    ctx->scale = 1.0f;
    ctx->changed_head = ctx->changed_tail = AX_NODE_NONE;
}

static void ctx_free(ax_layout_ctx *ctx)
{
    (void)ctx; /* nothing allocated */
}

/* The node's position offsets from margins and relative insets (Yoga's setPosition), for a tree
 * root. */
static void place_root(ax_layout_ctx *ctx, uint32_t row, float ow, float oh)
{
    const ax_node_style *st = axl_style(ctx, row);
    ax_node_layout *b = axl_box(ctx, row);
    float rel_x = 0.0f, rel_y = 0.0f;
    if (st->position != AX_POSITION_STATIC) {
        rel_x = axl_inset_defined(st, AX_EDGE_LEFT) ? axl_inset(st, AX_EDGE_LEFT, ow)
                                                    : -axl_inset(st, AX_EDGE_RIGHT, ow);
        rel_y = axl_inset_defined(st, AX_EDGE_TOP) ? axl_inset(st, AX_EDGE_TOP, oh)
                                                   : -axl_inset(st, AX_EDGE_BOTTOM, oh);
    }
    b->pos[AX_EDGE_LEFT] = axl_margin(st, AX_EDGE_LEFT, ow) + rel_x;
    b->pos[AX_EDGE_RIGHT] = axl_margin(st, AX_EDGE_RIGHT, ow) + rel_x;
    b->pos[AX_EDGE_TOP] = axl_margin(st, AX_EDGE_TOP, ow) + rel_y;
    b->pos[AX_EDGE_BOTTOM] = axl_margin(st, AX_EDGE_BOTTOM, ow) + rel_y;
}

/* Lays out a whole tree from its parentless root (Yoga's calculateLayout). */
static void layout_tree(ax_layout_ctx *ctx, uint32_t row, float ow, float oh, bool everything)
{
    const ax_node_style *st;
    ax_layout_request req;
    ctx->tree_root_row = row;
    st = axl_style(ctx, row);
    memset(&req, 0, sizeof req);
    req.owner_w = ow;
    req.owner_h = oh;
    req.perform = true;
    if (axl_definite(st, 0, ow)) {
        req.avail_w =
            axl_resolved_size(st, 0, ow, ow) + axl_margin_axis(st, AX_FLEX_DIRECTION_ROW, ow);
        req.mode_w = AX_LAYOUT_EXACT;
    } else if (axl_def(axl_resolved_max(st, 0, ow, ow))) {
        req.avail_w = axl_resolved_max(st, 0, ow, ow);
        req.mode_w = AX_LAYOUT_AT_MOST;
    } else {
        req.avail_w = ow;
        req.mode_w = axl_undef(ow) ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_EXACT;
    }
    if (axl_definite(st, 1, oh)) {
        req.avail_h =
            axl_resolved_size(st, 1, oh, ow) + axl_margin_axis(st, AX_FLEX_DIRECTION_COLUMN, ow);
        req.mode_h = AX_LAYOUT_EXACT;
    } else if (axl_def(axl_resolved_max(st, 1, oh, ow))) {
        req.avail_h = axl_resolved_max(st, 1, oh, ow);
        req.mode_h = AX_LAYOUT_AT_MOST;
    } else {
        req.avail_h = oh;
        req.mode_h = axl_undef(oh) ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_EXACT;
    }
    if (ax_layout_node(ctx, row, &req) || everything) {
        place_root(ctx, row, ow, oh);
    }
    ctx->counters.roots++;
    round_subtree(ctx, row, 0.0, 0.0, everything);
}

/* Lays a relayout boundary out again with the constraints it last had; its own size and place do
 * not change. */
static void layout_boundary(ax_layout_ctx *ctx, uint32_t row)
{
    ax_node_store *s = ctx->s;
    ax_node_layout *b = axl_box(ctx, row);
    const ax_node_layout *pb = axl_box(ctx, axl_row(s, s->links[row].parent));
    ax_layout_request req;
    memset(&req, 0, sizeof req);
    req.avail_w = b->performed.avail_w;
    req.avail_h = b->performed.avail_h;
    req.mode_w = (ax_layout_mode)b->performed.mode_w;
    req.mode_h = (ax_layout_mode)b->performed.mode_h;
    req.owner_w = b->owner_w;
    req.owner_h = b->owner_h;
    req.perform = true;
    (void)ax_layout_node(ctx, row, &req);
    ctx->counters.roots++;
    round_subtree(ctx, row, pb->abs_x, pb->abs_y, false);
}

/* The tree root above a row. */
static uint32_t tree_root_of(const ax_node_store *s, uint32_t row)
{
    while (s->links[row].parent != AX_NODE_NONE) {
        row = axl_row(s, s->links[row].parent);
    }
    return row;
}

/* Prepares the context for the tree whose root is root_row: the viewport and the pixel scale. */
static void ctx_for_tree(ax_layout_ctx *ctx, const ax_layout_pass_desc *desc, uint32_t root_row,
                         float *ow, float *oh)
{
    float w = 0.0f, h = 0.0f, scale = 1.0f;
    ax_node root = ax_node_store_handle(ctx->s, ctx->s->slot_of_row[root_row]);
    ctx->tree_root_row = root_row;
    ctx->viewport_row = AX_NODE_NONE;
    ctx->scale = 1.0f;
    *ow = *oh = AXL_UNDEF;
    if (desc != NULL && desc->viewport != NULL &&
        desc->viewport(desc->viewport_user, root, &w, &h, &scale)) {
        ax_node_style *vs = &ctx->viewport_style;
        int e;
        *vs = ctx->s->style[root_row];
        vs->width.value = w;
        vs->width.unit = AX_UNIT_POINTS;
        vs->height.value = h;
        vs->height.unit = AX_UNIT_POINTS;
        vs->min_width.unit = vs->min_height.unit = AX_UNIT_AUTO;
        vs->max_width.unit = vs->max_height.unit = AX_UNIT_AUTO;
        vs->box_sizing = AX_BOX_SIZING_BORDER_BOX;
        vs->position = AX_POSITION_RELATIVE;
        for (e = 0; e < 4; ++e) {
            vs->margin[e].value = 0.0f;
            vs->margin[e].unit = AX_UNIT_POINTS;
            vs->inset[e].unit = AX_UNIT_AUTO;
        }
        ctx->viewport_row = root_row;
        ctx->scale = scale > 0.0f ? scale : 1.0f;
        *ow = w;
        *oh = h;
    }
}

/* ---- The pass ------------------------------------------------------------------------------- */

typedef struct root_job {
    ax_layout_ctx ctx;
    const ax_layout_pass_desc *desc;
    uint32_t row;
    bool tree; /* a tree root, not a boundary */
} root_job;

static void run_root(root_job *j)
{
    uint64_t before = j->ctx.counters.layouts + j->ctx.counters.measures;
    if (j->tree) {
        float ow, oh;
        ctx_for_tree(&j->ctx, j->desc, j->row, &ow, &oh);
        layout_tree(&j->ctx, j->row, ow, oh, j->ctx.force);
    } else {
        float ow, oh;
        ctx_for_tree(&j->ctx, j->desc, tree_root_of(j->ctx.s, j->row), &ow, &oh);
        layout_boundary(&j->ctx, j->row);
    }
    /* The work it took, for deciding next time whether it is worth a job. */
    j->ctx.box[j->row].last_work =
        (uint32_t)(j->ctx.counters.layouts + j->ctx.counters.measures - before);
}

static void root_job_fn(void *arg)
{
    run_root((root_job *)arg);
}

static uint32_t depth_of(const ax_node_store *s, uint32_t row)
{
    uint32_t d = 0;
    while (s->links[row].parent != AX_NODE_NONE) {
        row = axl_row(s, s->links[row].parent);
        d++;
    }
    return d;
}

static void merge(ax_layout_ctx *into, ax_layout_ctx *from)
{
    into->counters.layouts += from->counters.layouts;
    into->counters.measures += from->counters.measures;
    into->counters.cached += from->counters.cached;
    into->counters.measure_calls += from->counters.measure_calls;
    into->counters.roots += from->counters.roots;
    if (from->changed_head != AX_NODE_NONE) {
        if (into->changed_tail == AX_NODE_NONE) {
            into->changed_head = from->changed_head;
        } else {
            axl_box(into, axl_row(into->s, into->changed_tail))->changed_next = from->changed_head;
        }
        into->changed_tail = from->changed_tail;
    }
    into->counters.frames_changed += from->counters.frames_changed;
    ctx_free(from);
}

typedef struct pending_root {
    uint32_t row;
    uint32_t depth;
    bool tree;
} pending_root;

static int compare_depth(const void *a, const void *b)
{
    const pending_root *x = (const pending_root *)a;
    const pending_root *y = (const pending_root *)b;
    if (x->depth != y->depth) {
        return x->depth < y->depth ? -1 : 1;
    }
    return x->row < y->row ? -1 : (x->row > y->row ? 1 : 0);
}

uint32_t ax_layout_run(ax_scene *scene, const ax_layout_pass_desc *desc)
{
    ax_node_store *s = &scene->store;
    const ax_dirty_set *d = &scene->dirty;
    ax_layout_ctx main_ctx;
    pending_root *roots = NULL;
    uint32_t root_count = 0, root_cap = 0;
    uint32_t min_nodes =
        desc != NULL && desc->parallel_min_nodes != 0 ? desc->parallel_min_nodes : 100u;
    uint32_t i;
    uint32_t changed;

    scene->layout_pass++;
    if (scene->layout_pass == 0) {
        scene->layout_pass = 1; /* 0 means "never visited" */
    }
    ctx_init(&main_ctx, scene);
    main_ctx.counters.passes = 1;
    main_ctx.force = d->roots_overflowed; /* flags may be missing: lay every tree out */

    /* The roots to lay out: live parentless roots as trees; boundaries whose parent is not itself
     * dirty (a dirty ancestor lays them out when it reaches them). */
    {
        uint32_t n = d->roots_overflowed ? s->row_count : d->layout_root_count;
        root_cap = n;
        if (n > 0) {
            roots = (pending_root *)ax_mem_alloc(&s->allocator, sizeof(pending_root) * n,
                                                 AX_ALIGNOF(pending_root));
            if (roots == NULL) {
                /* No memory for the list: lay every tree out, forced, which needs none. */
                (void)ax_set_error(AX_ERR_OUT_OF_MEMORY, "layout: out of memory; laying out all");
                main_ctx.force = true;
                for (i = 0; i < s->row_count; ++i) {
                    if (s->links[i].parent == AX_NODE_NONE) {
                        root_job j;
                        j.ctx = main_ctx;
                        j.desc = desc;
                        j.row = i;
                        j.tree = true;
                        run_root(&j);
                        main_ctx = j.ctx;
                    }
                }
            }
        }
        for (i = 0; roots != NULL && i < n; ++i) {
            uint32_t row;
            bool tree;
            if (d->roots_overflowed) {
                row = i;
                if (s->links[row].parent != AX_NODE_NONE) {
                    continue;
                }
            } else {
                ax_node r = d->layout_roots[i];
                row = ax_node_store_row(s, r);
                if (row == AX_NODE_NONE) {
                    continue; /* destroyed since it was listed */
                }
            }
            tree = s->links[row].parent == AX_NODE_NONE;
            if (!tree) {
                uint32_t parent_row = axl_row(s, s->links[row].parent);
                if ((s->dirty[parent_row] & (AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_CHILD)) != 0) {
                    continue; /* a dirty ancestor reaches it */
                }
                if (!s->layout[row].laid_out || s->style[row].display == AX_DISPLAY_NONE) {
                    /* Never laid out: lay its whole tree out instead. */
                    row = tree_root_of(s, row);
                    tree = true;
                }
            }
            roots[root_count].row = row;
            roots[root_count].tree = tree;
            roots[root_count].depth = tree ? 0u : depth_of(s, row);
            root_count++;
        }
    }

    if (root_count > 1) {
        uint32_t w = 0;
        qsort(roots, root_count, sizeof *roots, compare_depth);
        /* The same row may appear twice (a boundary that fell back to its tree). */
        for (i = 0; i < root_count; ++i) {
            if (w > 0 && roots[w - 1].row == roots[i].row) {
                continue;
            }
            roots[w++] = roots[i];
        }
        root_count = w;
    }

    /* Waves of equal depth: no root of a wave is an ancestor of another, so a wave's roots write
     * disjoint subtrees and run in parallel. A deeper root is laid out after its ancestors, and is
     * skipped if one of them reached it in this pass. */
    i = 0;
    while (i < root_count) {
        uint32_t end = i;
        uint32_t big = 0;
        uint32_t k;
        while (end < root_count && roots[end].depth == roots[i].depth) {
            end++;
        }
        for (k = i; k < end; ++k) {
            if (s->layout[roots[k].row].pass == scene->layout_pass) {
                roots[k].row = AX_NODE_NONE; /* an ancestor laid it out already */
            }
        }
        for (k = i; k < end; ++k) {
            /* Worth a job: it took many nodes last time, or it has never been a root (unknown). */
            if (roots[k].row != AX_NODE_NONE && (s->layout[roots[k].row].last_work >= min_nodes ||
                                                 s->layout[roots[k].row].last_work == 0)) {
                big++;
            }
        }
        if (desc != NULL && desc->jobs != NULL && big >= 2) {
            root_job *jobs_state = (root_job *)ax_mem_alloc(
                &s->allocator, sizeof(root_job) * (end - i), AX_ALIGNOF(root_job));
            ax_job *jobs = (ax_job *)ax_mem_alloc(&s->allocator, sizeof(ax_job) * (end - i),
                                                  AX_ALIGNOF(ax_job));
            if (jobs_state != NULL && jobs != NULL) {
                ax_job_counter done = AX_JOB_COUNTER_INIT;
                uint32_t n = 0;
                for (k = i; k < end; ++k) {
                    if (roots[k].row == AX_NODE_NONE) {
                        continue;
                    }
                    ctx_init(&jobs_state[n].ctx, scene);
                    jobs_state[n].ctx.force = main_ctx.force;
                    jobs_state[n].desc = desc;
                    jobs_state[n].row = roots[k].row;
                    jobs_state[n].tree = roots[k].tree;
                    jobs[n].fn = root_job_fn;
                    jobs[n].arg = &jobs_state[n];
                    n++;
                }
                ax_jobs_submit(desc->jobs, jobs, n, &done);
                ax_jobs_wait(desc->jobs, &done);
                for (k = 0; k < n; ++k) {
                    merge(&main_ctx, &jobs_state[k].ctx);
                }
                main_ctx.counters.parallel_roots += n;
                ax_mem_free(&s->allocator, jobs_state, sizeof(root_job) * (end - i));
                ax_mem_free(&s->allocator, jobs, sizeof(ax_job) * (end - i));
                i = end;
                continue;
            }
            ax_mem_free(&s->allocator, jobs_state, sizeof(root_job) * (end - i));
            ax_mem_free(&s->allocator, jobs, sizeof(ax_job) * (end - i));
        }
        for (k = i; k < end; ++k) {
            root_job j;
            if (roots[k].row == AX_NODE_NONE) {
                continue;
            }
            j.ctx = main_ctx;
            j.desc = desc;
            j.row = roots[k].row;
            j.tree = roots[k].tree;
            run_root(&j);
            main_ctx = j.ctx;
        }
        i = end;
    }
    ax_mem_free(&s->allocator, roots, sizeof(pending_root) * root_cap);

    changed = (uint32_t)main_ctx.counters.frames_changed;
    if (desc != NULL && desc->frame_changed != NULL) {
        uint32_t slot;
        for (slot = main_ctx.changed_head; slot != AX_NODE_NONE;
             slot = s->layout[axl_row(s, slot)].changed_next) {
            desc->frame_changed(desc->frame_changed_user, slot);
        }
    }
    {
        ax_layout_counters *c = &scene->layout_counters;
        c->passes += 1;
        c->roots += main_ctx.counters.roots;
        c->parallel_roots += main_ctx.counters.parallel_roots;
        c->layouts += main_ctx.counters.layouts;
        c->measures += main_ctx.counters.measures;
        c->cached += main_ctx.counters.cached;
        c->measure_calls += main_ctx.counters.measure_calls;
        c->frames_changed += main_ctx.counters.frames_changed;
    }
    ctx_free(&main_ctx);
    return changed;
}

void ax_layout_tree(ax_scene *scene, ax_node root, float owner_w, float owner_h, float scale)
{
    ax_layout_ctx ctx;
    uint32_t row = ax_node_store_row(&scene->store, root);
    if (row == AX_NODE_NONE || scene->store.links[row].parent != AX_NODE_NONE) {
        return;
    }
    scene->layout_pass++;
    if (scene->layout_pass == 0) {
        scene->layout_pass = 1;
    }
    ctx_init(&ctx, scene);
    ctx.scale = scale;
    layout_tree(&ctx, row, owner_w, owner_h, true);
    scene->layout_counters.passes++;
    scene->layout_counters.layouts += ctx.counters.layouts;
    scene->layout_counters.measures += ctx.counters.measures;
    scene->layout_counters.cached += ctx.counters.cached;
    scene->layout_counters.measure_calls += ctx.counters.measure_calls;
    scene->layout_counters.frames_changed += ctx.counters.frames_changed;
    ctx_free(&ctx);
}

void ax_layout_present(ax_scene *scene, uint32_t row, float width, float height,
                       ax_node_layout *boxes)
{
    ax_layout_ctx ctx;
    const ax_node_layout *target = &scene->store.layout[row];
    ax_node_style *vs = &ctx.viewport_style;
    ax_layout_request req;
    int e;
    ctx_init(&ctx, scene);
    ctx.box = boxes;
    ctx.force = true; /* every node of the subtree, into `boxes` */
    ctx.scale = 0.0f; /* intermediate frames are not rounded */
    ctx.pass = ++scene->present_pass;
    if (ctx.pass == 0) {
        ctx.pass = ++scene->present_pass;
    }
    /* The node at exactly width x height: its own size, margins and bounds set aside. */
    *vs = scene->store.style[row];
    vs->width.value = width;
    vs->width.unit = AX_UNIT_POINTS;
    vs->height.value = height;
    vs->height.unit = AX_UNIT_POINTS;
    vs->min_width.unit = vs->min_height.unit = AX_UNIT_AUTO;
    vs->max_width.unit = vs->max_height.unit = AX_UNIT_AUTO;
    vs->box_sizing = AX_BOX_SIZING_BORDER_BOX;
    for (e = 0; e < 4; ++e) {
        vs->margin[e].value = 0.0f;
        vs->margin[e].unit = AX_UNIT_POINTS;
    }
    ctx.viewport_row = row;
    memset(&req, 0, sizeof req);
    req.avail_w = width;
    req.avail_h = height;
    req.mode_w = AX_LAYOUT_EXACT;
    req.mode_h = AX_LAYOUT_EXACT;
    req.owner_w = target->owner_w;
    req.owner_h = target->owner_h;
    req.perform = true;
    (void)ax_layout_node(&ctx, row, &req);
    scene->layout_counters.present_layouts += ctx.counters.layouts + ctx.counters.measures;
}

void ax_layout_get_counters(ax_scene *scene, ax_layout_counters *out, bool reset)
{
    *out = scene->layout_counters;
    if (reset) {
        memset(&scene->layout_counters, 0, sizeof scene->layout_counters);
    }
}

ax_result ax_layout_set_measure(ax_scene *scene, ax_node node, ax_layout_measure_fn fn, void *user)
{
    measure_component *m;
    ax_result r;
    if (!ax_node_store_is_live(&scene->store, node)) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_layout_set_measure: stale node");
    }
    if (fn == NULL) {
        if (ax_node_store_get_component(&scene->store, node, scene->measure_component) != NULL) {
            (void)ax_node_store_remove_component(&scene->store, node, scene->measure_component);
            ax_scene_mark_layout(scene, node);
        }
        return AX_OK;
    }
    r = ax_node_store_add_component(&scene->store, node, scene->measure_component, (void **)&m);
    if (r != AX_OK) {
        return ax_set_error(r, "ax_layout_set_measure: out of memory");
    }
    m->fn = fn;
    m->user = user;
    ax_scene_mark_layout(scene, node);
    return AX_OK;
}
