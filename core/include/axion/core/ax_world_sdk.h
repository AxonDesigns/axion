/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK of the world: thread roles, the scene thread's side of the transaction queue, and
 * the encoded form of transactions.
 *
 * Encoding. A transaction is one byte buffer of commands, kept by the transaction object and
 * reused when the object is recycled, so recording allocates nothing once warm. Each command is a
 * header {u16 op, u16 flags, u32 size} followed by its fields, 8-byte aligned; size covers the
 * header and steps to the next command. Values travel inline (ax_value).
 *
 * Queue. Committed transactions go from the app thread to the scene thread through a wait-free
 * single-producer single-consumer linked queue with a stub node, whose nodes are the transaction
 * objects themselves (unbounded, no allocation). The scene thread takes them in commit order. A
 * taken transaction stays the queue's dummy node until the next one is taken, and then goes back
 * to the app thread for reuse.
 */
#ifndef AX_CORE_WORLD_SDK_H
#define AX_CORE_WORLD_SDK_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_txn.h"
#include "../ax_world.h"
#include "ax_anim.h"
#include "ax_prop_registry.h"
#include "ax_scene.h"
#include <axion/base/ax_compiler.h>

AX_EXTERN_C_BEGIN

struct ax_text_system;

/* ---- Threads --------------------------------------------------------------------------------- */

/* The calling thread becomes the world's scene thread (and owns its node store). The previous
 * scene thread must not be using it. */
AX_API void ax_world_bind_scene_thread(ax_world world);

/* The calling thread becomes the world's app thread. The previous one must not be using it. */
AX_API void ax_world_bind_app_thread(ax_world world);

/*
 * Frame requests (decision 95's generic wake). ax_world_request_frame asks for a frame of the
 * windows that show `root` (every window for a null root): new content that comes without a
 * transaction (glyphs rasterized in the background, decision 100; later, decoded images and loaded
 * fonts) or a commit, which calls it with a null root. The world calls its wake hook, which a
 * runtime sets to wake its resting surfaces (NULL removes it, waiting for a call in progress); the
 * woken frame's tick draws what changed. Nothing happens without a hook.
 * thread: any, for both. The hook runs under the world's wake lock: it must be quick and must not
 * call back into the world.
 */
typedef void (*ax_world_wake_fn)(void *user, ax_node root);
AX_API void ax_world_set_wake_hook(ax_world world, ax_world_wake_fn hook, void *user);
AX_API void ax_world_request_frame(ax_world world, ax_node root);

/* The job system the scene's frame stages may use for frame jobs (layout's independent relayout
 * boundaries), or NULL (the default) for one thread. It must outlive its use. thread: scene */
AX_API void ax_world_set_jobs(ax_world world, struct ax_job_system *jobs);

/*
 * The text system text leaves use (fonts, the shaping and glyph caches; decision 99), or NULL to
 * go back to the world's own. Without one, the world makes its own on first use with the bundled
 * fonts only (the headless default). A runtime sets one with the system's fonts. Set it before the
 * first text; it must outlive the world. thread: scene
 */
AX_API void ax_world_set_text_system(ax_world world, struct ax_text_system *text);

/* The text system in use, made on first call when none was set (NULL when out of memory).
 * thread: scene */
AX_API struct ax_text_system *ax_world_text_system(ax_world world);

/* True for a live world. thread: any */
AX_API bool ax_world_is_valid(ax_world world);

/* The scene, for the scene thread (NULL for a stale handle). */
AX_API ax_scene *ax_world_scene(ax_world world);

/* ---- Clock ----------------------------------------------------------------------------------- */

/* True when the world uses the default clock (base's monotonic clock), the platform's. */
AX_API bool ax_world_clock_is_monotonic(ax_world world);

/*
 * A time on base's monotonic clock (ax_time_monotonic_ns: the platform's predicted present times)
 * on the world's clock. The same value with the default clock. With an injected clock, the
 * distance from now is kept: now_world + (monotonic_ns - now_monotonic), at least 0. Callers that
 * need it never to go backwards (a root's frame times) clamp it themselves. thread: any
 */
AX_API uint64_t ax_world_time_from_monotonic(ax_world world, uint64_t monotonic_ns);

/* ---- Encoded commands ------------------------------------------------------------------------ */

typedef enum ax_txn_op {
    AX_TXN_OP_CREATE = 1,
    AX_TXN_OP_SET = 2,
    AX_TXN_OP_INSERT = 3, /* insert_before; append when before is null */
    AX_TXN_OP_REMOVE = 4,
    AX_TXN_OP_TRANSITION = 5,
    AX_TXN_OP_SET_STR = 6 /* a string value, its bytes inline (decision 99) */
} ax_txn_op;

typedef struct ax_txn_cmd {
    uint16_t op;
    uint16_t flags; /* none defined */
    uint32_t size;  /* bytes, header included, multiple of 8 */
} ax_txn_cmd;

typedef struct ax_txn_cmd_create {
    ax_txn_cmd h;
    uint32_t kind;
    uint32_t reserved;
    ax_node node;
} ax_txn_cmd_create;

typedef struct ax_txn_cmd_set {
    ax_txn_cmd h;
    ax_prop prop;
    uint32_t type; /* ax_value_type */
    ax_node node;
    ax_value value;
} ax_txn_cmd_set;

