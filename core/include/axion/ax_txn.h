/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: transactions. The app thread never mutates the tree: it records changes in a
 * transaction and commits it, and the scene thread applies the whole transaction atomically at
 * a frame boundary, so the screen never shows a half-applied change and app code never waits for
 * layout.
 *
 *     ax_txn *t = ax_txn_begin(world);
 *     ax_node card = ax_txn_create(t, AX_NODE_VIEW);
 *     ax_rgba background = ax_rgba_from_srgb8(0x20, 0x24, 0x30, 255);
 *     ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 16.0f);
 *     ax_txn_set_color(t, card, AX_PROP_BACKGROUND_COLOR, background);
 *     ax_txn_append_child(t, list, card);
 *     uint64_t seq;
 *     ax_txn_commit(t, &seq);  // queued; applied at the next frame boundary
 *
 * Handles. ax_txn_create returns the new node's handle at once, without waiting for the scene:
 * it can be used in the same transaction and in later ones. If the transaction is aborted or
 * rejected, the handle stays stale forever.
 *
 * Errors while recording (a null node, a value of the wrong type or out of range for a core
 * property, out of memory) are sticky: the first one is kept, later calls do nothing, and
 * ax_txn_commit returns it and discards the transaction. Errors the scene finds when applying
 * (a stale node, a cycle, an unknown property) reject the whole transaction, never part of it;
 * the app thread reads them with ax_world_poll_txn_error.
 *
 * Every function here runs on the world's app thread. A transaction belongs to the thread that
 * began it, ends with ax_txn_commit or ax_txn_abort, and must not be used afterwards.
 */
#ifndef AX_TXN_H
#define AX_TXN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ax_node.h"
#include "ax_prop.h"
#include "ax_world.h"
#include <axion/ax_config.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

typedef struct ax_txn ax_txn;

/* ---- Transitions ----------------------------------------------------------------------------- */

/*
 * How a property animates to a new value (decision 94). When a transaction changes an animatable
 * property, the scene looks up the transition: the node's own (ax_txn_set_transition), then the
 * world's for that property (ax_txn_set_default_transition), then the built-in default of its
 * effect: a spring (0.4 s, no bounce) for layout (the frame), transform and opacity, a short ease
 * (0.2 s) for colors and other paint properties. Layout properties animate through the frame
 * layout computes: a transition set on any of them, or on AX_PROP_LAYOUT, is the node's layout
 * transition. A zero-initialized ax_transition with a kind is valid: the other fields' zeros are
 * defaults.
 */
typedef enum ax_transition_kind {
    AX_TRANSITION_NONE = 1,   /* change at once */
    AX_TRANSITION_TIMING = 2, /* a duration and an easing */
    AX_TRANSITION_SPRING = 3, /* a spring: perceptual (duration, bounce) or physical */
    AX_TRANSITION_DECAY = 4,  /* the remaining distance shrinks exponentially at `rate` */
    AX_TRANSITION_DEFAULT = 5 /* removes an override: the next level of the lookup decides */
} ax_transition_kind;

/* TIMING's easing. */
typedef enum ax_easing {
    AX_EASING_CUBIC_BEZIER = 0, /* curve: CSS cubic-bezier(x1, y1, x2, y2); all 0 is linear */
    AX_EASING_STEPS = 1,        /* CSS steps(steps, step_position) */
    AX_EASING_LINEAR = 2        /* CSS linear() with explicit inputs: stops */
} ax_easing;

typedef enum ax_step_position {
    AX_STEP_JUMP_END = 0, /* CSS jump-end (end) */
    AX_STEP_JUMP_START = 1,
    AX_STEP_JUMP_NONE = 2,
    AX_STEP_JUMP_BOTH = 3
} ax_step_position;

/* A point of a linear() easing: the output at an input of the progress (0..1). */
typedef struct ax_linear_stop {
    float input, output;
} ax_linear_stop;

#define AX_TRANSITION_MAX_STOPS 16u

