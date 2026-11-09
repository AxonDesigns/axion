/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: semantics in the node store and the accessibility tree (decision 108).
 *
 * The semantics component. A node gets it when it first sets a semantic property (AX_PROP_ROLE
 * to AX_PROP_LEVEL, effect SEMANTICS), so a tree without semantics pays nothing. It holds the
 * node's own values and a bit per property it set (`own`): an optional number (VALUE and its
 * range) exists only while its bit is set.
 *
 * The accessibility tree. The scene thread keeps it next to the node tree, for each window root an
 * assistive technology (or a test) reads, and publishes it as updates in AccessKit's shape: the
 * nodes that changed, each complete, the focus and the root. A node missing from its parent's
 * children afterwards is gone with its subtree. Nothing walks the whole tree each frame: the
 * world's change points (transactions, layout's changed frames, destroyed nodes) queue the nodes
 * they touch, and each frame's flush rebuilds only those, sending the ones whose content changed.
 * While no root is active the tree costs a branch per change point and nothing per frame;
 * activation builds the root's tree once.
 *
 *   - Ids: a node's id is its handle; a text leaf's lines are TEXT_RUN children with ids of their
 *     own (bit 63 set).
 *   - Nodes in the tree: every node under the root except HIDDEN ones, DISPLAY none ones and the
 *     subtrees below them. A text leaf's own children are not (layout ignores them too).
 *   - Role: ROLE, or WINDOW for the root, LABEL for a text leaf, GENERIC otherwise.
 *   - Name: LABEL; otherwise a text leaf's text, and for roles named by their content (button,
 *     checkbox, heading, link, menu item, radio, switch, tab) the text of the text leaves below.
 *   - Geometry: target values, as the mirror (decision 108): bounds (0, 0, w, h) in the node's own
 *     space, and a transform to its parent's space, the frame's origin times TRANSFORM about the
 *     frame's center. Nothing is sent while an animation runs between targets.
 *   - Text ranges: one TEXT_RUN per line, with its characters (grapheme clusters) and words.
 *
 * thread: scene, unless a function says otherwise.
 */
#ifndef AX_CORE_A11Y_H
#define AX_CORE_A11Y_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_prop.h"
#include "../ax_world.h"
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_math.h>
#include <axion/base/ax_shared_str.h>

AX_EXTERN_C_BEGIN

struct ax_scene;

/* The bit of a semantic property in ax_node_semantics.own. */
#define AX_SEMANTICS_FIRST        AX_PROP_ROLE
#define AX_SEMANTICS_LAST         AX_PROP_LEVEL
#define AX_SEMANTICS_BIT(prop)    (1u << ((prop) - AX_SEMANTICS_FIRST))
#define AX_IS_SEMANTIC_PROP(prop) ((prop) >= AX_SEMANTICS_FIRST && (prop) <= AX_SEMANTICS_LAST)

typedef struct ax_node_semantics {
    uint32_t own; /* AX_SEMANTICS_BIT of each property the node set */
    uint32_t role;
    uint32_t checked, expanded;
    uint32_t actions;
    uint32_t level;
    /* References the struct holds; NULL is the empty string. */
    const ax_shared_str *label;
    const ax_shared_str *description;
    const ax_shared_str *test_id;
    const ax_shared_str *value_text;
    float value, value_min, value_max, value_step;
    bool disabled, hidden, selected;
} ax_node_semantics;

/* The node's semantics component, or NULL (a node that never set a semantic property). */
AX_API ax_node_semantics *ax_scene_semantics(const struct ax_scene *scene, ax_node node);

/* ---- The tree's updates ---------------------------------------------------------------------- */

/* ax_a11y_node.flags */
#define AX_A11Y_DISABLED  0x01u
#define AX_A11Y_SELECTED  0x02u
#define AX_A11Y_FOCUSABLE 0x04u /* ACTIONS has FOCUS */
#define AX_A11Y_HAS_VALUE 0x08u /* value is set */
#define AX_A11Y_HAS_MIN   0x10u
#define AX_A11Y_HAS_MAX   0x20u
#define AX_A11Y_HAS_STEP  0x40u

/* A text run's id: bit 63, then its leaf's slot and generation, and the line. */
#define AX_A11Y_RUN_BIT (1ull << 63)

