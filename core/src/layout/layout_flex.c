/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The flex protocol (decision 90): Yoga-compatible flexbox. The steps and their quirks follow
 * Yoga's algorithm (MIT, Meta Platforms; see NOTICE), so the conformance suite ported from Yoga's
 * tests (decision 91) measures this code against the same expectations. Left to right only: RTL
 * arrives with bidi (M3). Baseline alignment is laid out as flex-start until text gives nodes a
 * baseline.
 *
 * Children are iterated through the store's sibling links; a flex line is a run of siblings
 * [start, end), so the protocol needs no memory of its own.
 */
#include <float.h>
#include <string.h>

#include "layout_internal.h"

typedef struct flex_line {
    uint32_t start, end; /* slots: the line's first child and the one after its last */
    uint32_t items;
    float size_consumed;
    uint32_t auto_margins;
    float total_grow;
    float total_shrink_scaled;
    float remaining;
    float main_dim;
    float cross_dim;
} flex_line;

#define NEXT(s, slot) ((s)->links[axl_row((s), (slot))].next_sibling)

static bool in_flow(const ax_node_style *st)
{
    return st->display != AX_DISPLAY_NONE && st->position != AX_POSITION_ABSOLUTE;
}

static float flex_grow(ax_layout_ctx *c, uint32_t row)
{
    return c->s->links[row].parent == AX_NODE_NONE ? 0.0f : axl_style(c, row)->flex_grow;
}

static float flex_shrink(ax_layout_ctx *c, uint32_t row)
{
    return c->s->links[row].parent == AX_NODE_NONE ? 0.0f : axl_style(c, row)->flex_shrink;
}

static bool flexible(ax_layout_ctx *c, uint32_t row)
{
    return axl_style(c, row)->position != AX_POSITION_ABSOLUTE &&
           (flex_grow(c, row) != 0.0f || flex_shrink(c, row) != 0.0f);
}

static uint32_t child_align(const ax_node_style *parent, const ax_node_style *child)
{
    uint32_t a = child->align_self == AX_ALIGN_AUTO ? parent->align_items : child->align_self;
    if (a == AX_ALIGN_BASELINE) {
        a = AX_ALIGN_FLEX_START; /* no baselines until text */
    }
    return a;
}

static uint32_t fallback_align(uint32_t a)
{
    switch (a) {
    case AX_ALIGN_SPACE_BETWEEN:
    case AX_ALIGN_STRETCH:
    case AX_ALIGN_SPACE_AROUND:
    case AX_ALIGN_SPACE_EVENLY:
        return AX_ALIGN_FLEX_START;
    default:
        return a;
    }
}

static uint32_t fallback_justify(uint32_t j)
{
    switch (j) {
    case AX_JUSTIFY_SPACE_BETWEEN:
    case AX_JUSTIFY_SPACE_AROUND:
    case AX_JUSTIFY_SPACE_EVENLY:
        return AX_JUSTIFY_FLEX_START;
    default:
        return j;
    }
}

static float dim_with_margin(ax_layout_ctx *c, uint32_t row, uint32_t axis, float width_size)
{
    return axl_measured(axl_box(c, row), axl_dim(axis)) +
           axl_margin_axis(axl_style(c, row), axis, width_size);
}

static void request(ax_layout_ctx *c, uint32_t row, float w, float h, ax_layout_mode wm,
                    ax_layout_mode hm, float ow, float oh, bool perform)
{
    ax_layout_request r;
    r.avail_w = w;
    r.avail_h = h;
    r.mode_w = wm;
    r.mode_h = hm;
    r.owner_w = ow;
    r.owner_h = oh;
    r.perform = perform;
    (void)ax_layout_node(c, row, &r);
}

static void constrain_max(const ax_node_style *st, uint32_t axis, float owner_axis, float owner_w,
                          ax_layout_mode *mode, float *size)
{
    float mx = axl_resolved_max(st, axl_dim(axis), owner_axis, owner_w) +
               axl_margin_axis(st, axis, owner_w);
    switch (*mode) {
    case AX_LAYOUT_EXACT:
    case AX_LAYOUT_AT_MOST:
        *size = (axl_undef(mx) || *size < mx) ? *size : mx;
        break;
    case AX_LAYOUT_UNDEFINED:
        if (axl_def(mx)) {
            *mode = AX_LAYOUT_AT_MOST;
            *size = mx;
        }
        break;
    }
}

/* A child's offsets from its margins and relative insets, before justification. */
static void set_position(ax_layout_ctx *c, uint32_t row, float ow, float oh)
{
    const ax_node_style *st = axl_style(c, row);
    ax_node_layout *b = axl_box(c, row);
    uint32_t main = st->flex_direction;
    uint32_t cross = axl_is_row(main) ? AX_FLEX_DIRECTION_COLUMN : AX_FLEX_DIRECTION_ROW;
    float rel_main = 0.0f, rel_cross = 0.0f;
    if (st->position != AX_POSITION_STATIC) {
        float ms = axl_is_row(main) ? ow : oh;
        float cs = axl_is_row(main) ? oh : ow;
        rel_main = axl_inset_defined(st, axl_inline_start(main))
                       ? axl_inset(st, axl_inline_start(main), ms)
                       : -axl_inset(st, axl_inline_end(main), ms);
        rel_cross = axl_inset_defined(st, axl_inline_start(cross))
                        ? axl_inset(st, axl_inline_start(cross), cs)
                        : -axl_inset(st, axl_inline_end(cross), cs);
    }
    b->pos[axl_inline_start(main)] = axl_margin(st, axl_inline_start(main), ow) + rel_main;
    b->pos[axl_inline_end(main)] = axl_margin(st, axl_inline_end(main), ow) + rel_main;
    b->pos[axl_inline_start(cross)] = axl_margin(st, axl_inline_start(cross), ow) + rel_cross;
    b->pos[axl_inline_end(cross)] = axl_margin(st, axl_inline_end(cross), ow) + rel_cross;
}

static float available_inner(const ax_node_style *st, int dim, float avail, float pb,
                             float owner_dim, float owner_w)
{
    float inner = avail - pb;
    if (axl_def(inner)) {
        float mn = axl_resolved_min(st, dim, owner_dim, owner_w);
        float mx = axl_resolved_max(st, dim, owner_dim, owner_w);
        float min_inner = axl_undef(mn) ? 0.0f : mn - pb;
        float max_inner = axl_undef(mx) ? FLT_MAX : mx - pb;
        inner = axl_max_or_def(axl_min_or_def(inner, max_inner), min_inner);
    }
    return inner;
}

/* ---- Flex basis
 * ------------------------------------------------------------------------------------ */