typedef struct ax_txn_cmd_insert {
    ax_txn_cmd h;
    ax_node parent;
    ax_node child;
    ax_node before; /* null: append */
} ax_txn_cmd_insert;

typedef struct ax_txn_cmd_remove {
    ax_txn_cmd h;
    ax_node node;
} ax_txn_cmd_remove;

typedef struct ax_txn_cmd_transition {
    ax_txn_cmd h;
    ax_prop prop; /* a longhand or AX_PROP_LAYOUT: shorthands and layout properties are mapped
                   * when recording */
    uint32_t reserved;
    ax_node node;   /* null: the world's default for prop (ax_txn_set_default_transition) */
    ax_curve curve; /* validated (ax_anim.h) */
} ax_txn_cmd_transition;

/* SET_STR: `length` bytes of UTF-8 follow the fixed part, padded to a multiple of 8 (the
 * command's size is sizeof(ax_txn_cmd_set_str) + length rounded up to 8). */
typedef struct ax_txn_cmd_set_str {
    ax_txn_cmd h;
    ax_prop prop;
    uint32_t length;
    ax_node node;
} ax_txn_cmd_set_str;

static inline const char *ax_txn_cmd_str_bytes(const ax_txn_cmd_set_str *c)
{
    return (const char *)(c + 1);
}

/* A committed transaction, as the scene thread reads it. */
typedef struct ax_txn_info {
    uint64_t seq;
    uint64_t commit_time; /* world clock */
    uint32_t command_count;
    uint32_t create_count;
    bool animated;
    const unsigned char *commands;
    uint32_t size; /* bytes of commands */
} ax_txn_info;

AX_API void ax_txn_get_info(const ax_txn *t, ax_txn_info *out);

/* The command after prev (NULL: the first), or NULL at the end. Assumes well-formed sizes;
 * ax_txn_cmd_valid checks one command against the buffer. */
AX_API const ax_txn_cmd *ax_txn_next_cmd(const ax_txn_info *info, const ax_txn_cmd *prev);

/* True when the command's op is known and its size is the op's size and fits in the buffer. */
AX_API bool ax_txn_cmd_valid(const ax_txn_info *info, const ax_txn_cmd *cmd);

/* ---- The scene thread's side of the queue ---------------------------------------------------- */

/* The oldest committed transaction not taken yet, or NULL. thread: scene */
AX_API const ax_txn *ax_world_peek_txn(ax_world world);

/*
 * Takes the oldest committed transaction (NULL when none). It stays valid until the next take,
 * which releases it to the app thread with the outcome set by ax_world_finish_txn. thread: scene
 */
AX_API ax_txn *ax_world_take_txn(ax_world world);

/*
 * Records the outcome of a taken transaction. On failure, a rejection record (sequence, result,
 * command index, message) goes to the app thread's error queue, and the app thread recycles the
 * handles the transaction created when it gets the object back. Sets the last applied sequence.
 * thread: scene
 */
AX_API void ax_world_finish_txn(ax_world world, ax_txn *t, ax_result result, uint32_t command,
                                const char *message);

/* Returns a node handle whose slot the scene freed, for the app thread to hand out again (its
 * generation is the next one the slot may use). thread: scene */
AX_API void ax_world_recycle_handle(ax_world world, ax_node next_handle);

/*
 * Applies, in order, every committed transaction whose commit time (world clock) is at most
 * deadline_ns, each atomically: all of its commands, or none. A rejected transaction is reported
 * to the app thread (ax_world_poll_txn_error). Later commits wait for the next call. Returns how
 * many transactions were applied or rejected. thread: scene
 */
AX_API uint32_t ax_world_apply_pending(ax_world world, uint64_t deadline_ns);

/* ---- The app thread's mirror (ax_mirror.h) --------------------------------------------------- */

/*
 * Publishes the tree's current state to the app thread's mirror: the slots changed since the copy
 * being written last reflected them. Call it once per frame, after transactions, layout and
 * anything else that changes what the mirror shows, so the app never sees a frame half done.
 * Applying transactions does not publish. Returns false when nothing changed, or when memory for
 * a larger copy was not available (it retries on the next call). thread: scene
 */
AX_API bool ax_world_publish_mirror(ax_world world);

/* Marks a node as changed for the next publication. Transactions mark what they touch; layout
 * calls it for nodes whose frame changed (the mirror's channel for layout results). thread: scene
 */
AX_API void ax_world_touch_node(ax_world world, ax_node node);

/* ---- Events to the app thread (ax_world_poll_event) ------------------------------------------ */

/*
 * Queues an event for the app thread. Never waits; when the queue is full the event waits in a
 * scene-side backlog, sent first on the next call (false only when that backlog is out of
 * memory, and the event is lost). thread: scene
 */
AX_API bool ax_world_push_event(ax_world world, const ax_world_event *event);

/*
 * Every applied transaction up to through_seq is on screen (or changes nothing visible), shown at
 * time_ns (world clock): their ax_txn_on_presented callbacks become due. Values below the last
 * report are ignored; through_seq must not pass the last finished transaction. Rejections are
 * reported by ax_world_finish_txn itself. thread: scene
 */
AX_API void ax_world_report_presented(ax_world world, uint64_t through_seq, uint64_t time_ns);

/* Sends what waits in the scene-side backlog; true when nothing is left. thread: scene */
AX_API bool ax_world_flush_events(ax_world world);

AX_EXTERN_C_END

#endif /* AX_CORE_WORLD_SDK_H */
