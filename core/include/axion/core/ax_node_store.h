/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the node store, where the scene thread keeps the retained tree.
 *
 * Rows. Nodes are rows in dense component arrays (structure of arrays, ECS style). The hot
 * columns below are indexed by row and always dense: row_count rows, no holes. Destroying a node
 * moves the last row into its place (swap-remove), and ax_node_store_compact reorders the rows
 * into pre-order of the tree so traversals walk memory in order.
 *
 * Handles. A node's handle is (slot, generation). The slot table maps a slot to its current row,
 * so handles stay stable while rows move. Generations start at 1 and increase when the node is
 * destroyed; a slot whose generation would wrap is retired, never reused (as in ax_pool, decision
 * 7). Stale, null and unknown handles return AX_ERR_INVALID_HANDLE without asserting (decision
 * 6): the store is a mechanism and records no ax_last_error.
 *
 * Hierarchy links hold slots, not rows, so moving rows never rewrites them: parent, first and last
 * child, previous and next sibling, AX_NODE_NONE when absent.
 *
 * Slots: two modes, chosen at init.
 *   - Internal (on_slot_freed is NULL): the store hands out slots itself (ax_node_store_create),
 *     recycling freed ones LIFO.
 *   - External (on_slot_freed set): another party hands out handles (the app thread reserves them
 *     without waiting) and the store creates nodes at them (ax_node_store_create_at).
 *     create_at accepts any generation at least the slot's recorded one (any from 1 for a slot
 *     it has not seen) and adopts it: the other party may skip generations without telling the
 *     store, for handles that never reached it (an aborted transaction, a create after a failed
 *     command). When a node is destroyed, the store reports the slot's next handle (generation
 *     + 1) and keeps no free list. Whoever hands out handles retires a slot whose generation
 *     reaches UINT32_MAX, as the store does, so a generation is never reused.
 *
 * Side components: optional per-node data (text, image, scroll... arrive with their features),
 * registered at run time. Each type is stored dense with swap-remove, indexed by slot through a
 * paged sparse array, and a node's components are removed when it is destroyed.
 *
 * Thread: the store belongs to one thread, the scene thread. ax_node_store_init records the
 * calling thread; ax_node_store_bind_thread hands the store to the calling thread. Debug builds
 * assert in every function that the caller is that thread.
 */
#ifndef AX_CORE_NODE_STORE_H
#define AX_CORE_NODE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_prop.h"
#include <axion/ax_allocator.h>
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_math.h>

AX_EXTERN_C_BEGIN

#define AX_NODE_NONE UINT32_MAX /* no slot, no row */

/* Largest number of slots a store can create. */
#define AX_NODE_STORE_MAX_SLOTS 0xFFFFFFF0u

#define AX_NODE_MAX_COMPONENT_TYPES 32u

/* ---- Hot columns (one element per row) ------------------------------------------------------- */

/* Hierarchy, as slots. */
typedef struct ax_node_links {
    uint32_t parent;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t prev_sibling;
    uint32_t next_sibling;
} ax_node_links;

/* Style inputs of layout. Flex (direction, wrap, grow, shrink, basis, padding, gap...) adds
 * fields here when layout lands; text inputs go to a side component. */
typedef struct ax_node_style {
    ax_dimension width;
    ax_dimension height;
    ax_dimension left;
    ax_dimension top;
} ax_node_style;

/* Layout outputs: the frame in the parent's coordinate space, before the node's transform. Zero
 * until layout exists; published to the app thread's mirror. */
typedef struct ax_node_layout {
    ax_rect frame;
} ax_node_layout;

/* Paint properties. Corners in order top-left, top-right, bottom-right, bottom-left. */
typedef struct ax_node_paint {
    ax_color background; /* decision 50: linear light, premultiplied */
    float corner_radius[4];
} ax_node_paint;

/* Composite properties. The transform is local, applied about the center of the node's frame
 * (AX_PROP_TRANSFORM); the world transform is computed when needed, never stored. */
typedef struct ax_node_composite {
    ax_affine2 transform;
    float opacity;
} ax_node_composite;

/* ---- Slots ----------------------------------------------------------------------------------- */

#define AX_NODE_SLOT_RETIRED 0xFFFFFFFEu /* row value: generation exhausted; never reused */