static void basis_for_child(ax_layout_ctx *c, uint32_t node, uint32_t child, float width,
                            ax_layout_mode width_mode, float height, float owner_w, float owner_h,
                            ax_layout_mode height_mode)
{
    const ax_node_style *ps = axl_style(c, node);
    const ax_node_style *cs = axl_style(c, child);
    ax_node_layout *cb = axl_box(c, child);
    uint32_t main = ps->flex_direction;
    bool row_main = axl_is_row(main);
    float main_size = row_main ? width : height;
    float main_owner = row_main ? owner_w : owner_h;
    float basis = ax_layout_resolve(cs->flex_basis, main_owner);
    bool row_def = axl_definite(cs, 0, owner_w);
    bool col_def = axl_definite(cs, 1, owner_h);
    bool dirty = c->force || (c->s->dirty[child] & (AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_CHILD)) != 0;

    if (cs->box_sizing == AX_BOX_SIZING_CONTENT_BOX) {
        basis = basis + axl_pad_border_dim(cs, axl_dim(main), owner_w);
    }
    if (dirty && cb->basis_pass != c->pass) {
        cb->flex_basis = AXL_UNDEF; /* Yoga clears it when a node is marked dirty */
    }
    if (axl_def(basis) && axl_def(main_size)) {
        if (axl_undef(cb->flex_basis)) {
            cb->flex_basis = axl_max_or_def(basis, axl_pad_border_axis(cs, main, owner_w));
        }
    } else if (row_main && row_def) {
        cb->flex_basis = axl_max_or_def(axl_resolved_size(cs, 0, owner_w, owner_w),
                                        axl_pad_border_axis(cs, AX_FLEX_DIRECTION_ROW, owner_w));
    } else if (!row_main && col_def) {
        cb->flex_basis = axl_max_or_def(axl_resolved_size(cs, 1, owner_h, owner_w),
                                        axl_pad_border_axis(cs, AX_FLEX_DIRECTION_COLUMN, owner_w));
    } else {
        float cw = AXL_UNDEF, ch = AXL_UNDEF;
        ax_layout_mode cwm = AX_LAYOUT_UNDEFINED, chm = AX_LAYOUT_UNDEFINED;
        float margin_row = axl_margin_axis(cs, AX_FLEX_DIRECTION_ROW, owner_w);
        float margin_col = axl_margin_axis(cs, AX_FLEX_DIRECTION_COLUMN, owner_w);
        float ar = cs->aspect_ratio;
        uint32_t align = child_align(ps, cs);
        if (row_def) {
            cw = axl_resolved_size(cs, 0, owner_w, owner_w) + margin_row;
            cwm = AX_LAYOUT_EXACT;
        }
        if (col_def) {
            ch = axl_resolved_size(cs, 1, owner_h, owner_w) + margin_col;
            chm = AX_LAYOUT_EXACT;
        }
        /* Overflow scroll would change these two; it arrives with scroll views. */
        if (axl_undef(cw) && axl_def(width)) {
            cw = width;
            cwm = AX_LAYOUT_AT_MOST;
        }
        if (axl_undef(ch) && axl_def(height)) {
            ch = height;
            chm = AX_LAYOUT_AT_MOST;
        }
        if (ar > 0.0f) {
            if (!row_main && cwm == AX_LAYOUT_EXACT) {
                ch = margin_col + (cw - margin_row) / ar;
                chm = AX_LAYOUT_EXACT;
            } else if (row_main && chm == AX_LAYOUT_EXACT) {
                cw = margin_row + (ch - margin_col) * ar;
                cwm = AX_LAYOUT_EXACT;
            }
        }
        if (!row_main && !row_def && axl_def(width) && width_mode == AX_LAYOUT_EXACT &&
            align == AX_ALIGN_STRETCH && cwm != AX_LAYOUT_EXACT) {
            cw = width;
            cwm = AX_LAYOUT_EXACT;
            if (ar > 0.0f) {
                ch = (cw - margin_row) / ar;
                chm = AX_LAYOUT_EXACT;
            }
        }
        if (row_main && !col_def && axl_def(height) && height_mode == AX_LAYOUT_EXACT &&
            align == AX_ALIGN_STRETCH && chm != AX_LAYOUT_EXACT) {
            ch = height;
            chm = AX_LAYOUT_EXACT;
            if (ar > 0.0f) {
                cw = (ch - margin_col) * ar;
                cwm = AX_LAYOUT_EXACT;
            }
        }
        constrain_max(cs, AX_FLEX_DIRECTION_ROW, owner_w, owner_w, &cwm, &cw);
        constrain_max(cs, AX_FLEX_DIRECTION_COLUMN, owner_h, owner_w, &chm, &ch);
        request(c, child, cw, ch, cwm, chm, owner_w, owner_h, false);
        cb->flex_basis =
            axl_max_or_def(axl_measured(cb, axl_dim(main)), axl_pad_border_axis(cs, main, owner_w));
    }
    cb->basis_pass = c->pass;
}

static float basis_for_children(ax_layout_ctx *c, uint32_t node, float inner_w, float inner_h,
                                ax_layout_mode wm, ax_layout_mode hm, bool perform)
{
    ax_node_store *s = c->s;
    const ax_node_style *ps = axl_style(c, node);
    uint32_t main = ps->flex_direction;
    float total = 0.0f;
    uint32_t single = AX_NODE_NONE;
    uint32_t slot;
    ax_layout_mode main_mode = axl_is_row(main) ? wm : hm;

    if (main_mode == AX_LAYOUT_EXACT) {
        for (slot = s->links[node].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
            uint32_t row = axl_row(s, slot);
            if (flexible(c, row)) {
                if (single != AX_NODE_NONE || axl_inexact_eq(flex_grow(c, row), 0.0f) ||
                    axl_inexact_eq(flex_shrink(c, row), 0.0f)) {
                    single = AX_NODE_NONE;
                    break;
                }
                single = row;
            }
        }
    }
    for (slot = s->links[node].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
        uint32_t row = axl_row(s, slot);
        const ax_node_style *cs = axl_style(c, row);
        if (cs->display == AX_DISPLAY_NONE) {
            ax_layout_zero_subtree(c, row);
            continue;
        }
        if (perform) {
            set_position(c, row, inner_w, inner_h);
        }
        if (cs->position == AX_POSITION_ABSOLUTE) {
            continue;
        }
        if (row == single) {
            axl_box(c, row)->basis_pass = c->pass;
            axl_box(c, row)->flex_basis = 0.0f;
        } else {
            basis_for_child(c, node, row, inner_w, wm, inner_h, inner_w, inner_h, hm);
        }
        total += axl_box(c, row)->flex_basis + axl_margin_axis(cs, main, inner_w);
    }
    return total;
}