/* A node as an update carries it. Every pointer and string is valid during the sink's call. */
typedef struct ax_a11y_node {
    uint64_t id; /* the node's handle, or a text run's id */
    uint32_t role; /* ax_role, resolved: never NONE */
    uint32_t flags; /* AX_A11Y_* */
    uint32_t checked, expanded, actions, level;
    ax_str name, description, value_text, test_id;
    float value, value_min, value_max, value_step;
    ax_rect bounds;       /* in the node's own space */
    ax_affine2 transform; /* the node's space to its parent's (identity for a text run) */
    const uint64_t *children;
    uint32_t child_count;
    /* TEXT_RUN: its line's text and characters; positions and widths from the run's left. */
    ax_str text;
    uint32_t text_start; /* the line's byte offset in its leaf's text */
    uint32_t char_count;
    const uint8_t *char_lengths; /* UTF-8 bytes of each character */
    const float *char_positions;
    const float *char_widths;
    uint32_t word_count;
    const uint8_t *word_lengths; /* characters in each word */
} ax_a11y_node;

typedef struct ax_a11y_update {
    ax_node root;
    uint64_t focus; /* the node with the assistive technology's focus, or the root */
    bool full;      /* the whole tree: the first update after activation */
    const ax_a11y_node *nodes;
    uint32_t count;
} ax_a11y_update;

/* Where a root's updates go: a platform bridge (AccessKit) or the headless host. `update` is
 * called on the scene thread, from ax_world_a11y_flush; it must not call back into the world. */
typedef struct ax_a11y_sink {
    void (*update)(void *user, const ax_a11y_update *update);
    void *user;
} ax_a11y_sink;

/* Window roots with a sink, at most; activations waiting for the scene thread, at most. */
#define AX_A11Y_MAX_ROOTS       64u
#define AX_A11Y_MAX_ACTIVATIONS 16u

/* Sets (or with NULL removes) the sink of a window root's tree. Removing it deactivates the tree.
 * The root need not be live yet (its transaction may not have applied). Errors:
 * AX_ERR_INVALID_ARGUMENT (null root), AX_ERR_OUT_OF_MEMORY (also beyond AX_A11Y_MAX_ROOTS). */
AX_API ax_result ax_world_set_a11y_sink(ax_world world, ax_node root, const ax_a11y_sink *sink);

/*
 * An assistive technology started (or stopped) reading root's tree. Any thread: the scene
 * thread's next flush builds the whole tree (or forgets it), and a frame is asked for
 * (ax_world_request_frame). While no root is active, the tree costs nothing per frame.
 */
AX_API void ax_world_a11y_activate(ax_world world, ax_node root, bool active);

/* An action an assistive technology asks for (AccessKit's action requests). */
typedef struct ax_a11y_action {
    ax_node root;
    uint64_t target; /* a node's handle (a text run's actions go to its leaf) */
    uint32_t action; /* one AX_ACTION_* bit */
    float value;     /* SET_VALUE */
} ax_a11y_action;

/* Queues an action for the scene thread (any thread; adapters call it from their own threads)
 * and asks for a frame. The flush checks it (a live node in the tree that has the action in its
 * ACTIONS, or FOCUS on a focusable node), moves the tree's focus for FOCUS, and passes it to the
 * app thread as an AX_WORLD_EVENT_A11Y_ACTION event. Returns false when the queue (64 actions)
 * is full: the action is dropped and counted. */
#define AX_A11Y_ACTION_QUEUE 64u
AX_API bool ax_world_post_a11y_action(ax_world world, const ax_a11y_action *action);

/* Activations and actions, then the updates of every active root, to their sinks. The runtime
 * calls it once per frame after recording and before the dirty flags are cleared. */
AX_API void ax_world_a11y_flush(ax_world world);

/* Whether any root's tree is active. */
AX_API bool ax_world_a11y_enabled(ax_world world);

typedef struct ax_a11y_counters {
    uint64_t touched;        /* nodes the change points queued */
    uint64_t built;          /* node records computed */
    uint64_t sent;           /* nodes sent in updates */
    uint64_t runs_sent;      /* text runs among them */
    uint64_t updates;        /* updates sent */
    uint64_t full_builds;    /* activations */
    uint64_t actions;        /* actions passed to the app */
    uint64_t actions_dropped; /* the queue was full, or the action did not apply */
} ax_a11y_counters;

AX_API void ax_world_get_a11y_counters(ax_world world, ax_a11y_counters *out, bool reset);

AX_EXTERN_C_END

#endif /* AX_CORE_A11Y_H */
