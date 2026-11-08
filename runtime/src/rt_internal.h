/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The runtime's private state, shared by runtime.c (surfaces, the scene stage, single-threaded
 * frames) and threads.c (the scene and render threads).
 */
#ifndef AX_RUNTIME_RT_INTERNAL_H
#define AX_RUNTIME_RT_INTERNAL_H

#include <axion/base/ax_atomic.h>
#include <axion/base/ax_spsc_ring.h>
#include <axion/base/ax_thread.h>
#include <axion/base/ax_triple_buffer.h>
#include <axion/runtime/ax_runtime_sdk.h>

/* Snapshots whose included transaction each surface remembers, by mailbox sequence. */
#define RT_SNAP_HISTORY 64u

typedef enum rt_cmd_kind { RT_CMD_ADD = 1, RT_CMD_REMOVE = 2 } rt_cmd_kind;

typedef struct rt_cmd {
    uint32_t kind;
    uint32_t surface;
} rt_cmd;

typedef struct rt_surface {
    /* Set by the app thread before the add command is queued; read-only after. */
    ax_node root;
    uint64_t native;
    uint64_t window;
    uint64_t host_refresh_ns;
    ax_ir_mailbox mailbox; /* scene writes, render side reads */

    /* Render side -> scene: the newest frame request. */
    ax_triple_buffer req_tb;
    ax_rt_request req[3];
    /* Render side -> scene: the newest snapshot shown. */
    ax_atomic_u64 shown_ir;
    ax_atomic_u64 shown_ns;
    ax_atomic_u32 shown_world_clock;

    /* Acknowledgements of a removal. */
    ax_atomic_u32 scene_done;
    ax_atomic_u32 render_done;

    /* Scene side. */
    bool scene_live;
    bool visible;
    uint64_t last_time;    /* world clock; never goes backwards */
    uint64_t published_ir; /* mailbox sequence of the last snapshot published */
    uint64_t snap_txn[RT_SNAP_HISTORY];
    uint64_t needs_txn;  /* newest transaction that changed this surface's content */
    uint64_t shown_txn;  /* newest transaction included in a snapshot shown */
    uint64_t shown_time; /* world clock */
    uint64_t seen_ir;    /* last shown_ir taken into account */

    /* Render side. */
    bool render_live;

    /* App thread. */
    bool app_used;
    bool removing; /* the removal commands are queued; a retry only waits again */
    float host_width, host_height, host_scale;
} rt_surface;

typedef struct rt_impl {
    uint32_t slot, generation;
    ax_rt_desc desc;
    ax_allocator allocator;
    ax_job_system *jobs;
    rt_surface *surfaces; /* AX_RUNTIME_MAX_SURFACES */

    ax_spsc_ring scene_cmds;  /* app -> scene */
    ax_spsc_ring render_cmds; /* app -> render */

    /* The scene thread's wait: requests, commands, presentation reports, platform events. */
    ax_mutex scene_lock;
    ax_cond scene_cond;
    ax_atomic_u64 scene_gen;
    ax_atomic_u32 scene_sleeping;
    /* The app thread waiting for removal acknowledgements. */
    ax_mutex ack_lock;
    ax_cond ack_cond;

    /* The render thread's wait while it has no surface. */
    ax_mutex render_lock;
    ax_cond render_cond;
    ax_atomic_u64 render_gen;

    ax_atomic_u32 stop;
    bool threads_running;
    ax_thread scene_thread;
    ax_thread render_thread;
    ax_atomic_u32 scene_bound;

    ax_rt_counters counters; /* scene side */
} rt_impl;

rt_impl *ax__rt_from_handle(ax_runtime rt);

/* Wakes the scene thread (any thread); nothing in single-threaded mode. */
void ax__rt_wake_scene(rt_impl *r);

/* Wakes the render thread when it waits for its first surface (any thread). */
void ax__rt_wake_render(rt_impl *r);

/* App thread: signals a waiter for acknowledgements (any thread). */
void ax__rt_signal_ack(rt_impl *r);

/* Render side: takes the render commands (attach, detach). */
void ax__rt_render_commands(rt_impl *r);

/* threads.c: start and stop the scene and render threads. */
ax_result ax__rt_start_threads(rt_impl *r);
void ax__rt_stop_threads(rt_impl *r);

#endif /* AX_RUNTIME_RT_INTERNAL_H */
