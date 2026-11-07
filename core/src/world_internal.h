/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The world's private state, shared by world.c (lifecycle, queues), txn.c (the app thread's
 * recording) and apply.c (the scene thread's side).
 */
#ifndef AX_CORE_WORLD_INTERNAL_H
#define AX_CORE_WORLD_INTERNAL_H

#include <axion/ax_txn.h>
#include <axion/ax_world.h>
#include <axion/base/ax_assert.h>
#include <axion/base/ax_atomic.h>
#include <axion/base/ax_spsc_ring.h>
#include <axion/base/ax_thread.h>
#include <axion/base/ax_triple_buffer.h>
#include <axion/core/ax_scene.h>
#include <axion/core/ax_world_sdk.h>

#define AX_TXN_MESSAGE_SIZE 192u

/* A node of the transaction queue. */
typedef struct txn_link {
    ax_atomic_ptr next;
} txn_link;

struct ax_txn {
    txn_link link; /* first member: a txn_link * is an ax_txn * except for the stub */
    struct world_impl *world;

    /* Recording (app thread). */
    unsigned char *buf;
    uint32_t size;
    uint32_t capacity;
    uint32_t command_count;
    uint32_t create_count;
    bool animated;
    bool open;
    ax_result error; /* sticky recording error */
    uint32_t error_command;
    char error_message[AX_TXN_MESSAGE_SIZE];

    /* Set at commit. */
    uint64_t seq;
    uint64_t commit_time;

    /* Set by the scene thread (ax_world_finish_txn), read by the app thread once returned. */
    ax_result result;

    struct ax_txn *next_free; /* app thread's free list */
    struct ax_txn *next_all;  /* every object of the world, for teardown */
};

/* One node in a mirror copy (ax_mirror.h). Dead or unknown slots are zero. */
typedef struct mirror_node {
    uint64_t handle; /* the live node's handle; 0 when the slot holds none */
    uint64_t parent;
    uint32_t kind;
    uint32_t reserved;
    ax_node_style style;
    ax_node_paint paint;
    ax_node_composite composite;
    ax_node_layout layout;
} mirror_node;

/* A published copy, indexed by slot. */
typedef struct mirror_copy {
    mirror_node *nodes;
    uint32_t count;    /* slots described */
    uint32_t capacity; /* slots allocated */
    uint64_t pub;      /* publication it reflects; 0: never written */
    uint64_t seq;      /* newest transaction it includes */
} mirror_copy;

/* A rejection, as it crosses to the app thread. */
typedef struct txn_failure {
    uint64_t seq;
    ax_result result;
    uint32_t command;
    char message[AX_TXN_MESSAGE_SIZE];
} txn_failure;

typedef struct world_impl {
    ax_allocator allocator;
    uint32_t slot;
    uint32_t generation;
    ax_clock_fn clock;
    void *clock_user;
    uint64_t app_thread;
    uint64_t scene_thread;

    /* ---- App thread ---- */
    uint64_t next_seq;      /* last sequence handed out */
    uint32_t next_new_slot; /* slots never handed out start here */
    ax_node *free_handles;  /* recycled handles, LIFO */
    uint32_t free_handle_count;
    uint32_t free_handle_capacity;
    uint32_t retired_slots; /* slots dropped at their last generation */
    ax_txn *free_txns;
    ax_txn *all_txns;
    txn_link *tail;            /* queue producer end */
    txn_failure last_failure;  /* the record ax_world_poll_txn_error last returned */
    uint32_t failures_dropped; /* app side: counted when the ring was full (scene side) */

    /* ---- Shared ---- */
    txn_link stub;
    ax_spsc_ring recycled; /* scene -> app: ax_node handles to hand out again */
    ax_spsc_ring returned; /* scene -> app: ax_txn * taken and finished */
    ax_spsc_ring failures; /* scene -> app: txn_failure */
    ax_atomic_u64 applied_seq;
    ax_atomic_u32 dropped_failures;

    /* ---- Scene thread ---- */
    txn_link *head; /* queue consumer end: the dummy node */
    ax_scene scene;
    /* Held back when the rings to the app thread were full; flushed on the next scene call. */
    ax_node *recycle_backlog;
    uint32_t recycle_backlog_count;
    uint32_t recycle_backlog_capacity;
    ax_txn **return_backlog;
    uint32_t return_backlog_count;
    uint32_t return_backlog_capacity;
    bool suppress_recycle; /* rolling back: the app thread recycles a rejected txn's creates */
    /* Mirror (mirror.c): three copies behind a triple buffer; the scene writes, the app reads. */
    mirror_copy mirror[3];
    ax_triple_buffer mirror_tb;
    uint32_t mirror_read; /* app thread: the latched copy */
    uint64_t mirror_pub;  /* scene: last publication number */
    uint64_t *changed_at; /* scene: per slot, the publication that will carry its change */
    uint32_t changed_capacity;
    uint64_t changed_all_at; /* scene: a publication that must copy every slot (stamps failed) */
    bool mirror_dirty;       /* scene: something changed since the last publication */
    void *undo;              /* apply.c's undo log, reused between transactions */
    uint32_t undo_capacity;
    ax_node *removed; /* subtrees removed by the transaction being applied */
    uint32_t removed_capacity;
    bool scene_ready;
} world_impl;

world_impl *ax__world_from_handle(ax_world world);

static inline bool ax__world_on_app_thread(const world_impl *w)
{
    return w->app_thread == ax_thread_current_id();
}

static inline bool ax__world_on_scene_thread(const world_impl *w)
{
    return w->scene_thread == ax_thread_current_id();
}

static inline uint64_t ax__world_now(const world_impl *w)
{
    return w->clock(w->clock_user);
}

/* App thread: takes back finished transactions and recycled handles from the scene thread. */
void ax__world_reclaim(world_impl *w);

/* App thread: hands out a node handle (recycled, or a slot never used). Null when every slot has
 * been handed out. */
ax_node ax__world_mint_handle(world_impl *w);

/* App thread: a handle that was never created (aborted, rejected): its slot can be handed out
 * again at the next generation, or is retired at the last one. */
void ax__world_unmint_handle(world_impl *w, ax_node handle);

/* App thread: puts a transaction object back on the free list, recycling the handles it created
 * when it did not apply (`created_nodes_exist` false). */
void ax__txn_release(world_impl *w, ax_txn *t, bool created_nodes_exist);

/* App thread: queues a committed transaction. */
void ax__world_push_txn(world_impl *w, ax_txn *t);

/* Frees a transaction object and its buffer (world teardown). */
void ax__txn_free(world_impl *w, ax_txn *t);

/* Frees apply.c's buffers (world teardown). */
void ax__apply_free(world_impl *w);

/* Scene thread: the slot's state changed; the next publication copies it. Never fails. */
void ax__mirror_touch_slot(world_impl *w, uint32_t slot);

/* Mirror setup and teardown. */
void ax__mirror_init(world_impl *w);
void ax__mirror_free(world_impl *w);

#endif /* AX_CORE_WORLD_INTERNAL_H */
