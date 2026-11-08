/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the layout engine (decision 90).
 *
 * Protocol. Layout follows "constraints down, sizes up, parent positions": a parent asks each child
 * for its size under constraints (an available width and height, each with a mode), the child
 * answers with a size, and the parent positions it. The engine's core is protocol-neutral:
 *   - ax_layout_node: the entry for every node, with the measurement cache (the last layout and up
 *     to AX_LAYOUT_MEASURE_CACHE measurements, by constraint) and the dirty check;
 *   - leaves: a measure function (text, tests), or a node without children (padding and border);
 *   - the pass over the dirty part of the tree: layout roots and relayout boundaries (decision 64),
 *     independent boundaries in parallel on the job system, rounding to the pixel grid, and the
 *     changed frames reported to the caller;
 *   - the output: each node's ax_node_layout (store column), its `frame` the rounded result.
 * A protocol (ax_layout_protocol) lays out one node's children: flex is the first one
 * (layout_flex.c, Yoga-compatible semantics). The node's DISPLAY selects it; grid (M2), stack and
 * custom layouts are rows added to the protocol table, with their data in side components
 * (decision 89), without changes to this core.
 *
 * Hooks for later work (decision 90):
 *   - layout animations: a pass writes into the ax_node_layout array its context names; the
 *     resize preset will lay a subtree out again each frame into a presentation array of its own,
 *     with the same code and its own cache, without touching the target layout the mirror shows;
 *   - virtualization: a virtual layout is a protocol that answers for the children it has built and
 *     estimates the rest; ax_layout_node lets it lay out only the built range.
 *
 * Modes (Yoga's measure modes):
 *   EXACT    the size is the available size (stretch-fit);
 *   AT_MOST  the size is at most the available size (fit-content);
 *   UNDEFINED there is no limit; the available size is NaN (max-content).
 * Undefined lengths are NaN throughout.
 *
 * thread: scene (or a frame job of the scene's pass), for every function here.
 */
#ifndef AX_CORE_LAYOUT_H
#define AX_CORE_LAYOUT_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "ax_node_store.h"
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

struct ax_scene;
struct ax_job_system;

typedef enum ax_layout_mode {
    AX_LAYOUT_UNDEFINED = 0, /* max-content: no limit */
    AX_LAYOUT_EXACT = 1,     /* stretch-fit */
    AX_LAYOUT_AT_MOST = 2    /* fit-content */
} ax_layout_mode;

typedef struct ax_layout_ctx ax_layout_ctx;

/* What a parent asks of a node: available sizes (NaN when undefined), including the node's own
 * margins, the modes, and the size of its containing block (for percentages). */
typedef struct ax_layout_request {
    float avail_w, avail_h;
    ax_layout_mode mode_w, mode_h;
    float owner_w, owner_h;
    bool perform; /* position the children too (a layout), or only compute the size (a measure) */
} ax_layout_request;

/*
 * A layout protocol: computes the size of a node that has children, for the request, and writes it
 * to its ax_node_layout's measured_w and measured_h. When the request performs, it also lays the
 * children out (ax_layout_node with perform) and writes their positions (`pos`). Measuring and
 * laying out are one entry, as in Yoga, because a protocol shares nearly all of its work between
 * them. The node's margins, padding and border are already resolved in its ax_node_layout.
 */
typedef struct ax_layout_protocol {
    const char *name;
    void (*layout)(ax_layout_ctx *ctx, uint32_t row, const ax_layout_request *req);
} ax_layout_protocol;

/* A leaf's intrinsic size, given its content box's available size (NaN when undefined) and modes.
 * Called from the scene thread or from a worker during a parallel pass: it must be thread-safe and
 * must not touch the scene. */
typedef void (*ax_layout_measure_fn)(void *user, ax_node node, float width,
                                     ax_layout_mode width_mode, float height,
                                     ax_layout_mode height_mode, float *out_width,
                                     float *out_height);

/* Counters of a pass (summed over passes until reset). */
typedef struct ax_layout_counters {
    uint64_t passes;
    uint64_t roots;          /* layout roots laid out (boundaries and trees) */
    uint64_t parallel_roots; /* of those, laid out in frame jobs */
    uint64_t layouts;        /* nodes laid out (a protocol or leaf run with perform) */
    uint64_t measures;       /* nodes measured without laying out */
    uint64_t cached;         /* requests answered from the cache */
    uint64_t measure_calls;  /* measure functions called */
    uint64_t frames_changed; /* rounded frames that changed */
} ax_layout_counters;

/* ---- For protocols -----------------------------------------------------------------------------
 */

/* Lays out or measures a child (or any node) for the request: the cache, the leaf cases and its
 * protocol. Returns true when it did work, false when the cache answered. */
AX_API bool ax_layout_node(ax_layout_ctx *ctx, uint32_t row, const ax_layout_request *req);

/* The store and the layout array the pass writes. */
AX_API ax_node_store *ax_layout_store(ax_layout_ctx *ctx);
AX_API ax_node_layout *ax_layout_box(ax_layout_ctx *ctx, uint32_t row);

/* The style the pass uses for a row (a window root's own size, margins and insets are replaced by
 * its viewport). */
AX_API const ax_node_style *ax_layout_style(ax_layout_ctx *ctx, uint32_t row);

/* Whether the row is the root of the tree being laid out (no parent). */
AX_API bool ax_layout_is_tree_root(ax_layout_ctx *ctx, uint32_t row);

/* Zeroes the layout of a subtree that takes no space (DISPLAY none). */
AX_API void ax_layout_zero_subtree(ax_layout_ctx *ctx, uint32_t row);

/* ---- Style resolution (shared by protocols)
 * ------------------------------------------------------ */

/* NaN for AUTO, the value in points, or a percentage of ref (NaN when ref is NaN). */
static inline float ax_layout_resolve(ax_dimension d, float ref)
{
    switch (d.unit) {
    case AX_UNIT_POINTS:
        return d.value;
    case AX_UNIT_PERCENT:
        return d.value * ref * 0.01f;
    default:
        return NAN;
    }
}

/* ---- The pass
 * ------------------------------------------------------------------------------------ */

/* Asks for a window root's viewport: true when `root` (a parentless node) is a window root, with
 * its logical size and its pixel scale (rounding). */
typedef bool (*ax_layout_viewport_fn)(void *user, ax_node root, float *width, float *height,
                                      float *scale);

typedef struct ax_layout_pass_desc {
    struct ax_job_system *jobs; /* NULL: one thread */
    /* Independent boundaries go to frame jobs when at least two of them have at least this many
     * nodes laid out last time (0: a default of 64). */
    uint32_t parallel_min_nodes;
    ax_layout_viewport_fn viewport; /* NULL: no window roots */
    void *viewport_user;
    /* Called once per changed frame, after the pass, on the calling thread (may be NULL). */
    void (*frame_changed)(void *user, uint32_t slot);
    void *frame_changed_user;
} ax_layout_pass_desc;

/*
 * Lays out the dirty part of the scene: each layout root of the dirty set (decision 64) that is
 * live and not covered by a dirty ancestor. A parentless root is laid out as a tree (its viewport
 * when it is a window root, otherwise its own properties against an undefined containing block,
 * pixel scale 1); a relayout boundary again with the constraints it last had. Boundaries whose
 * ancestors are not themselves being laid out are independent and run in parallel. Rounded frames
 * that changed are written to the store's layout column and reported.
 * Returns the number of changed frames; AX_ERR_OUT_OF_MEMORY is reported through ax_last_error
 * and every changed slot is then reported as changed (the caller treats them all as changed).
 */
AX_API uint32_t ax_layout_run(struct ax_scene *scene, const ax_layout_pass_desc *desc);

/* Lays out the tree under a parentless node completely, against a containing block of owner_w x
 * owner_h (NaN: undefined), with the given pixel scale (0: no rounding). For tests and tools. */
AX_API void ax_layout_tree(struct ax_scene *scene, ax_node root, float owner_w, float owner_h,
                           float scale);

AX_API void ax_layout_get_counters(struct ax_scene *scene, ax_layout_counters *out, bool reset);

/* Gives a leaf node a measure function (NULL removes it), marking it for layout. A node with a
 * measure function is laid out as a leaf: its children are not laid out.
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_layout_set_measure(struct ax_scene *scene, ax_node node,
                                       ax_layout_measure_fn fn, void *user);

AX_EXTERN_C_END

#endif /* AX_CORE_LAYOUT_H */
