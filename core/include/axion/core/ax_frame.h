/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the scene thread's frame stages after applying transactions. Each stage is a
 * function of the world alone: it does not know whether a dedicated scene thread or the
 * single-threaded scheduler calls it (ax_runtime orders them).
 *
 *     ax_world_apply_pending(world, deadline);   // ax_world_sdk.h
 *     ax_world_position(world);                   // frames from LEFT, TOP, WIDTH, HEIGHT
 *     ax_world_record_collect(world);             // what changed, before the flags are cleared
 *     if (ax_world_root_changed(world, root, &seq))
 *         ax_world_record_root(world, root, builder);   // into the root's mailbox
 *     ax_world_publish_mirror(world);
 *     ax_scene_clear_dirty(ax_world_scene(world));
 *
 * Window roots. A root attached to a surface has a viewport, the surface's logical size: it is
 * laid out at (0, 0, width, height) whatever its own layout properties.
 *
 * Positioning (until flex): a node's frame is (LEFT, TOP, WIDTH, HEIGHT) in points, relative to
 * its parent's top-left; AUTO is 0. Only layout roots marked dirty (decision 64) are walked,
 * descending through nodes marked LAYOUT or LAYOUT_CHILD; stale roots are skipped, and every
 * tree is walked when the root list overflowed. Changed frames go to the store's layout column
 * and to the mirror (ax_world_touch_node).
 *
 * Layers. A window root, and every node that has ever had a TRANSFORM other than the identity or
 * an OPACITY other than 1, records a layer: its transform is translate(its origin in the parent
 * layer) times its TRANSFORM about the center of its frame (decision 63), with its opacity.
 * Promotion is sticky: a node stays a layer until it is destroyed, so animating a transform never
 * records items again. Its items are in its own space, origin at its frame's top-left; a node
 * that is not a layer records into its nearest layer's space.
 *
 * Incremental recording. Each node's items (its background: a rect, or a rounded rect when a
 * corner has a radius) are kept encoded, in the space of their layer. A node's items are recorded
 * again only when it was marked PAINT, or when layout changed its frame's size or its place in
 * its layer (it moved, an ancestor in the same layer moved, or a node above it became a layer);
 * otherwise they are copied. A LAYOUT mark that leaves the frame as it was (a parent whose
 * children changed) records nothing, and a layer that moves only changes its layer record, as a
 * change of TRANSFORM or OPACITY does.
 *
 * thread: scene, for every function here.
 */
#ifndef AX_CORE_FRAME_H
#define AX_CORE_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_world.h"
#include <axion/base/ax_compiler.h>
#include <axion/render/ax_render_ir.h>

AX_EXTERN_C_BEGIN

/*
 * Makes `root` a window root with a viewport of width x height logical units, or changes its
 * viewport (marking it for layout when the size changes).
 * Errors: AX_ERR_INVALID_HANDLE (not live), AX_ERR_INVALID_ARGUMENT (the node has a parent, or the
 * size is negative or not finite), AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_world_set_root_viewport(ax_world world, ax_node root, float width,
                                            float height);

/* Forgets a window root (its window closed); the node itself stays. */
AX_API void ax_world_remove_root(ax_world world, ax_node root);

/* Positions the dirty part of the tree. Returns how many frames changed. */
AX_API uint32_t ax_world_position(ax_world world);

/*
 * Reads the dirty flags before they are cleared: marks the items of nodes with PAINT for
 * recording, and the window roots whose content changed, with the newest transaction applied.
 * Call it once per frame after positioning and before ax_scene_clear_dirty.
 */
AX_API void ax_world_record_collect(ax_world world);

/* Whether the root's content changed since it was last recorded (true before its first record).
 * *out_seq (may be NULL): the newest transaction applied when it changed. */
AX_API bool ax_world_root_changed(ax_world world, ax_node root, uint64_t *out_seq);

/*
 * Records the root's tree into `b`, which the caller has begun (ax_ir_builder_begin, or
 * ax_ir_mailbox_begin) at the root's viewport size, and clears its changed state.
 * Errors: AX_ERR_INVALID_HANDLE (not a live window root), AX_ERR_OUT_OF_MEMORY (the cache could
 * not grow; the builder may hold part of the tree).
 */
AX_API ax_result ax_world_record_root(ax_world world, ax_node root, ax_ir_builder *b);

/* Counters since the last reset, for tests and profiles. */
typedef struct ax_frame_counters {
    uint64_t frames_changed;  /* positioning: frames written */
    uint64_t nodes_recorded;  /* nodes whose items were recorded again */
    uint64_t items_recorded;  /* items made from properties */
    uint64_t items_reused;    /* items copied from the cache */
    uint64_t layers;          /* layers recorded */
    uint64_t layers_promoted; /* nodes that became layers */
    uint64_t snapshots;       /* ax_world_record_root calls that succeeded */
} ax_frame_counters;

AX_API void ax_world_get_frame_counters(ax_world world, ax_frame_counters *out, bool reset);

AX_EXTERN_C_END

#endif /* AX_CORE_FRAME_H */