typedef struct ax_transition {
    uint32_t struct_size; /* sizeof(ax_transition) */
    const void *next;     /* extension chain; none defined yet */
    uint32_t kind;        /* ax_transition_kind */
    float delay;          /* seconds, >= 0 */
    float duration;       /* seconds, >= 0: TIMING's length; SPRING's perceptual duration */
    /* TIMING */
    uint32_t easing;             /* ax_easing */
    float curve[4];              /* CUBIC_BEZIER: x1, y1, x2, y2 (x in 0..1) */
    uint32_t steps;              /* STEPS: at least 1 (2 with JUMP_NONE) */
    uint32_t step_position;      /* ax_step_position */
    const ax_linear_stop *stops; /* LINEAR: 2 to AX_TRANSITION_MAX_STOPS, inputs in 0..1 and */
    uint32_t stop_count;         /*   not decreasing; copied when recorded */
    /* SPRING. Perceptual (stiffness 0): duration > 0 and bounce. Physical: stiffness > 0. */
    float bounce;    /* -1 < bounce < 1; 0 is critically damped, above 0 bounces */
    float stiffness; /* > 0 for a physical spring */
    float damping;   /* >= 0 */
    float mass;      /* > 0; 0 means 1 */
    /* DECAY */
    float rate; /* > 0, per second */
} ax_transition;

/* ---- Colors ---------------------------------------------------------------------------------- */

/* Converts an sRGB-encoded color with straight alpha (CSS-style values, 0..1) to the stored
 * form: linear light, premultiplied. thread: any */
AX_API ax_rgba ax_rgba_from_srgb(float r, float g, float b, float a);

/* As ax_rgba_from_srgb, from 8-bit components (#RRGGBBAA). thread: any */
AX_API ax_rgba ax_rgba_from_srgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* ---- Transactions ---------------------------------------------------------------------------- */

/* Starts a transaction. NULL on failure (stale world, wrong thread, out of memory), with
 * ax_last_error. thread: app */
AX_API ax_txn *ax_txn_begin(ax_world world);

/* Creates a node and returns its handle; null if the transaction has failed. thread: app */
AX_API ax_node ax_txn_create(ax_txn *t, ax_node_kind kind);

/* Set a property. The function must match the property's value type. thread: app */
AX_API void ax_txn_set_f32(ax_txn *t, ax_node node, ax_prop prop, float value);
AX_API void ax_txn_set_dimension(ax_txn *t, ax_node node, ax_prop prop, ax_dimension value);
AX_API void ax_txn_set_vec2(ax_txn *t, ax_node node, ax_prop prop, ax_vec2f value);
AX_API void ax_txn_set_color(ax_txn *t, ax_node node, ax_prop prop, ax_rgba value);
AX_API void ax_txn_set_transform(ax_txn *t, ax_node node, ax_prop prop, ax_transform2d value);
AX_API void ax_txn_set_u32(ax_txn *t, ax_node node, ax_prop prop, uint32_t value);
AX_API void ax_txn_set_bool(ax_txn *t, ax_node node, ax_prop prop, bool value);

/* Sets a string property (AX_PROP_TEXT, AX_PROP_FONT_FAMILY, AX_PROP_FONT_FEATURES): `length`
 * bytes of UTF-8, copied (they need no terminating NUL; NULL with length 0 is the empty string).
 * Invalid UTF-8, more than AX_PROP_MAX_STRING bytes or a font feature list that does not parse
 * is a recording error. thread: app */
AX_API void ax_txn_set_str(ax_txn *t, ax_node node, ax_prop prop, const char *utf8, size_t length);

/* Makes child the last child of parent, moving it if it has a parent. thread: app */
AX_API void ax_txn_append_child(ax_txn *t, ax_node parent, ax_node child);

/* Inserts child before `before` (a child of parent; null appends), moving it if it has a parent.
 * thread: app */
AX_API void ax_txn_insert_before(ax_txn *t, ax_node parent, ax_node child, ax_node before);

/* Destroys the node and its subtree when the transaction applies. Later commands in the same
 * transaction that name a removed node fail as stale. thread: app */
AX_API void ax_txn_remove(ax_txn *t, ax_node node);

/* Whether this transaction's changes animate implicitly (default true). With false, its changes
 * take effect at once and stop the animations of the properties they change; frames that change
 * in a frame where every transaction applied was not animated snap too. thread: app */
AX_API void ax_txn_set_animated(ax_txn *t, bool animated);

/* The node's own transition for one property, kept until set again (AX_TRANSITION_DEFAULT
 * removes it). It applies to changes made from then on, this transaction's included. On a
 * shorthand it applies to every longhand; on a layout property it is the node's layout
 * transition (AX_PROP_LAYOUT). thread: app */