/* ---- Lines
 * ------------------------------------------------------------------------------------------ */

static void collect_line(ax_layout_ctx *c, uint32_t node, uint32_t start, uint32_t line_index,
                         float owner_w, float main_owner, float inner_w, float inner_main,
                         flex_line *out)
{
    ax_node_store *s = c->s;
    const ax_node_style *ps = axl_style(c, node);
    uint32_t main = ps->flex_direction;
    bool wrap = ps->flex_wrap != AX_FLEX_WRAP_NO_WRAP;
    float gap = axl_gap(ps, main, inner_main);
    float consumed_min = 0.0f;
    bool first = true;
    uint32_t slot;

    memset(out, 0, sizeof *out);
    out->start = start;
    for (slot = start; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
        uint32_t row = axl_row(s, slot);
        const ax_node_style *cs = axl_style(c, row);
        ax_node_layout *cb = axl_box(c, row);
        float margin_main, lead_gap, basis;
        if (!in_flow(cs)) {
            continue;
        }
        if (axl_margin_auto(cs, axl_lead(main))) {
            out->auto_margins++;
        }
        if (axl_margin_auto(cs, axl_trail(main))) {
            out->auto_margins++;
        }
        cb->line = line_index;
        margin_main = axl_margin_axis(cs, main, inner_w);
        lead_gap = first ? 0.0f : gap;
        basis = axl_bound_min_max(cs, main, cb->flex_basis, main_owner, owner_w);
        if (consumed_min + basis + margin_main + lead_gap > inner_main && wrap && out->items > 0) {
            break;
        }
        first = false;
        consumed_min += basis + margin_main + lead_gap;
        out->size_consumed += basis + margin_main + lead_gap;
        if (flexible(c, row)) {
            out->total_grow += flex_grow(c, row);
            out->total_shrink_scaled += -flex_shrink(c, row) * cb->flex_basis;
        }
        out->items++;
    }
    out->end = slot;
    if (out->total_grow > 0.0f && out->total_grow < 1.0f) {
        out->total_grow = 1.0f;
    }
    if (out->total_shrink_scaled > 0.0f && out->total_shrink_scaled < 1.0f) {
        out->total_shrink_scaled = 1.0f;
    }
}

/* Calls fn for each in-flow item of the line. */
#define FOR_LINE_ITEMS(s, line, slot_, row_)                                             \
    for (uint32_t slot_ = (line)->start; slot_ != (line)->end; slot_ = NEXT((s), slot_)) \
        if (!in_flow(axl_style(c, (row_) = axl_row((s), slot_)))) {                      \
        } else

/* ---- Resolving flexible lengths
 * -------------------------------------------------------------------- */

typedef struct flex_frame {
    uint32_t node;
    uint32_t main, cross;
    bool row_main;
    float owner_w;
    float main_owner;
    float inner_main, inner_cross, inner_w, inner_h;
    bool main_overflows;
    ax_layout_mode cross_mode;
    bool perform;
} flex_frame;

static void distribute_first(ax_layout_ctx *c, const flex_frame *f, flex_line *line)
{
    ax_node_store *s = c->s;
    float delta = 0.0f;
    uint32_t row = 0;
    FOR_LINE_ITEMS(s, line, slot, row)
    {
        const ax_node_style *cs = axl_style(c, row);
        ax_node_layout *cb = axl_box(c, row);
        float basis = axl_bound_min_max(cs, f->main, cb->flex_basis, f->main_owner, f->owner_w);
        if (line->remaining < 0.0f) {
            float scaled = -flex_shrink(c, row) * basis;
            if (axl_def(scaled) && scaled != 0.0f) {
                float base = basis + line->remaining / line->total_shrink_scaled * scaled;
                float bound = axl_bound(cs, f->main, base, f->inner_main, f->inner_w);
                if (axl_def(base) && axl_def(bound) && base != bound) {
                    delta += bound - basis;
                    line->total_shrink_scaled -= -flex_shrink(c, row) * cb->flex_basis;
                }
            }
        } else if (axl_def(line->remaining) && line->remaining > 0.0f) {
            float grow = flex_grow(c, row);
            if (axl_def(grow) && grow != 0.0f) {
                float base = basis + line->remaining / line->total_grow * grow;
                float bound = axl_bound(cs, f->main, base, f->inner_main, f->inner_w);
                if (axl_def(base) && axl_def(bound) && base != bound) {
                    delta += bound - basis;
                    line->total_grow -= grow;
                }
            }
        }
    }
    line->remaining -= delta;
}

static float distribute_second(ax_layout_ctx *c, const flex_frame *f, flex_line *line)
{
    ax_node_store *s = c->s;
    const ax_node_style *ps = axl_style(c, f->node);
    bool wrap = ps->flex_wrap != AX_FLEX_WRAP_NO_WRAP;
    float delta = 0.0f;
    uint32_t row = 0;
    FOR_LINE_ITEMS(s, line, slot, row)
    {
        const ax_node_style *cs = axl_style(c, row);
        ax_node_layout *cb = axl_box(c, row);
        float basis = axl_bound_min_max(cs, f->main, cb->flex_basis, f->main_owner, f->owner_w);
        float updated = basis;
        float margin_main, margin_cross, child_main, child_cross = AXL_UNDEF;
        ax_layout_mode cross_mode, main_mode = AX_LAYOUT_EXACT;
        uint32_t align = child_align(ps, cs);
        bool cross_def = axl_definite(cs, axl_dim(f->cross), f->inner_cross);
        bool cross_auto =
            axl_margin_auto(cs, axl_lead(f->cross)) || axl_margin_auto(cs, axl_trail(f->cross));
        bool stretch_layout;
        if (axl_def(line->remaining) && line->remaining < 0.0f) {
            float scaled = -flex_shrink(c, row) * basis;
            if (scaled != 0.0f) {
                float size;
                if (axl_def(line->total_shrink_scaled) && line->total_shrink_scaled == 0.0f) {
                    size = basis + scaled;
                } else {
                    size = basis + (line->remaining / line->total_shrink_scaled) * scaled;
                }
                updated = axl_bound(cs, f->main, size, f->inner_main, f->inner_w);
            }
        } else if (axl_def(line->remaining) && line->remaining > 0.0f) {
            float grow = flex_grow(c, row);
            if (!(grow != grow) && grow != 0.0f) {
                updated = axl_bound(cs, f->main, basis + line->remaining / line->total_grow * grow,
                                    f->inner_main, f->inner_w);
            }
        }
        delta += updated - basis;
        margin_main = axl_margin_axis(cs, f->main, f->inner_w);
        margin_cross = axl_margin_axis(cs, f->cross, f->inner_w);
        child_main = updated + margin_main;
        if (cs->aspect_ratio > 0.0f) {
            child_cross = f->row_main ? (child_main - margin_main) / cs->aspect_ratio
                                      : (child_main - margin_main) * cs->aspect_ratio;
            cross_mode = AX_LAYOUT_EXACT;
            child_cross += margin_cross;
        } else if (!(f->inner_cross != f->inner_cross) && !cross_def &&
                   f->cross_mode == AX_LAYOUT_EXACT && !(wrap && f->main_overflows) &&
                   align == AX_ALIGN_STRETCH && !cross_auto) {
            child_cross = f->inner_cross;
            cross_mode = AX_LAYOUT_EXACT;
        } else if (!cross_def) {
            child_cross = f->inner_cross;
            cross_mode = axl_undef(child_cross) ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_AT_MOST;
        } else {
            ax_dimension cd = axl_size_style(cs, axl_dim(f->cross));
            bool loose_percent = cd.unit == AX_UNIT_PERCENT && f->cross_mode != AX_LAYOUT_EXACT;
            child_cross =
                axl_resolved_size(cs, axl_dim(f->cross), f->inner_cross, f->inner_w) + margin_cross;
            cross_mode =
                axl_undef(child_cross) || loose_percent ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_EXACT;
        }
        constrain_max(cs, f->main, f->inner_main, f->inner_w, &main_mode, &child_main);
        constrain_max(cs, f->cross, f->inner_cross, f->inner_w, &cross_mode, &child_cross);
        stretch_layout = !cross_def && align == AX_ALIGN_STRETCH && !cross_auto;
        request(c, row, f->row_main ? child_main : child_cross,
                f->row_main ? child_cross : child_main, f->row_main ? main_mode : cross_mode,
                f->row_main ? cross_mode : main_mode, f->inner_w, f->inner_h,
                f->perform && !stretch_layout);
    }
    return delta;
}

