/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal structures shared by the platform module's common code and its backends.
 *
 * Ownership. An ax_app_impl and its window table belong to the app's main thread. Other threads
 * only (a) validate handles through the atomic `handle` fields, (b) take snapshots under
 * `info_lock`, (c) publish or read through the triple buffers, and (d) hand work to the main
 * thread with ax__app_call. Window entries are reserved when the app is created and never freed
 * before ax_app_destroy, so a stale handle can never reach freed memory.
 */
#ifndef AX_PLATFORM_INTERNAL_H
#define AX_PLATFORM_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_platform.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_thread.h"
#include "axion/platform/ax_hit_regions.h"
#include "axion/platform/ax_present.h"
#include "axion/platform/ax_surface_native.h"
#include "event_queue.h"
#include "frame_clock.h"
#include "triple_buffer.h"

typedef struct ax_app_impl ax_app_impl;
typedef struct ax_window_impl ax_window_impl;

/* The latest configure, as the main thread hands it to the presenter. */
typedef struct ax_surface_config {
    uint32_t serial;     /* latest configure serial (re-sent unchanged by scale-only updates) */
    float logical_width; /* logical size to draw at */
    float logical_height;
    float scale;
    uint32_t pixel_width; /* logical size times scale, rounded */
    uint32_t pixel_height;
    uint64_t time_ns; /* when that configure arrived */
} ax_surface_config;

typedef struct ax_hit_map {
    uint64_t window_handle; /* the window it was published for (stale maps are ignored) */
    uint32_t count;
    ax_hit_region regions[AX_HIT_REGIONS_MAX];
} ax_hit_map;

/* What ax_window_get_info / ax_surface_get_info report; written by the main thread. */
typedef struct ax_window_snapshot {
    float width, height, scale;
    uint32_t pixel_width, pixel_height;
    bool server_decorated;
    bool focused;
    bool occluded;
    bool configured;
} ax_window_snapshot;

struct ax_window_impl {
    ax_app_impl *app;
    uint32_t index;
    ax_atomic_u64 handle; /* raw handle while open, 0 when free; validates handles anywhere */

    /* main thread */
    bool open;
    uint32_t flags; /* AX_WINDOW_* */
    uint32_t decorations;
    void *backend; /* backend-specific state */

    ax_mutex info_lock;
    ax_window_snapshot info;

    /* hit-test map: one publisher thread, the main thread reads */
    ax_triple_buffer hit_tb;
    ax_hit_map hit[3];

    /* configure handoff: main thread publishes, presenter latches */
    ax_triple_buffer config_tb;
    ax_surface_config config[3];
    ax_atomic_u32 commit_requested; /* main thread changed commit-time state */

    /* presenter only */
    uint32_t presenter_acked_serial;
    bool presenter_has_config;
    ax_surface_config presenter_config;
    ax_frame_clock clock;
    uint64_t presenter_work_seen_ns; /* when a configure/commit request was first seen waiting */
    ax_atomic_u32 presenting;        /* debug aid: a presenter is between begin and prepare */
};

/* A command handed to the main thread by ax__app_call. Lives on the caller's stack. */
typedef struct ax_command {
    ax_result (*fn)(void *ctx);
    void *ctx;
    struct ax_command *next;
    ax_atomic_u32 done;
    ax_result result;
    char message[AX_ERROR_MESSAGE_CAPACITY];
} ax_command;