AX_API void ax_txn_set_transition(ax_txn *t, ax_node node, ax_prop prop,
                                  const ax_transition *transition);

/* The world's transition for one property, for nodes without their own (AX_TRANSITION_DEFAULT
 * goes back to the built-in default). thread: app */
AX_API void ax_txn_set_default_transition(ax_txn *t, ax_prop prop, const ax_transition *transition);

/*
 * Queues the transaction for the scene thread and ends it. On success *out_seq (may be NULL) is
 * its sequence number: transactions are numbered from 1 in commit order and applied in that
 * order. On failure (a recording error) nothing is queued and the error is returned, with
 * ax_last_error naming the command. thread: app
 */
AX_API ax_result ax_txn_commit(ax_txn *t, uint64_t *out_seq);

/* Discards the transaction; nodes it created are never created. thread: app */
AX_API void ax_txn_abort(ax_txn *t);

/* ---- Results --------------------------------------------------------------------------------- */

/* A transaction the scene rejected. */
typedef struct ax_txn_error {
    uint64_t seq;
    ax_result result;
    uint32_t command; /* index of the failing command, from 0 */
    ax_str message;   /* valid until the next call on this world from the app thread */
} ax_txn_error;

/*
 * Takes the oldest rejection not read yet. False when there is none. At most
 * AX_TXN_ERROR_QUEUE rejections wait: while the queue is full, newer ones are dropped (the oldest
 * are kept), and `dropped` (may be NULL) says how many since the last call. thread: app
 */
AX_API bool ax_world_poll_txn_error(ax_world world, ax_txn_error *out, uint32_t *dropped);

#define AX_TXN_ERROR_QUEUE 64u

/* Sequence number of the newest transaction the scene has applied or rejected (0: none). thread:
 * app */
AX_API uint64_t ax_world_last_applied_seq(ax_world world);

/* Sequence number of the newest transaction committed (0: none). Every transaction up to it has
 * been applied or rejected once ax_world_last_applied_seq reaches it (the headless host's
 * settle). thread: app */
AX_API uint64_t ax_world_last_committed_seq(ax_world world);

/* ---- When a change is on screen -------------------------------------------------------------- */

typedef enum ax_presented_status {
    AX_PRESENTED = 1,                /* the change is on screen (see ax_txn_on_presented) */
    AX_PRESENTED_REJECTED = 2,       /* the scene rejected the transaction; nothing changed */
    AX_PRESENTED_WORLD_DESTROYED = 3 /* the world was destroyed first */
} ax_presented_status;

/* present_time_ns: for AX_PRESENTED, when the change reached the screen (world clock), 0
 * otherwise. */
typedef void (*ax_presented_fn)(void *user, uint64_t seq, ax_presented_status status,
                                uint64_t present_time_ns);

/*
 * Calls fn once transaction `seq` is on screen, on the app thread, from ax_world_poll_event or
 * ax_world_wait_event (or ax_world_destroy). The callback always comes, exactly once, with a
 * status, and callbacks come in sequence order:
 *   - AX_PRESENTED when every visible window whose content the transaction (or an earlier one)
 *     changed has shown a frame that includes it. A transaction that changes no visible window
 *     is presented when it is applied. A window that closes stops holding it back; a hidden
 *     window does not hold it back, and one that becomes visible again holds back what it has
 *     not shown yet.
 *   - AX_PRESENTED_REJECTED when the scene rejected it (the details are in
 *     ax_world_poll_txn_error).
 *   - AX_PRESENTED_WORLD_DESTROYED when the world is destroyed before either.
 * A transaction already presented or rejected is reported on the next poll. The seq must be one
 * of the last AX_TXN_PRESENTED_WINDOW committed.
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (fn NULL, seq 0, not committed yet, or
 * too old), AX_ERR_OUT_OF_MEMORY, AX_ERR_WRONG_THREAD. thread: app
 */
AX_API ax_result ax_txn_on_presented(ax_world world, uint64_t seq, ax_presented_fn fn, void *user);

#define AX_TXN_PRESENTED_WINDOW 1024u

AX_EXTERN_C_END

#endif /* AX_TXN_H */
