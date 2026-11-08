/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: the app thread's mirror of the tree.
 *
 * The scene thread publishes, at the end of each frame, a consistent copy of what the app may read
 * without a round trip: which nodes are live, their parents, the values of core properties and
 * the frames layout computed. The app thread latches the newest publication with
 * ax_world_mirror_sync, and every read until the next sync sees that same publication, so a
 * sequence of reads can never mix two frames or see part of a transaction.
 *
 *     uint64_t seq = ax_world_mirror_sync(world);   // transactions up to seq are visible
 *     float opacity;
 *     if (ax_mirror_get_f32(world, card, AX_PROP_OPACITY, &opacity) == AX_OK) { ... }
 *
 * Before the first sync, or before the scene publishes, the mirror is empty. Values are the
 * node's own (inherited values are not resolved), as the app set them; properties registered at
 * run time are not mirrored yet. Every function here runs on the world's app thread.
 */
#ifndef AX_MIRROR_H
#define AX_MIRROR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ax_node.h"
#include "ax_prop.h"
#include "ax_world.h"
#include <axion/ax_config.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

/* A rectangle in logical units: origin (x, y), extending w right and h down. */
typedef struct ax_rectf {
    float x, y, w, h;
} ax_rectf;

/* Latches the newest publication. Returns the sequence number of the newest transaction it
 * includes (0 when nothing has been published). thread: app */
AX_API uint64_t ax_world_mirror_sync(ax_world world);

/* Whether the node was live in the latched publication. thread: app */
AX_API bool ax_mirror_is_live(ax_world world, ax_node node);

/* The node's parent in the latched publication; null for a root or a node that is not live.
 * thread: app */
AX_API ax_node ax_mirror_parent(ax_world world, ax_node node);

/*
 * A core paint or composite property's value in the latched publication. The getter must match the
 * property's value type. A shorthand reads like the scene (decision 63): its longhands' common
 * value, or AX_ERR_INVALID_ARGUMENT when they differ. Layout's inputs (WIDTH, PADDING,
 * FLEX_GROW...) are not mirrored: read the frame layout computed from them with
 * ax_mirror_get_frame. Errors: AX_ERR_INVALID_HANDLE (not live there), AX_ERR_INVALID_ARGUMENT
 * (unknown property, wrong type), AX_ERR_UNSUPPORTED (a layout input, or a property registered at
 * run time). thread: app
 */
AX_API ax_result ax_mirror_get_f32(ax_world world, ax_node node, ax_prop prop, float *out);
AX_API ax_result ax_mirror_get_dimension(ax_world world, ax_node node, ax_prop prop,
                                         ax_dimension *out);
AX_API ax_result ax_mirror_get_color(ax_world world, ax_node node, ax_prop prop, ax_rgba *out);
AX_API ax_result ax_mirror_get_transform(ax_world world, ax_node node, ax_prop prop,
                                         ax_transform2d *out);

/*
 * A string property's value in the latched publication: AX_PROP_TEXT, the content the app set
 * (decision 99). Copies at most capacity - 1 bytes and a NUL into buffer (may be NULL with
 * capacity 0), and sets *out_length (may be NULL) to the whole value's length, so a caller can
 * size its buffer first. A node without text reads as "". The text style properties (font
 * family, features) are not mirrored yet: AX_ERR_UNSUPPORTED. Errors: AX_ERR_INVALID_HANDLE,
 * AX_ERR_INVALID_ARGUMENT (not a string property). thread: app
 */
AX_API ax_result ax_mirror_get_str(ax_world world, ax_node node, ax_prop prop, char *buffer,
                                   size_t capacity, size_t *out_length);

/* The frame layout computed for the node (in its parent's space, before its transform, rounded to
 * its window's pixel grid), as of the latched publication. Errors: AX_ERR_INVALID_HANDLE.
 * thread: app */
AX_API ax_result ax_mirror_get_frame(ax_world world, ax_node node, ax_rectf *out);

AX_EXTERN_C_END

#endif /* AX_MIRROR_H */
