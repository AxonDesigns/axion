/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the scene, the scene thread's retained tree with its semantics. The node store is
 * the storage mechanism; the scene adds property validation, values by property id and dirty
 * propagation. Transactions are applied through these functions.
 *
 * Dirty flags (ax_node_store.dirty, per node):
 *   STYLE         a computed (inherited) value of this node may have changed
 *   LAYOUT        this node's own layout inputs changed, or its children did
 *   LAYOUT_CHILD  a descendant below this node, up to the relayout boundary, needs layout
 *   PAINT         this node's paint record must be recorded again
 *   COMPOSITE     this node's composite properties changed (transform, opacity)
 *   LAYOUT_ROOT   this node is in the dirty set's list of layout roots
 * A new node has STYLE | LAYOUT | PAINT | COMPOSITE.
 *
 * Relayout boundaries. A node whose size does not depend on its children: a root (no parent), or
 * a node with WIDTH and HEIGHT both in points. (Scroll viewports join when scrolling exists.)
 * Layout dirtiness climbs from a change up to the nearest boundary and stops there, and that
 * boundary is recorded as a layout root: layout runs from each root over its dirty subtree, so
 * editing a list item never relayouts the window, and independent roots can be laid out in
 * parallel. The list may hold candidates that have since gained a parent or been destroyed;
 * layout skips or reconciles them.
 *
 * By effect class, a property change marks:
 *   - LAYOUT: the node LAYOUT; its size or position may change, so its parent and the ancestors
 *     above it get LAYOUT_CHILD up to the nearest boundary at or above the parent, the root.
 *     A node without parent is its own root.
 *   - PAINT: the node PAINT. COMPOSITE: the node COMPOSITE. Nothing else: the stored transform is
 *     local, so a transform change never touches the subtree.
 *   - An inherited property: STYLE plus its effect on the node and on every descendant.
 * Children added or removed mark the parent LAYOUT; if the parent is not a boundary, dirtiness
 * climbs from its parent. A moved node gets LAYOUT. Setting a property to the value it already
 * has marks nothing.
 *
 * Flags stay set until the frame that consumes them clears them (ax_scene_clear_dirty). Marking
 * never fails: if the root list cannot grow, `roots_overflowed` is set and layout must treat every
 * root as dirty.
 */
#ifndef AX_CORE_SCENE_H
#define AX_CORE_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_prop.h"
#include "ax_layout.h"
#include "ax_node_store.h"
#include "ax_prop_registry.h"
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

#define AX_DIRTY_STYLE        0x01u
#define AX_DIRTY_LAYOUT       0x02u
#define AX_DIRTY_LAYOUT_CHILD 0x04u
#define AX_DIRTY_PAINT        0x08u
#define AX_DIRTY_COMPOSITE    0x10u
#define AX_DIRTY_LAYOUT_ROOT  0x20u
#define AX_DIRTY_NEW_NODE (AX_DIRTY_STYLE | AX_DIRTY_LAYOUT | AX_DIRTY_PAINT | AX_DIRTY_COMPOSITE)

typedef struct ax_dirty_set {
    ax_node *layout_roots; /* relayout boundaries with dirty layout at or below them */
    uint32_t layout_root_count;
    uint32_t layout_root_capacity;
    bool roots_overflowed; /* a root could not be recorded: lay out every root */
} ax_dirty_set;

typedef struct ax_scene {
    ax_node_store store;
    ax_prop_registry props;
    ax_dirty_set dirty;
    /* Layout (ax_layout.h). */
    ax_component_type measure_component; /* a leaf's measure function */
    uint32_t layout_pass;                /* the newest pass */
    ax_layout_counters layout_counters;
} ax_scene;

/* Never fails. desc is the node store's (allocator, slot mode). */
AX_API void ax_scene_init(ax_scene *scene, const ax_node_store_desc *desc);
AX_API void ax_scene_deinit(ax_scene *scene);

/* ---- Tree ------------------------------------------------------------------------------------ */

/* As the node store's create / create_at, marking the new node AX_DIRTY_NEW_NODE and a root. */
AX_API ax_result ax_scene_create(ax_scene *scene, ax_node_kind kind, ax_node *out);
AX_API ax_result ax_scene_create_at(ax_scene *scene, ax_node node, ax_node_kind kind);

/* As the node store's insert_before (null before appends), marking both parents and the node. */
AX_API ax_result ax_scene_insert_before(ax_scene *scene, ax_node parent, ax_node child,
                                        ax_node before);

/* Destroys the node and its subtree, marking its parent. */
AX_API ax_result ax_scene_destroy(ax_scene *scene, ax_node node);

/* Detaches the node from its parent (a root afterwards), marking the parent and the node. */
AX_API ax_result ax_scene_detach(ax_scene *scene, ax_node node);

/* ---- Properties ------------------------------------------------------------------------------ */

/*
 * Checks a value for a property without setting it. type must be the property's value type.
 *   - AX_ERR_INVALID_ARGUMENT: unknown property, wrong type, a value that is not finite, outside
 *     the property's range, or an unknown unit.
 *   - AX_ERR_UNSUPPORTED: a percentage, until layout exists.
 * Records ax_last_error with the reason.
 */
AX_API ax_result ax_scene_check_value(const ax_scene *scene, ax_prop prop, ax_value_type type,
                                      const ax_value *value);

/* Sets a property (a shorthand sets its longhands) and marks the node by the property's effect.
 * Errors: AX_ERR_INVALID_HANDLE, and those of ax_scene_check_value. */
AX_API ax_result ax_scene_set(ax_scene *scene, ax_node node, ax_prop prop, ax_value_type type,
                              const ax_value *value);

/* Reads a property's stored value (the default for a run-time property the node never set). A
 * shorthand reads its longhands' common value: AX_ERR_INVALID_ARGUMENT when they differ.
 * Inherited values are not resolved: this is the node's own value. */
AX_API ax_result ax_scene_get(const ax_scene *scene, ax_node node, ax_prop prop, ax_value *out);

/* ---- Dirty state ----------------------------------------------------------------------------- */

/* Marks the node's layout dirty, as a change of one of its layout properties would (its size or
 * position may change): for inputs the scene owns, such as a window root's viewport. */
AX_API void ax_scene_mark_layout(ax_scene *scene, ax_node node);

AX_API bool ax_scene_is_relayout_boundary(const ax_scene *scene, ax_node node);

/* The node's dirty flags (0 for a node that is not live). */
AX_API uint32_t ax_scene_dirty_flags(const ax_scene *scene, ax_node node);

/* Clears every node's flags and the root list (the frame consumed them). */
AX_API void ax_scene_clear_dirty(ax_scene *scene);

AX_EXTERN_C_END

#endif /* AX_CORE_SCENE_H */
