/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The layout engine's internals (decision 90): the pass context, and the style helpers the core and
 * the flex protocol share. Undefined lengths are NaN.
 *
 * The flex protocol follows Yoga's algorithm (MIT, Meta Platforms) closely, quirks included, so the
 * conformance suite ported from Yoga's tests measures the same thing (decision 91).
 */
#ifndef AX_CORE_LAYOUT_INTERNAL_H
#define AX_CORE_LAYOUT_INTERNAL_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <axion/core/ax_layout.h>
#include <axion/core/ax_scene.h>

/* ---- The pass context
 * ---------------------------------------------------------------------------- */

struct ax_layout_ctx {
    ax_scene *scene;
    ax_node_store *s;
    ax_node_layout *box; /* the array written: the store's layout column, for now */
    uint32_t pass;
    uint32_t tree_root_row;       /* the parentless node of the tree being laid out */
    uint32_t viewport_row;        /* a window root, or AX_NODE_NONE */
    ax_node_style viewport_style; /* its style, with the viewport's size, without margins */
    float scale;                  /* pixel scale for rounding; 0: none */
    bool force;                   /* every node counts as dirty (the root list overflowed) */
    ax_layout_counters counters;
    /* Changed frames, reported after the pass. */
    uint32_t *changed;
    uint32_t changed_count, changed_capacity;
    bool changed_overflow;
};

static inline uint32_t axl_row(const ax_node_store *s, uint32_t slot)
{
    return s->slots[slot].row;
}

static inline const ax_node_style *axl_style(const ax_layout_ctx *c, uint32_t row)
{
    return row == c->viewport_row ? &c->viewport_style : &c->s->style[row];
}

static inline ax_node_layout *axl_box(const ax_layout_ctx *c, uint32_t row)
{
    return &c->box[row];
}

/* ---- Numbers
 * ------------------------------------------------------------------------------------- */

#define AXL_UNDEF NAN

static inline bool axl_def(float x)
{
    return x == x;
}

static inline bool axl_undef(float x)
{
    return x != x;
}

static inline float axl_max_or_def(float a, float b)
{
    if (axl_def(a) && axl_def(b)) {
        return a > b ? a : b;
    }
    return axl_undef(a) ? b : a;
}

static inline float axl_min_or_def(float a, float b)
{
    if (axl_def(a) && axl_def(b)) {
        return a < b ? a : b;
    }
    return axl_undef(a) ? b : a;
}

static inline bool axl_inexact_eq(float a, float b)
{
    if (axl_def(a) && axl_def(b)) {
        return fabsf(a - b) < 0.0001f;
    }
    return axl_undef(a) && axl_undef(b);
}

/* ---- Axes and edges
 * ------------------------------------------------------------------------------ */

/* Axes are ax_flex_direction values. */
static inline bool axl_is_row(uint32_t axis)
{
    return axis == AX_FLEX_DIRECTION_ROW || axis == AX_FLEX_DIRECTION_ROW_REVERSE;
}

static inline bool axl_is_reverse(uint32_t axis)
{
    return axis == AX_FLEX_DIRECTION_ROW_REVERSE || axis == AX_FLEX_DIRECTION_COLUMN_REVERSE;
}

/* 0: width, 1: height. */
static inline int axl_dim(uint32_t axis)
{
    return axl_is_row(axis) ? 0 : 1;
}

/* The edge where the axis starts (flex-start) and ends. */
static inline int axl_lead(uint32_t axis)
{
    switch (axis) {
    case AX_FLEX_DIRECTION_ROW:
        return AX_EDGE_LEFT;
    case AX_FLEX_DIRECTION_ROW_REVERSE:
        return AX_EDGE_RIGHT;
    case AX_FLEX_DIRECTION_COLUMN_REVERSE:
        return AX_EDGE_BOTTOM;
    default:
        return AX_EDGE_TOP;
    }
}

static inline int axl_trail(uint32_t axis)
{
    switch (axis) {
    case AX_FLEX_DIRECTION_ROW:
        return AX_EDGE_RIGHT;
    case AX_FLEX_DIRECTION_ROW_REVERSE:
        return AX_EDGE_LEFT;
    case AX_FLEX_DIRECTION_COLUMN_REVERSE:
        return AX_EDGE_TOP;
    default:
        return AX_EDGE_BOTTOM;
    }
}

/* Inline start and end, left to right (RTL arrives with bidi, M3). */
static inline int axl_inline_start(uint32_t axis)
{
    return axl_is_row(axis) ? AX_EDGE_LEFT : AX_EDGE_TOP;
}

static inline int axl_inline_end(uint32_t axis)
{
    return axl_is_row(axis) ? AX_EDGE_RIGHT : AX_EDGE_BOTTOM;
}

static inline float axl_measured(const ax_node_layout *b, int dim)
{
    return dim == 0 ? b->measured_w : b->measured_h;
}

static inline void axl_set_measured(ax_node_layout *b, int dim, float v)
{
    if (dim == 0) {
        b->measured_w = v;
    } else {
        b->measured_h = v;
    }
}

/* ---- Style
 * ---------------------------------------------------------------------------------------- */

static inline bool axl_dim_eq(ax_dimension a, ax_dimension b)
{
    if (a.unit != b.unit) {
        return false;
    }
    return a.unit == AX_UNIT_AUTO || axl_inexact_eq(a.value, b.value);
}