static void resolve_flexible(ax_layout_ctx *c, const flex_frame *f, flex_line *line)
{
    float original = line->remaining;
    float distributed;
    distribute_first(c, f, line);
    distributed = distribute_second(c, f, line);
    line->remaining = original - distributed;
}

static void justify(ax_layout_ctx *c, const flex_frame *f, flex_line *line,
                    ax_layout_mode main_mode)
{
    ax_node_store *s = c->s;
    const ax_node_style *ps = axl_style(c, f->node);
    float lead_pb = axl_pad_border(ps, axl_lead(f->main), f->owner_w);
    float trail_pb = axl_pad_border(ps, axl_trail(f->main), f->owner_w);
    float gap = axl_gap(ps, f->main, f->inner_main);
    float leading = 0.0f;
    float between = gap;
    uint32_t j;
    uint32_t row = 0;
    uint32_t last = AX_NODE_NONE;
    if (main_mode == AX_LAYOUT_AT_MOST && line->remaining > 0.0f) {
        ax_dimension mind = axl_dim(f->main) == 0 ? ps->min_width : ps->min_height;
        float mn = axl_resolved_min(ps, axl_dim(f->main), f->main_owner, f->owner_w);
        if (mind.unit != AX_UNIT_AUTO && axl_def(mn)) {
            float min_avail = mn - lead_pb - trail_pb;
            float occupied = f->inner_main - line->remaining;
            line->remaining = axl_max_or_def(0.0f, min_avail - occupied);
        } else {
            line->remaining = 0.0f;
        }
    }
    j = line->remaining >= 0.0f ? ps->justify_content : fallback_justify(ps->justify_content);
    if (line->auto_margins == 0) {
        float n = (float)line->items;
        switch (j) {
        case AX_JUSTIFY_CENTER:
            leading = line->remaining / 2.0f;
            break;
        case AX_JUSTIFY_FLEX_END:
            leading = line->remaining;
            break;
        case AX_JUSTIFY_SPACE_BETWEEN:
            if (line->items > 1) {
                between += line->remaining / (n - 1.0f);
            }
            break;
        case AX_JUSTIFY_SPACE_EVENLY:
            leading = line->remaining / (n + 1.0f);
            between += leading;
            break;
        case AX_JUSTIFY_SPACE_AROUND:
            leading = 0.5f * line->remaining / n;
            between += leading * 2.0f;
            break;
        default:
            break;
        }
    }
    FOR_LINE_ITEMS(s, line, slot, row)
    {
        last = slot;
    }
    line->main_dim = lead_pb + leading;
    line->cross_dim = 0.0f;
    FOR_LINE_ITEMS(s, line, slot, row)
    {
        const ax_node_style *cs = axl_style(c, row);
        ax_node_layout *cb = axl_box(c, row);
        if (axl_margin_auto(cs, axl_lead(f->main)) && line->remaining > 0.0f) {
            line->main_dim += line->remaining / (float)line->auto_margins;
        }
        if (f->perform) {
            cb->pos[axl_lead(f->main)] = cb->pos[axl_lead(f->main)] + line->main_dim;
        }
        if (slot != last) {
            line->main_dim += between;
        }
        if (axl_margin_auto(cs, axl_trail(f->main)) && line->remaining > 0.0f) {
            line->main_dim += line->remaining / (float)line->auto_margins;
        }
        if (!f->perform && f->cross_mode == AX_LAYOUT_EXACT) {
            line->main_dim += axl_margin_axis(cs, f->main, f->inner_w) + cb->flex_basis;
            line->cross_dim = f->inner_cross;
        } else {
            line->main_dim += dim_with_margin(c, row, f->main, f->inner_w);
            line->cross_dim =
                axl_max_or_def(line->cross_dim, dim_with_margin(c, row, f->cross, f->inner_w));
        }
    }
    line->main_dim += trail_pb;
}

/* ---- Absolute children
 * ------------------------------------------------------------------------------ */

static float opposite_edge(ax_layout_ctx *c, float position, uint32_t axis, uint32_t containing,
                           uint32_t row)
{
    return axl_measured(axl_box(c, containing), axl_dim(axis)) -
           axl_measured(axl_box(c, row), axl_dim(axis)) - position;
}

static void abs_start(ax_layout_ctx *c, uint32_t parent, uint32_t child, uint32_t axis, float cbw)
{
    const ax_node_layout *pb = axl_box(c, parent);
    float p = axl_margin(axl_style(c, child), axl_lead(axis), cbw) + pb->border[axl_lead(axis)] +
              pb->padding[axl_lead(axis)];
    axl_box(c, child)->pos[axl_lead(axis)] = p;
}