typedef struct ax_node_slot {
    uint32_t generation; /* of the live node, or of the next one; never 0 */
    uint32_t row;        /* the live node's row, AX_NODE_NONE when free, or _RETIRED */
    uint32_t next_free;  /* internal mode: next free slot, or AX_NODE_NONE */
} ax_node_slot;

/* ---- Side components ------------------------------------------------------------------------- */

typedef uint32_t ax_component_type;

typedef struct ax_component_desc {
    size_t size;  /* > 0 */
    size_t align; /* power of two */
    /* Called before an element is removed: explicitly, with its node, or at deinit. May be NULL.
     * It must not call back into the store. */
    void (*on_remove)(void *element, void *user);
    void *user;
} ax_component_desc;

typedef struct ax_component_storage {
    ax_component_desc desc;
    size_t stride;        /* size rounded up to align */
    unsigned char *data;  /* count elements, dense */
    uint32_t *owner_slot; /* slot owning each element */
    uint32_t count;
    uint32_t capacity;
    uint32_t **sparse; /* pages of element index + 1 (0: none), by slot */
    uint32_t sparse_pages;
} ax_component_storage;

/* ---- The store ------------------------------------------------------------------------------- */

typedef struct ax_node_store_desc {
    const ax_allocator *allocator; /* NULL: ax_default_allocator(); copied */
    /* External slots mode when set: called on the owning thread, once per destroyed node, with
     * the handle the slot's next occupant must use. Not called for retired slots. */
    void (*on_slot_freed)(void *user, ax_node next_handle);
    void *user;
} ax_node_store_desc;

typedef struct ax_node_store {
    ax_allocator allocator;
    uint64_t owner_thread;
    void (*on_slot_freed)(void *user, ax_node next_handle);
    void *user;

    ax_node_slot *slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t free_head; /* internal mode */

    uint32_t row_count;
    uint32_t row_capacity;
    uint32_t *slot_of_row;
    uint16_t *kind; /* ax_node_kind */
    uint32_t *dirty;
    uint32_t *component_mask; /* bit i: has component type i */
    ax_node_links *links;
    ax_node_style *style;
    ax_node_layout *layout;
    ax_node_paint *paint;
    ax_node_composite *composite;

    ax_component_storage components[AX_NODE_MAX_COMPONENT_TYPES];
    uint32_t component_type_count;
} ax_node_store;

/* Never fails; memory is allocated as nodes are created. The calling thread owns the store. */
AX_API void ax_node_store_init(ax_node_store *s, const ax_node_store_desc *desc);

/* Destroys every node (calling components' on_remove) and frees all memory. on_slot_freed is not
 * called. */
AX_API void ax_node_store_deinit(ax_node_store *s);

/* The calling thread becomes the store's owner. The previous owner must not be using it. */
AX_API void ax_node_store_bind_thread(ax_node_store *s);

AX_API bool ax_node_store_on_owner_thread(const ax_node_store *s);

/* ---- Nodes ----------------------------------------------------------------------------------- */

/* Internal slots mode: creates a node with default values, without parent. *out is null on
 * failure. Errors: AX_ERR_OUT_OF_MEMORY (also when every slot is used). */
AX_API ax_result ax_node_store_create(ax_node_store *s, ax_node_kind kind, ax_node *out);

/* External slots mode: creates a node at a handle reserved elsewhere, adopting its generation.
 * Errors: AX_ERR_INVALID_HANDLE (null, slot live or retired, generation lower than the slot's
 * recorded one), AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_node_store_create_at(ax_node_store *s, ax_node node, ax_node_kind kind);

/* Destroys the node and its whole subtree, detaching it from its parent first. */
AX_API ax_result ax_node_store_destroy(ax_node_store *s, ax_node node);

AX_API bool ax_node_store_is_live(const ax_node_store *s, ax_node node);

/* Number of live nodes (= rows). */
static inline uint32_t ax_node_store_count(const ax_node_store *s)
{
    return s->row_count;
}

/* The node's current row, or AX_NODE_NONE for a stale, null or unknown handle. Rows change when
 * nodes are destroyed or the store compacts; look them up again after either. */