/* WIDTH or HEIGHT, or the maximum when it equals the minimum (Yoga's processed dimension). */
static inline ax_dimension axl_size_style(const ax_node_style *st, int dim)
{
    ax_dimension mx = dim == 0 ? st->max_width : st->max_height;
    ax_dimension mn = dim == 0 ? st->min_width : st->min_height;
    if (mx.unit != AX_UNIT_AUTO && axl_dim_eq(mx, mn)) {
        return mx;
    }
    return dim == 0 ? st->width : st->height;
}

static inline float axl_padding(const ax_node_style *st, int edge, float width_size)
{
    return axl_max_or_def(ax_layout_resolve(st->padding[edge], width_size), 0.0f);
}

static inline float axl_border(const ax_node_style *st, int edge)
{
    return st->border[edge] > 0.0f ? st->border[edge] : 0.0f;
}

static inline float axl_pad_border(const ax_node_style *st, int edge, float width_size)
{
    return axl_padding(st, edge, width_size) + axl_border(st, edge);
}

static inline float axl_pad_border_axis(const ax_node_style *st, uint32_t axis, float width_size)
{
    return axl_pad_border(st, axl_inline_start(axis), width_size) +
           axl_pad_border(st, axl_inline_end(axis), width_size);
}

/* Padding and border along a dimension (0: horizontal). */
static inline float axl_pad_border_dim(const ax_node_style *st, int dim, float width_size)
{
    return axl_pad_border_axis(st, dim == 0 ? AX_FLEX_DIRECTION_ROW : AX_FLEX_DIRECTION_COLUMN,
                               width_size);
}

/* A size, minimum or maximum resolved against ref, with padding and border added for a
 * content-box node. */
static inline float axl_box_size(const ax_node_style *st, ax_dimension d, int dim, float ref,
                                 float width_size)
{
    float v = ax_layout_resolve(d, ref);
    if (st->box_sizing == AX_BOX_SIZING_CONTENT_BOX) {
        v = v + axl_pad_border_dim(st, dim, width_size);
    }
    return v;
}

static inline float axl_resolved_size(const ax_node_style *st, int dim, float ref, float width_size)
{
    return axl_box_size(st, axl_size_style(st, dim), dim, ref, width_size);
}

static inline float axl_resolved_min(const ax_node_style *st, int dim, float ref, float width_size)
{
    return axl_box_size(st, dim == 0 ? st->min_width : st->min_height, dim, ref, width_size);
}

static inline float axl_resolved_max(const ax_node_style *st, int dim, float ref, float width_size)
{
    return axl_box_size(st, dim == 0 ? st->max_width : st->max_height, dim, ref, width_size);
}

/* Whether the size along dim resolves to a length of at least 0. */
static inline bool axl_definite(const ax_node_style *st, int dim, float owner_size)
{
    float v = ax_layout_resolve(axl_size_style(st, dim), owner_size);
    return axl_def(v) && v >= 0.0f;
}

/* A margin (AUTO counts as 0 here). */
static inline float axl_margin(const ax_node_style *st, int edge, float width_size)
{
    float v = ax_layout_resolve(st->margin[edge], width_size);
    return axl_def(v) ? v : 0.0f;
}

static inline float axl_margin_axis(const ax_node_style *st, uint32_t axis, float width_size)
{
    return axl_margin(st, axl_inline_start(axis), width_size) +
           axl_margin(st, axl_inline_end(axis), width_size);
}

static inline bool axl_margin_auto(const ax_node_style *st, int edge)
{
    return st->margin[edge].unit == AX_UNIT_AUTO;
}

static inline bool axl_inset_defined(const ax_node_style *st, int edge)
{
    return st->inset[edge].unit != AX_UNIT_AUTO;
}

static inline float axl_inset(const ax_node_style *st, int edge, float axis_size)
{
    float v = ax_layout_resolve(st->inset[edge], axis_size);
    return axl_def(v) ? v : 0.0f;
}

static inline float axl_gap(const ax_node_style *st, uint32_t axis, float owner_size)
{
    return axl_max_or_def(ax_layout_resolve(st->gap[axl_is_row(axis) ? 0 : 1], owner_size), 0.0f);
}

/* The min and max along the axis bound the value (min wins over max, as in Yoga). */
static inline float axl_bound_min_max(const ax_node_style *st, uint32_t axis, float value,
                                      float axis_size, float width_size)
{
    int dim = axl_dim(axis);
    float mn = axl_resolved_min(st, dim, axis_size, width_size);
    float mx = axl_resolved_max(st, dim, axis_size, width_size);
    if (axl_def(mx) && mx >= 0.0f && axl_def(value) && value > mx) {
        return mx;
    }
    if (axl_def(mn) && mn >= 0.0f && axl_def(value) && value < mn) {
        return mn;
    }
    return value;
}

/* As axl_bound_min_max, and never below the padding and border. */
static inline float axl_bound(const ax_node_style *st, uint32_t axis, float value, float axis_size,
                              float width_size)
{
    return axl_max_or_def(axl_bound_min_max(st, axis, value, axis_size, width_size),
                          axl_pad_border_axis(st, axis, width_size));
}

/* ---- Shared by the core and protocols
 * -------------------------------------------------------------- */

/* The measure function of a node, if it has one. */
const ax_layout_measure_fn *axl_measure_of(ax_layout_ctx *c, uint32_t row, void **out_user);

/* The flex protocol (layout_flex.c). */
extern const ax_layout_protocol ax__layout_flex;

#endif /* AX_CORE_LAYOUT_INTERNAL_H */