static void abs_end(ax_layout_ctx *c, uint32_t parent, uint32_t child, uint32_t axis, float cbw)
{
    const ax_node_layout *pb = axl_box(c, parent);
    float p = pb->border[axl_trail(axis)] + axl_margin(axl_style(c, child), axl_trail(axis), cbw) +
              pb->padding[axl_trail(axis)];
    axl_box(c, child)->pos[axl_lead(axis)] = opposite_edge(c, p, axis, parent, child);
}

static void abs_center(ax_layout_ctx *c, uint32_t parent, uint32_t child, uint32_t axis, float cbw)
{
    const ax_node_layout *pb = axl_box(c, parent);
    const ax_node_style *cs = axl_style(c, child);
    int d = axl_dim(axis);
    float content = axl_measured(pb, d) - pb->border[axl_lead(axis)] - pb->border[axl_trail(axis)] -
                    pb->padding[axl_lead(axis)] - pb->padding[axl_trail(axis)];
    float outer = axl_measured(axl_box(c, child), d) + axl_margin_axis(cs, axis, cbw);
    float p = (content - outer) / 2.0f + pb->border[axl_lead(axis)] +
              axl_margin(cs, axl_lead(axis), cbw) + pb->padding[axl_lead(axis)];
    axl_box(c, child)->pos[axl_lead(axis)] = p;
}

static void position_abs_child(ax_layout_ctx *c, uint32_t containing, uint32_t parent,
                               uint32_t child, uint32_t axis, bool is_main, float cbw, float cbh)
{
    const ax_node_style *cs = axl_style(c, child);
    const ax_node_style *ks = axl_style(c, containing);
    float cb_size = axl_is_row(axis) ? cbw : cbh;
    int start = axl_inline_start(axis);
    int end = axl_inline_end(axis);
    ax_node_layout *chb = axl_box(c, child);
    if (axl_inset_defined(cs, start)) {
        float p =
            axl_inset(cs, start, cb_size) + axl_border(ks, start) + axl_margin(cs, start, cb_size);
        chb->pos[axl_lead(axis)] =
            start != axl_lead(axis) ? opposite_edge(c, p, axis, containing, child) : p;
    } else if (axl_inset_defined(cs, end)) {
        float p = axl_measured(axl_box(c, containing), axl_dim(axis)) -
                  axl_measured(chb, axl_dim(axis)) - axl_border(ks, end) -
                  axl_margin(cs, end, cb_size) - axl_inset(cs, end, cb_size);
        chb->pos[axl_lead(axis)] =
            start != axl_lead(axis) ? opposite_edge(c, p, axis, containing, child) : p;
    } else if (is_main) {
        switch (axl_style(c, parent)->justify_content) {
        case AX_JUSTIFY_FLEX_END:
            abs_end(c, parent, child, axis, cbw);
            break;
        case AX_JUSTIFY_CENTER:
        case AX_JUSTIFY_SPACE_AROUND:
        case AX_JUSTIFY_SPACE_EVENLY:
            abs_center(c, parent, child, axis, cbw);
            break;
        default:
            abs_start(c, parent, child, axis, cbw);
            break;
        }
    } else {
        const ax_node_style *ps = axl_style(c, parent);
        uint32_t a = child_align(ps, cs);
        if (ps->flex_wrap == AX_FLEX_WRAP_WRAP_REVERSE) {
            if (a == AX_ALIGN_FLEX_END) {
                a = AX_ALIGN_FLEX_START;
            } else if (a != AX_ALIGN_CENTER) {
                a = AX_ALIGN_FLEX_END;
            }
        }
        if (a == AX_ALIGN_FLEX_END) {
            abs_end(c, parent, child, axis, cbw);
        } else if (a == AX_ALIGN_CENTER) {
            abs_center(c, parent, child, axis, cbw);
        } else {
            abs_start(c, parent, child, axis, cbw);
        }
    }
}

static void layout_abs_child(ax_layout_ctx *c, uint32_t containing, uint32_t parent, uint32_t child,
                             float cbw, float cbh, ax_layout_mode width_mode)
{
    const ax_node_style *ps = axl_style(c, parent);
    const ax_node_style *cs = axl_style(c, child);
    const ax_node_style *ks = axl_style(c, containing);
    const ax_node_layout *kb = axl_box(c, containing);
    ax_node_layout *chb = axl_box(c, child);
    uint32_t main = ps->flex_direction;
    uint32_t cross = axl_is_row(main) ? AX_FLEX_DIRECTION_COLUMN : AX_FLEX_DIRECTION_ROW;
    bool row_main = axl_is_row(main);
    float cw = AXL_UNDEF, ch = AXL_UNDEF;
    ax_layout_mode cwm = AX_LAYOUT_UNDEFINED, chm = AX_LAYOUT_UNDEFINED;
    float margin_row = axl_margin_axis(cs, AX_FLEX_DIRECTION_ROW, cbw);
    float margin_col = axl_margin_axis(cs, AX_FLEX_DIRECTION_COLUMN, cbw);

    if (axl_definite(cs, 0, cbw)) {
        cw = axl_resolved_size(cs, 0, cbw, cbw) + margin_row;
    } else if (axl_inset_defined(cs, AX_EDGE_LEFT) && axl_inset_defined(cs, AX_EDGE_RIGHT)) {
        cw = kb->measured_w - (axl_border(ks, AX_EDGE_LEFT) + axl_border(ks, AX_EDGE_RIGHT)) -
             (axl_inset(cs, AX_EDGE_LEFT, cbw) + axl_inset(cs, AX_EDGE_RIGHT, cbw));
        cw = axl_bound(cs, AX_FLEX_DIRECTION_ROW, cw, cbw, cbw);
    }
    if (axl_definite(cs, 1, cbh)) {
        ch = axl_resolved_size(cs, 1, cbh, cbw) + margin_col;
    } else if (axl_inset_defined(cs, AX_EDGE_TOP) && axl_inset_defined(cs, AX_EDGE_BOTTOM)) {
        ch = kb->measured_h - (axl_border(ks, AX_EDGE_TOP) + axl_border(ks, AX_EDGE_BOTTOM)) -
             (axl_inset(cs, AX_EDGE_TOP, cbh) + axl_inset(cs, AX_EDGE_BOTTOM, cbh));
        ch = axl_bound(cs, AX_FLEX_DIRECTION_COLUMN, ch, cbh, cbw);
    }
    if (axl_undef(cw) != axl_undef(ch) && cs->aspect_ratio > 0.0f) {
        if (axl_undef(cw)) {
            cw = margin_row + (ch - margin_col) * cs->aspect_ratio;
        } else {
            ch = margin_col + (cw - margin_row) / cs->aspect_ratio;
        }
    }
    if (axl_undef(cw) || axl_undef(ch)) {
        cwm = axl_undef(cw) ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_EXACT;
        chm = axl_undef(ch) ? AX_LAYOUT_UNDEFINED : AX_LAYOUT_EXACT;
        if (!row_main && axl_undef(cw) && width_mode != AX_LAYOUT_UNDEFINED && axl_def(cbw) &&
            cbw > 0.0f) {
            cw = cbw;
            cwm = AX_LAYOUT_AT_MOST;
        }
        request(c, child, cw, ch, cwm, chm, cbw, cbh, false);
        cw = chb->measured_w + axl_margin_axis(cs, AX_FLEX_DIRECTION_ROW, cbw);
        ch = chb->measured_h + axl_margin_axis(cs, AX_FLEX_DIRECTION_COLUMN, cbw);
    }
    request(c, child, cw, ch, AX_LAYOUT_EXACT, AX_LAYOUT_EXACT, cbw, cbh, true);
    position_abs_child(c, containing, parent, child, main, true, cbw, cbh);
    position_abs_child(c, containing, parent, child, cross, false, cbw, cbh);
}

