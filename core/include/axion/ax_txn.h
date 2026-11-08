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
#include <stdint.h>

#include "ax_node.h"
#include "ax_prop.h"
#include "ax_world.h"
#include <axion/ax_config.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

typedef struct ax_txn ax_txn;

/* ---- Transitions ----------------------------------------------------------------------------- */

/* How a property animates to a new value. Recorded and validated now; it takes effect when
 * animations exist. Fields may still change before the ABI is frozen. */
typedef enum ax_transition_kind {
    AX_TRANSITION_NONE = 1,   /* change at once */
    AX_TRANSITION_TIMING = 2, /* duration and a cubic Bezier curve */
    AX_TRANSITION_SPRING = 3  /* perceptual spring: duration and bounce */
} ax_transition_kind;

typedef struct ax_transition {
    uint32_t struct_size; /* sizeof(ax_transition) */
    const void *next;     /* extension chain; none defined yet */
    uint32_t kind;        /* ax_transition_kind */
    float delay;          /* seconds, >= 0 */
    float duration;       /* seconds, >= 0 (TIMING, SPRING) */
    float curve[4];       /* TIMING: cubic Bezier x1, y1, x2, y2 (x in 0..1) */
    float bounce;         /* SPRING: -1..1 */
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

/* Makes child the last child of parent, moving it if it has a parent. thread: app */
AX_API void ax_txn_append_child(ax_txn *t, ax_node parent, ax_node child);

/* Inserts child before `before` (a child of parent; null appends), moving it if it has a parent.
 * thread: app */
AX_API void ax_txn_insert_before(ax_txn *t, ax_node parent, ax_node child, ax_node before);

/* Destroys the node and its subtree when the transaction applies. Later commands in the same
 * transaction that name a removed node fail as stale. thread: app */
AX_API void ax_txn_remove(ax_txn *t, ax_node node);

/* Whether this transaction's changes animate implicitly (default true). No effect until
 * animations exist. thread: app */
AX_API void ax_txn_set_animated(ax_txn *t, bool animated);

/* Overrides the transition of one property of a node for this change. On a shorthand it applies
 * to every longhand. No effect until animations exist. thread: app */
AX_API void ax_txn_set_transition(ax_txn *t, ax_node node, ax_prop prop,
                                  const ax_transition *transition);

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