/* Backend interface. Everything runs on the main thread unless noted. */
typedef struct ax_backend_ops {
    ax_backend kind;
    ax_result (*app_init)(ax_app_impl *app, const ax_app_desc *desc);
    void (*app_shutdown)(ax_app_impl *app);
    /* One pump iteration: wait up to timeout_ns for events or a wake, dispatch, and call
     * ax__app_after_dispatch. UINT64_MAX waits indefinitely. */
    ax_result (*pump)(ax_app_impl *app, uint64_t timeout_ns);
    void (*wake)(ax_app_impl *app); /* any thread: makes pump return soon */

    ax_result (*window_create)(ax_app_impl *app, ax_window_impl *w, const ax_window_desc *desc);
    void (*window_destroy)(ax_app_impl *app, ax_window_impl *w);
    ax_result (*window_set_title)(ax_window_impl *w, const char *title);
    ax_result (*window_set_min_size)(ax_window_impl *w, float width, float height);

    /* any thread */
    void (*surface_native)(ax_window_impl *w, ax_surface_native *out);
    /* presenter thread */
    ax_result (*surface_prepare_commit)(ax_window_impl *w, const ax_frame_target *frame);
    void (*surface_dispatch)(ax_window_impl *w);      /* dispatch the presenter's queue */
    bool (*surface_frame_due)(ax_window_impl *w);     /* the frame callback fired; clears it */
    bool (*surface_frame_pending)(ax_window_impl *w); /* a frame callback is outstanding */
} ax_backend_ops;

struct ax_app_impl {
    ax_allocator allocator;
    uint32_t slot;
    uint32_t generation;
    uint64_t main_thread; /* ax_thread_current_id() of the creator */
    const ax_backend_ops *ops;
    void *backend;
    uint64_t caps;
    char *app_id; /* NUL-terminated copy */

    ax_event_queue events;
    ax_window_impl *windows;
    uint32_t max_windows;

    /* commands from other threads: a lock-free stack, completions signalled under cmd_lock */
    ax_atomic_ptr commands;
    ax_mutex cmd_lock;
    ax_cond cmd_cond;
    ax_atomic_u32 quit;

    /* presenters wait on this; the main thread bumps wake_gen after reading events, publishing a
     * configure or requesting a commit */
    ax_mutex wake_lock;
    ax_cond wake_cond;
    ax_atomic_u64 wake_gen;
    ax_atomic_u32 presenters_waiting;
};

/* ---- common helpers ---- */

/* Handle lookup; NULL for invalid or destroyed handles. Any thread. */
ax_app_impl *ax__app_from_handle(ax_app app);
ax_app_impl *ax__app_from_slot(uint32_t slot);
ax_window_impl *ax__window_from_handle(uint64_t raw);

/* True on the app's main thread. */
bool ax__app_on_main(const ax_app_impl *app);

/* Runs fn(ctx) on the main thread and returns its result: directly when called there, otherwise
 * handed over and waited for (the main thread must be pumping). Error details recorded by fn are
 * re-recorded on the calling thread. Any thread. */
ax_result ax__app_call(ax_app_impl *app, ax_result (*fn)(void *ctx), void *ctx);

/* Destroys an open window. Main thread. */
ax_result ax__window_destroy_on_main(ax_window_impl *w);

/* Raw window handle layout: slot = app slot << 16 | window index; generation from a global
 * counter, so handles from a destroyed app never match a later app's windows. */
#define AX_WINDOW_INDEX_BITS 16u

/* Runs commands handed over by other threads. Main thread. */
void ax__app_run_commands(ax_app_impl *app);

/* Wakes every presenter waiting in ax_surface_wait_frame. Main thread (any thread is safe). */
void ax__app_wake_presenters(ax_app_impl *app);

/* Publishes a window's latest configure for its presenter, updates the info snapshot and queues
 * the matching events. Main thread. */
void ax__window_publish_config(ax_window_impl *w, const ax_surface_config *config,
                               bool size_changed, bool scale_changed);

/* Asks the presenter to commit commit-time state (minimum size, ...). Main thread. */
void ax__window_request_commit(ax_window_impl *w);

/* Updates focus / occlusion / decoration state and queues events for changes. Main thread. */
void ax__window_set_focus(ax_window_impl *w, bool focused);
void ax__window_set_occluded(ax_window_impl *w, bool occluded);
void ax__window_set_server_decorated(ax_window_impl *w, bool server_decorated);
void ax__window_close_requested(ax_window_impl *w);

/* The newest hit map published for w (main thread; NULL if none or stale). */
const ax_hit_map *ax__window_hit_map(ax_window_impl *w);

uint32_t ax__pixels(float logical, float scale);

/* Backends. */
extern const ax_backend_ops ax_wayland_backend;

#endif /* AX_PLATFORM_INTERNAL_H */