static void trailing_position(ax_layout_ctx *c, uint32_t node, uint32_t child, uint32_t axis)
{
    ax_node_layout *cb = axl_box(c, child);
    cb->pos[axl_trail(axis)] = axl_measured(axl_box(c, node), axl_dim(axis)) -
                               axl_measured(cb, axl_dim(axis)) - cb->pos[axl_lead(axis)];
}

static bool horizontal_insets(const ax_node_style *st)
{
    return axl_inset_defined(st, AX_EDGE_LEFT) || axl_inset_defined(st, AX_EDGE_RIGHT);
}

static bool vertical_insets(const ax_node_style *st)
{
    return axl_inset_defined(st, AX_EDGE_TOP) || axl_inset_defined(st, AX_EDGE_BOTTOM);
}

static void layout_abs_descendants(ax_layout_ctx *c, uint32_t containing, uint32_t current,
                                   ax_layout_mode width_mode, float left_off, float top_off)
{
    ax_node_store *s = c->s;
    uint32_t slot;
    for (slot = s->links[current].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
        uint32_t child = axl_row(s, slot);
        const ax_node_style *cs = axl_style(c, child);
        if (cs->display == AX_DISPLAY_NONE) {
            continue;
        }
        if (cs->position == AX_POSITION_ABSOLUTE) {
            const ax_node_style *ks = axl_style(c, containing);
            const ax_node_layout *kb = axl_box(c, containing);
            const ax_node_style *cur = axl_style(c, current);
            float cbw =
                kb->measured_w - (axl_border(ks, AX_EDGE_LEFT) + axl_border(ks, AX_EDGE_RIGHT));
            float cbh =
                kb->measured_h - (axl_border(ks, AX_EDGE_TOP) + axl_border(ks, AX_EDGE_BOTTOM));
            uint32_t pmain = cur->flex_direction;
            uint32_t pcross = axl_is_row(pmain) ? AX_FLEX_DIRECTION_COLUMN : AX_FLEX_DIRECTION_ROW;
            ax_node_layout *chb;
            layout_abs_child(c, containing, current, child, cbw, cbh, width_mode);
            if (axl_is_reverse(pmain)) {
                bool defined = axl_is_row(pmain) ? horizontal_insets(cs) : vertical_insets(cs);
                trailing_position(c, defined ? containing : current, child, pmain);
            }
            if (axl_is_reverse(pcross)) {
                bool defined = axl_is_row(pcross) ? horizontal_insets(cs) : vertical_insets(cs);
                trailing_position(c, defined ? containing : current, child, pcross);
            }
            chb = axl_box(c, child);
            if (horizontal_insets(cs)) {
                chb->pos[AX_EDGE_LEFT] -= left_off;
            }
            if (vertical_insets(cs)) {
                chb->pos[AX_EDGE_TOP] -= top_off;
            }
        } else if (cs->position == AX_POSITION_STATIC) {
            const ax_node_layout *chb = axl_box(c, child);
            layout_abs_descendants(c, containing, child, width_mode,
                                   left_off + chb->pos[AX_EDGE_LEFT],
                                   top_off + chb->pos[AX_EDGE_TOP]);
        }
    }
}

/* ---- The protocol
 * ------------------------------------------------------------------------------------- */

