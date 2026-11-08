/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the scene thread's frame stages after applying transactions. Each stage is a
 * function of the world alone: it does not know whether a dedicated scene thread or the
 * single-threaded scheduler calls it (ax_runtime orders them).
 *
 *     ax_world_apply_pending(world, deadline);   // ax_world_sdk.h
 *     ax_world_position(world);                   // layout (ax_layout.h): flex
 *     ax_world_animate(world, frame_time);        // implicit animations, sampled at T
 *     ax_world_record_collect(world);             // what changed, before the flags are cleared
 *     if (ax_world_root_changed(world, root, &seq))
 *         ax_world_record_root(world, root, builder);   // into the root's mailbox
 *     ax_world_publish_mirror(world);
 *     ax_scene_clear_dirty(ax_world_scene(world));
 *
 * Window roots. A root attached to a surface has a viewport, the surface's logical size: it is
 * laid out at (0, 0, width, height) whatever its own layout properties.
 *
 * Positioning is the layout engine's pass (ax_layout.h, decision 90): the dirty layout roots and
 * relayout boundaries of decision 64, flex for every node, independent boundaries in parallel on
 * the world's job system (ax_world_set_jobs), frames rounded to the window's pixel grid. Changed
 * frames go to the store's layout column and to the mirror (ax_world_touch_node). A window root is
 * laid out at its viewport whatever its own size, margins and insets.
 *
 * Clips. A node with OVERFLOW hidden and children records a clip layer around its children: its
 * frame (0, 0, width, height), a rounded rect when it has corner radii, in its own space. Its own
 * items stay outside the clip (decision 93). Nodes with DISPLAY none record nothing.
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

#include "ax_anim.h"
#include "ax_prop_registry.h"

AX_EXTERN_C_BEGIN

/*
 * Makes `root` a window root with a viewport of width x height logical units and `scale` pixels
 * per unit (layout rounds frames to that pixel grid), or changes them (marking it for layout).
 * Errors: AX_ERR_INVALID_HANDLE (not live), AX_ERR_INVALID_ARGUMENT (the node has a parent, the
 * size is negative or not finite, the scale not positive), AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_world_set_root_viewport(ax_world world, ax_node root, float width, float height,
                                            float scale);

/* Forgets a window root (its window closed); the node itself stays. */
AX_API void ax_world_remove_root(ax_world world, ax_node root);

/* Lays out the dirty part of the tree. Returns how many frames changed. */
AX_API uint32_t ax_world_position(ax_world world);

/*
 * Implicit animations (decision 94), after layout and before recording: starts or retargets the
 * animations of the frames layout changed (when an animated transaction caused the change and it
 * was not a viewport's resize), samples every running animation at frame_time_ns (the frame's
 * predicted present time, on the world's clock) and marks what moved for recording, which then
 * draws presentation values. The first call starts the animator: changes applied before it (a
 * window's first content, tests that never animate) take effect at once. Returns the number of
 * nodes still animating.
 */
AX_API uint32_t ax_world_animate(ax_world world, uint64_t frame_time_ns);

/* Whether something under the window root animated at the last ax_world_animate: the window needs
 * the next frame even without new transactions. */
AX_API bool ax_world_root_animating(ax_world world, ax_node root);

/*
 * What a node shows at the last ax_world_animate (decision 94): a paint or composite property's
 * presentation value (target plus animation); its frame
 * goes through ax_world_presentation_frame. Hit testing and devtools read these; the mirror
 * reflects targets. Errors: AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (a property that
 * does not animate).
 */
AX_API ax_result ax_world_presentation(ax_world world, ax_node node, ax_prop prop, ax_value *out);
AX_API ax_result ax_world_presentation_frame(ax_world world, ax_node node, ax_rect *out);

/* The animator's counters since the last reset (ax_anim.h). */
AX_API void ax_world_get_anim_counters(ax_world world, ax_anim_counters *out, bool reset);

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

/*
 * Lets the text system evict glyphs that no snapshot still drawable uses (decision 100): the
 * runtime passes the oldest resource epoch of the snapshots its mailboxes hold
 * (ax_ir_mailbox_oldest_epoch). Returns the number of glyphs evicted.
 */
AX_API uint32_t ax_world_retire_glyphs(ax_world world, uint64_t oldest_live_epoch);

/*
 * Glyphs rasterized at once for one snapshot before the rest go to background jobs (decision 100).
 * AX_GLYPH_BUDGET_AUTO (the default): 1024 with a threaded job system, 256 otherwise (one thread
 * never defers). For tests and tuning.
 */
#define AX_GLYPH_BUDGET_AUTO UINT32_MAX
AX_API void ax_world_set_glyph_budget(ax_world world, uint32_t glyphs);

/* Counters since the last reset, for tests and profiles. */
typedef struct ax_frame_counters {
    uint64_t frames_changed;    /* layout: frames that changed */
    uint64_t nodes_recorded;    /* nodes whose items were recorded again */
    uint64_t items_recorded;    /* items made from properties */
    uint64_t items_reused;      /* items copied from the cache */
    uint64_t layers;            /* layers recorded */
    uint64_t layers_promoted;   /* nodes that became layers */
    uint64_t clips;             /* clip layers recorded (overflow hidden) */
    uint64_t layers_demoted;    /* layers back in their parent's space after a neutral second */
    uint64_t present_relayouts; /* resizing nodes whose children were laid out again */
    uint64_t snapshots;         /* ax_world_record_root calls that succeeded */
    uint64_t glyph_runs;          /* glyph runs recorded (decision 101) */
    uint64_t glyph_runs_deferred; /* runs left out whole: their glyphs were not ready yet */
    uint64_t text_rescaled;       /* layers whose settled scale changed: their text records again */
} ax_frame_counters;

AX_API void ax_world_get_frame_counters(ax_world world, ax_frame_counters *out, bool reset);

AX_EXTERN_C_END

#endif /* AX_CORE_FRAME_H */