static inline uint32_t ax_node_store_row(const ax_node_store *s, ax_node node)
{
    uint32_t slot = ax_handle_slot(node.h);
    uint32_t gen = ax_handle_generation(node.h);
    if (gen == 0 || slot >= s->slot_count || s->slots[slot].generation != gen) {
        return AX_NODE_NONE;
    }
    return s->slots[slot].row >= AX_NODE_SLOT_RETIRED ? AX_NODE_NONE : s->slots[slot].row;
}

/* The handle of the live node in a slot (null when AX_NODE_NONE). */
static inline ax_node ax_node_store_handle(const ax_node_store *s, uint32_t slot)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (slot != AX_NODE_NONE) {
        n.h = ax_handle_make(slot, s->slots[slot].generation);
    }
    return n;
}

/* Links of a live node by row. */
static inline const ax_node_links *ax_node_store_links(const ax_node_store *s, uint32_t row)
{
    return &s->links[row];
}

/* Parent and children as handles (null when absent or when node is not live). */
AX_API ax_node ax_node_store_parent(const ax_node_store *s, ax_node node);
AX_API ax_node ax_node_store_first_child(const ax_node_store *s, ax_node node);
AX_API ax_node ax_node_store_last_child(const ax_node_store *s, ax_node node);
AX_API ax_node ax_node_store_next_sibling(const ax_node_store *s, ax_node node);
AX_API ax_node ax_node_store_prev_sibling(const ax_node_store *s, ax_node node);

/* ---- Hierarchy ------------------------------------------------------------------------------- */

/*
 * Makes child the last child of parent. A child that already has a parent is moved (reparented,
 * or moved to the end within the same parent). Errors: AX_ERR_INVALID_HANDLE, and
 * AX_ERR_INVALID_ARGUMENT when parent is child or one of its descendants (a cycle). On error
 * nothing changes.
 */
AX_API ax_result ax_node_store_append_child(ax_node_store *s, ax_node parent, ax_node child);

/*
 * Inserts child before `before`, which must be a child of parent other than child itself; a null
 * `before` appends. Moves child if it already has a parent. Errors as append_child, plus
 * AX_ERR_INVALID_ARGUMENT when before is not a child of parent.
 */
AX_API ax_result ax_node_store_insert_before(ax_node_store *s, ax_node parent, ax_node child,
                                             ax_node before);

/* Detaches the node from its parent (keeps its subtree). No-op for a root. */
AX_API ax_result ax_node_store_detach(ax_node_store *s, ax_node node);

/* True when ancestor is node or one of node's ancestors. False for handles that are not live. */
AX_API bool ax_node_store_is_ancestor_or_self(const ax_node_store *s, ax_node ancestor,
                                              ax_node node);

/*
 * Reorders the rows into pre-order: each root (a node without parent), in current row order,
 * followed by its subtree depth-first. Handles, links and components are unchanged. Needs scratch
 * memory: AX_ERR_OUT_OF_MEMORY leaves the store unchanged.
 */
AX_API ax_result ax_node_store_compact(ax_node_store *s);

/* ---- Side components ------------------------------------------------------------------------- */

/* Errors: AX_ERR_INVALID_ARGUMENT (bad size or align), AX_ERR_UNSUPPORTED when
 * AX_NODE_MAX_COMPONENT_TYPES are already registered. */
AX_API ax_result ax_node_store_register_component(ax_node_store *s, const ax_component_desc *desc,
                                                  ax_component_type *out_type);

/*
 * Adds a zeroed component to the node, or returns the one it has. *out_element (may be NULL) is
 * valid until the next add or remove of this component type (elements move). Errors:
 * AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (unknown type), AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_node_store_add_component(ax_node_store *s, ax_node node, ax_component_type type,
                                             void **out_element);

/* The node's component, or NULL (no such component, stale handle or unknown type). */
AX_API void *ax_node_store_get_component(const ax_node_store *s, ax_node node,
                                         ax_component_type type);

/* Removes the component (calling on_remove). AX_ERR_INVALID_ARGUMENT when the node has none. */
AX_API ax_result ax_node_store_remove_component(ax_node_store *s, ax_node node,
                                                ax_component_type type);

AX_EXTERN_C_END

#endif /* AX_CORE_NODE_STORE_H */