static void flex_layout(ax_layout_ctx *c, uint32_t node, const ax_layout_request *req)
{
    ax_node_store *s = c->s;
    const ax_node_style *ps = axl_style(c, node);
    ax_node_layout *nb = axl_box(c, node);
    float aw = req->avail_w, ah = req->avail_h, ow = req->owner_w, oh = req->owner_h;
    float margin_row = nb->margin[AX_EDGE_LEFT] + nb->margin[AX_EDGE_RIGHT];
    float margin_col = nb->margin[AX_EDGE_TOP] + nb->margin[AX_EDGE_BOTTOM];
    flex_frame f;
    float pb_main, pb_cross, lead_pb_cross, pb_row, pb_col;
    ax_layout_mode main_mode, cross_mode;
    bool wrap = ps->flex_wrap != AX_FLEX_WRAP_NO_WRAP;
    float cross_owner;
    float total_main;
    uint32_t child_count = 0;
    uint32_t slot;
    uint32_t line_count = 0;
    float total_line_cross = 0.0f;
    float cross_gap;
    float max_line_main = 0.0f;
    uint32_t start;

    memset(&f, 0, sizeof f);
    f.node = node;
    f.main = ps->flex_direction;
    f.cross = axl_is_row(f.main) ? AX_FLEX_DIRECTION_COLUMN : AX_FLEX_DIRECTION_ROW;
    f.row_main = axl_is_row(f.main);
    f.owner_w = ow;
    f.main_owner = f.row_main ? ow : oh;
    f.perform = req->perform;
    cross_owner = f.row_main ? oh : ow;
    pb_main = axl_pad_border_axis(ps, f.main, ow);
    pb_cross = axl_pad_border_axis(ps, f.cross, ow);
    lead_pb_cross = axl_pad_border(ps, axl_lead(f.cross), ow);
    main_mode = f.row_main ? req->mode_w : req->mode_h;
    cross_mode = f.row_main ? req->mode_h : req->mode_w;
    f.cross_mode = cross_mode;
    pb_row = f.row_main ? pb_main : pb_cross;
    pb_col = f.row_main ? pb_cross : pb_main;
    f.inner_w = available_inner(ps, 0, aw - margin_row, pb_row, ow, ow);
    f.inner_h = available_inner(ps, 1, ah - margin_col, pb_col, oh, ow);
    f.inner_main = f.row_main ? f.inner_w : f.inner_h;
    f.inner_cross = f.row_main ? f.inner_h : f.inner_w;

    total_main =
        basis_for_children(c, node, f.inner_w, f.inner_h, req->mode_w, req->mode_h, req->perform);
    for (slot = s->links[node].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
        child_count++;
    }
    if (child_count > 1) {
        total_main += axl_gap(ps, f.main, f.inner_main) * (float)(child_count - 1u);
    }
    f.main_overflows = main_mode != AX_LAYOUT_UNDEFINED && total_main > f.inner_main;
    if (wrap && f.main_overflows && main_mode == AX_LAYOUT_AT_MOST) {
        main_mode = AX_LAYOUT_EXACT;
    }
    cross_gap = axl_gap(ps, f.cross, f.inner_cross);

    for (start = s->links[node].first_child; start != AX_NODE_NONE; line_count++) {
        flex_line line;
        bool can_skip = !req->perform && cross_mode == AX_LAYOUT_EXACT;
        bool by_content = false;
        float container_cross;
        uint32_t row = 0;
        collect_line(c, node, start, line_count, ow, f.main_owner, f.inner_w, f.inner_main, &line);
        start = line.end;
        if (main_mode != AX_LAYOUT_EXACT) {
            float min_w = axl_resolved_min(ps, 0, ow, ow) - pb_row;
            float max_w = axl_resolved_max(ps, 0, ow, ow) - pb_row;
            float min_h = axl_resolved_min(ps, 1, oh, ow) - pb_col;
            float max_h = axl_resolved_max(ps, 1, oh, ow) - pb_col;
            float min_main = f.row_main ? min_w : min_h;
            float max_main = f.row_main ? max_w : max_h;
            if (axl_def(min_main) && line.size_consumed < min_main) {
                f.inner_main = min_main;
            } else if (axl_def(max_main) && line.size_consumed > max_main) {
                f.inner_main = max_main;
            } else {
                float node_grow = flex_grow(c, node);
                if ((axl_def(line.total_grow) && line.total_grow == 0.0f) ||
                    (axl_def(node_grow) && node_grow == 0.0f)) {
                    f.inner_main = line.size_consumed;
                }
                by_content = true;
            }
        }
        if (!by_content && axl_def(f.inner_main)) {
            line.remaining = f.inner_main - line.size_consumed;
        } else if (line.size_consumed < 0.0f) {
            line.remaining = -line.size_consumed;
        }
        if (!can_skip) {
            resolve_flexible(c, &f, &line);
        }
        justify(c, &f, &line, main_mode);

        container_cross = f.inner_cross;
        if (cross_mode == AX_LAYOUT_UNDEFINED || cross_mode == AX_LAYOUT_AT_MOST) {
            container_cross =
                axl_bound(ps, f.cross, line.cross_dim + pb_cross, cross_owner, ow) - pb_cross;
        }
        if (!wrap && cross_mode == AX_LAYOUT_EXACT) {
            line.cross_dim = f.inner_cross;
        }
        if (!wrap) {
            line.cross_dim =
                axl_bound(ps, f.cross, line.cross_dim + pb_cross, cross_owner, ow) - pb_cross;
        }
        if (req->perform) {
            FOR_LINE_ITEMS(s, (&line), item, row)
            {
                const ax_node_style *cs = axl_style(c, row);
                ax_node_layout *cb = axl_box(c, row);
                float lead_cross = lead_pb_cross;
                uint32_t align = child_align(ps, cs);
                bool lead_auto = axl_margin_auto(cs, axl_lead(f.cross));
                bool trail_auto = axl_margin_auto(cs, axl_trail(f.cross));
                if (align == AX_ALIGN_STRETCH && !lead_auto && !trail_auto) {
                    if (!axl_definite(cs, axl_dim(f.cross), f.inner_cross)) {
                        float child_main = axl_measured(cb, axl_dim(f.main));
                        float child_cross = cs->aspect_ratio > 0.0f
                                                ? axl_margin_axis(cs, f.cross, f.inner_w) +
                                                      (f.row_main ? child_main / cs->aspect_ratio
                                                                  : child_main * cs->aspect_ratio)
                                                : line.cross_dim;
                        ax_layout_mode mm = AX_LAYOUT_EXACT, cm = AX_LAYOUT_EXACT;
                        bool no_grow = ps->align_content != AX_ALIGN_STRETCH && wrap;
                        float cw, ch;
                        child_main += axl_margin_axis(cs, f.main, f.inner_w);
                        constrain_max(cs, f.main, f.inner_main, f.inner_w, &mm, &child_main);
                        constrain_max(cs, f.cross, f.inner_cross, f.inner_w, &cm, &child_cross);
                        cw = f.row_main ? child_main : child_cross;
                        ch = f.row_main ? child_cross : child_main;
                        request(c, row, cw, ch,
                                axl_undef(cw) || (!f.row_main && no_grow) ? AX_LAYOUT_UNDEFINED
                                                                          : AX_LAYOUT_EXACT,
                                axl_undef(ch) || (f.row_main && no_grow) ? AX_LAYOUT_UNDEFINED
                                                                         : AX_LAYOUT_EXACT,
                                f.inner_w, f.inner_h, true);
                    }
                } else {
                    float rem = container_cross - dim_with_margin(c, row, f.cross, f.inner_w);
                    if (lead_auto && trail_auto) {
                        lead_cross += axl_max_or_def(0.0f, rem / 2.0f);
                    } else if (trail_auto) {
                        /* nothing */
                    } else if (lead_auto) {
                        lead_cross += axl_max_or_def(0.0f, rem);
                    } else if (align == AX_ALIGN_FLEX_START) {
                        /* nothing */
                    } else if (align == AX_ALIGN_CENTER) {
                        lead_cross += rem / 2.0f;
                    } else {
                        lead_cross += rem;
                    }
                }
                cb = axl_box(c, row);
                cb->pos[axl_lead(f.cross)] =
                    cb->pos[axl_lead(f.cross)] + total_line_cross + lead_cross;
            }
        }
        total_line_cross += line.cross_dim + (line_count != 0 ? cross_gap : 0.0f);
        max_line_main = axl_max_or_def(max_line_main, line.main_dim);
    }

    /* Several lines: align-content. */
    if (req->perform && wrap) {
        float lead_per_line = 0.0f;
        float current_lead = lead_pb_cross;
        float extra_per_line = 0.0f;
        float unclamped;
        float inner_cross;
        float remaining;
        uint32_t ac;
        uint32_t i;
        uint32_t line_start = s->links[node].first_child;
        if (cross_mode == AX_LAYOUT_EXACT) {
            unclamped = f.inner_cross + pb_cross;
        } else if (axl_definite(ps, axl_dim(f.cross), cross_owner)) {
            unclamped = axl_resolved_size(ps, axl_dim(f.cross), cross_owner, ow);
        } else {
            unclamped = total_line_cross + pb_cross;
        }
        inner_cross = axl_bound(ps, f.cross, unclamped, oh, ow) - pb_cross;
        remaining = inner_cross - total_line_cross;
        ac = remaining >= 0.0f ? ps->align_content : fallback_align(ps->align_content);
        switch (ac) {
        case AX_ALIGN_FLEX_END:
            current_lead += remaining;
            break;
        case AX_ALIGN_CENTER:
            current_lead += remaining / 2.0f;
            break;
        case AX_ALIGN_STRETCH:
            extra_per_line = remaining / (float)line_count;
            break;
        case AX_ALIGN_SPACE_AROUND:
            current_lead += remaining / (2.0f * (float)line_count);
            lead_per_line = remaining / (float)line_count;
            break;
        case AX_ALIGN_SPACE_EVENLY:
            current_lead += remaining / (float)(line_count + 1u);
            lead_per_line = remaining / (float)(line_count + 1u);
            break;
        case AX_ALIGN_SPACE_BETWEEN:
            if (line_count > 1) {
                lead_per_line = remaining / (float)(line_count - 1u);
            }
            break;
        default:
            break;
        }
        for (i = 0; i < line_count; ++i) {
            uint32_t line_end = line_start;
            float line_height = 0.0f;
            for (; line_end != AX_NODE_NONE; line_end = NEXT(s, line_end)) {
                uint32_t row = axl_row(s, line_end);
                const ax_node_style *cs = axl_style(c, row);
                const ax_node_layout *cb = axl_box(c, row);
                float m;
                if (cs->display == AX_DISPLAY_NONE) {
                    continue;
                }
                if (cs->position == AX_POSITION_ABSOLUTE) {
                    continue;
                }
                if (cb->line != i) {
                    break;
                }
                m = axl_measured(cb, axl_dim(f.cross));
                if (axl_def(m) && m >= 0.0f) {
                    line_height =
                        axl_max_or_def(line_height, m + axl_margin_axis(cs, f.cross, f.inner_w));
                }
            }
            current_lead += i != 0 ? cross_gap : 0.0f;
            line_height += extra_per_line;
            for (slot = line_start; slot != line_end; slot = NEXT(s, slot)) {
                uint32_t row = axl_row(s, slot);
                const ax_node_style *cs = axl_style(c, row);
                ax_node_layout *cb = axl_box(c, row);
                if (!in_flow(cs)) {
                    continue;
                }
                switch (child_align(ps, cs)) {
                case AX_ALIGN_FLEX_START:
                    cb->pos[axl_lead(f.cross)] =
                        current_lead + axl_inset(cs, axl_lead(f.cross), f.inner_w);
                    break;
                case AX_ALIGN_FLEX_END:
                    cb->pos[axl_lead(f.cross)] = current_lead + line_height -
                                                 axl_margin(cs, axl_trail(f.cross), f.inner_w) -
                                                 axl_measured(cb, axl_dim(f.cross));
                    break;
                case AX_ALIGN_CENTER:
                    cb->pos[axl_lead(f.cross)] =
                        current_lead + (line_height - axl_measured(cb, axl_dim(f.cross))) / 2.0f;
                    break;
                case AX_ALIGN_STRETCH:
                    cb->pos[axl_lead(f.cross)] =
                        current_lead + axl_margin(cs, axl_lead(f.cross), f.inner_w);
                    if (!axl_definite(cs, axl_dim(f.cross), f.inner_cross)) {
                        float cw = f.row_main
                                       ? cb->measured_w + axl_margin_axis(cs, f.main, f.inner_w)
                                       : lead_per_line + line_height;
                        float ch = !f.row_main
                                       ? cb->measured_h + axl_margin_axis(cs, f.cross, f.inner_w)
                                       : lead_per_line + line_height;
                        if (!(axl_inexact_eq(cw, cb->measured_w) &&
                              axl_inexact_eq(ch, cb->measured_h))) {
                            request(c, row, cw, ch, AX_LAYOUT_EXACT, AX_LAYOUT_EXACT, f.inner_w,
                                    f.inner_h, true);
                        }
                    }
                    break;
                default:
                    break;
                }
            }
            current_lead = current_lead + lead_per_line + line_height;
            line_start = line_end;
        }
    }

    /* The node's size. */
    nb->measured_w = axl_bound(ps, AX_FLEX_DIRECTION_ROW, aw - margin_row, ow, ow);
    nb->measured_h = axl_bound(ps, AX_FLEX_DIRECTION_COLUMN, ah - margin_col, oh, ow);
    if (main_mode == AX_LAYOUT_UNDEFINED || main_mode == AX_LAYOUT_AT_MOST) {
        axl_set_measured(nb, axl_dim(f.main),
                         axl_bound(ps, f.main, max_line_main, f.main_owner, ow));
    }
    if (cross_mode == AX_LAYOUT_UNDEFINED || cross_mode == AX_LAYOUT_AT_MOST) {
        axl_set_measured(nb, axl_dim(f.cross),
                         axl_bound(ps, f.cross, total_line_cross + pb_cross, cross_owner, ow));
    }

    if (req->perform && ps->flex_wrap == AX_FLEX_WRAP_WRAP_REVERSE) {
        for (slot = s->links[node].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
            uint32_t row = axl_row(s, slot);
            ax_node_layout *cb = axl_box(c, row);
            if (axl_style(c, row)->position != AX_POSITION_ABSOLUTE) {
                cb->pos[axl_lead(f.cross)] = axl_measured(nb, axl_dim(f.cross)) -
                                             cb->pos[axl_lead(f.cross)] -
                                             axl_measured(cb, axl_dim(f.cross));
            }
        }
    }
    if (req->perform) {
        bool main_trail = axl_is_reverse(f.main);
        bool cross_trail = axl_is_reverse(f.cross);
        if (main_trail || cross_trail) {
            for (slot = s->links[node].first_child; slot != AX_NODE_NONE; slot = NEXT(s, slot)) {
                uint32_t row = axl_row(s, slot);
                if (!in_flow(axl_style(c, row))) {
                    continue;
                }
                if (main_trail) {
                    trailing_position(c, node, row, f.main);
                }
                if (cross_trail) {
                    trailing_position(c, node, row, f.cross);
                }
            }
        }
        if (ps->position != AX_POSITION_STATIC || ax_layout_is_tree_root(c, node)) {
            layout_abs_descendants(c, node, node, f.row_main ? main_mode : cross_mode, 0.0f, 0.0f);
        }
    }
}

const ax_layout_protocol ax__layout_flex = {
    "flex",
    flex_layout,
};
