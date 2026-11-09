/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a window's accessibility adapter (decision 108), the platform side of the
 * accessibility bridge. It exposes a tree to the system's assistive technologies (AT-SPI on Linux
 * through AccessKit; UI Automation, NSAccessibility, Android and UIAccessibility through AccessKit
 * later, a hidden ARIA DOM on the web) and reports what they do.
 *
 * Platform depends only on base, so the tree crosses in this header's own types: the runtime turns
 * core's updates (axion/core/ax_a11y.h, same fields and the same ax_role and AX_ACTION_* numbers,
 * fixed forever in ax_prop.h) into them. An update has AccessKit's shape: the nodes that changed,
 * each complete, and the focus; a node its parent no longer lists is gone with its subtree.
 *
 *     ax_a11y_adapter_desc d = {sizeof d, ...};   // callbacks, the root's id and name
 *     ax_a11y_adapter_create(window, &d, &a);      // AX_ERR_UNSUPPORTED: no adapter here
 *     ... ax_a11y_adapter_update(a, &update) on any one thread at a time ...
 *     ax_a11y_adapter_destroy(a);                  // before the window
 *
 * The callbacks come from the adapter's own threads: they must only queue work (the runtime
 * passes them to ax_world_a11y_activate and ax_world_post_a11y_action) and never block on the
 * thread that calls ax_a11y_adapter_update.
 */
#ifndef AX_PLATFORM_A11Y_ADAPTER_H
#define AX_PLATFORM_A11Y_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_types.h>
#include <axion/ax_window.h>
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

typedef struct ax_a11y_adapter ax_a11y_adapter;

/* ax_a11y_tree_node.flags (the values of core's AX_A11Y_*). */
#define AX_A11Y_TREE_DISABLED  0x01u
#define AX_A11Y_TREE_SELECTED  0x02u
#define AX_A11Y_TREE_FOCUSABLE 0x04u
#define AX_A11Y_TREE_HAS_VALUE 0x08u
#define AX_A11Y_TREE_HAS_MIN   0x10u
#define AX_A11Y_TREE_HAS_MAX   0x20u
#define AX_A11Y_TREE_HAS_STEP  0x40u

/* A node of an update. Pointers are valid during the call. Geometry: bounds in the node's own
 * space and an affine transform (a, b, c, d, tx, ty as CSS matrix()) to its parent's. */
typedef struct ax_a11y_tree_node {
    uint64_t id;
    uint32_t role;  /* ax_role (ax_prop.h) */
    uint32_t flags; /* AX_A11Y_TREE_* */
    uint32_t checked, expanded, actions, level; /* ax_toggled, ax_expanded, AX_ACTION_* bits */
    ax_str name, description, value_text;
    float value, value_min, value_max, value_step;
    float bounds[4];    /* x, y, w, h */
    float transform[6]; /* a, b, c, d, tx, ty */
    const uint64_t *children;
    uint32_t child_count;
    /* TEXT_RUN: its text and characters (positions and widths from the run's left). */
    ax_str text;
    uint32_t char_count;
    const uint8_t *char_lengths;
    const float *char_positions;
    const float *char_widths;
    uint32_t word_count;
    const uint8_t *word_lengths;
} ax_a11y_tree_node;

typedef struct ax_a11y_tree_update {
    uint64_t root;
    uint64_t focus;
    bool full; /* the whole tree */
    const ax_a11y_tree_node *nodes;
    uint32_t count;
} ax_a11y_tree_update;

typedef struct ax_a11y_adapter_desc {
    uint32_t struct_size; /* sizeof(ax_a11y_adapter_desc) */
    /* The tree's root, and the name of the placeholder tree the adapter answers with when an
     * assistive technology activates it before the first update (its root alone). */
    uint64_t root_id;
    const char *root_name; /* copied; may be NULL */
    /* An assistive technology started (active) or stopped reading the tree. */
    void (*activated)(void *user, bool active);
    /* An action on a node: one AX_ACTION_* bit, a value for SET_VALUE. */
    void (*action)(void *user, uint64_t target, uint32_t action, float value);
    void *user;
} ax_a11y_adapter_desc;

/* Whether this build has an adapter for this platform (AccessKit compiled in, decision 108). */
AX_API bool ax_a11y_adapter_available(void);

/* Errors: AX_ERR_UNSUPPORTED (no adapter in this build or for this backend), AX_ERR_ABI_MISMATCH,
 * AX_ERR_INVALID_HANDLE (window), AX_ERR_OUT_OF_MEMORY. thread: any (the window's owner's). */
AX_API ax_result ax_a11y_adapter_create(ax_window window, const ax_a11y_adapter_desc *desc,
                                        ax_a11y_adapter **out);

/* Stops the adapter; no callback runs after it returns. NULL is fine. */
AX_API void ax_a11y_adapter_destroy(ax_a11y_adapter *adapter);

/* Sends an update when an assistive technology reads the tree (otherwise it is dropped: the next
 * activation starts from a full update). One thread at a time. */
AX_API void ax_a11y_adapter_update(ax_a11y_adapter *adapter, const ax_a11y_tree_update *update);

/* The window gained or lost the keyboard focus. */
AX_API void ax_a11y_adapter_set_window_focused(ax_a11y_adapter *adapter, bool focused);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_A11Y_ADAPTER_H */
