/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK of the runtime: surfaces, the presenter interface, frame requests and the scene
 * stage, for the platform integration, tests and examples.
 *
 * A surface is a root on something that presents: a window (the platform presenter draws with a
 * renderer), or a virtual surface in tests. Each has a Render IR mailbox the scene writes and the
 * render side reads (decision 52).
 *
 * Who calls what:
 *   app thread     ax_rt_create / destroy, ax_rt_add_surface / remove, ax_host_frame
 *   render side    ax_rt_request_frame (the next frame's time and size), ax_rt_snapshot_presented
 *                  (a snapshot reached the screen), ax_rt_surface_mailbox
 *   scene side     ax_rt_scene_step (one pass: commands, due ticks, presentation), visibility
 * In threaded mode the render side is the render thread and the scene side the scene thread; in
 * single-threaded mode both are the caller of ax_host_frame.
 *
 * A tick, for a surface whose request came with predicted present time T (decision 77):
 *   1. T on the world's clock (ax_world_time_from_monotonic), never before the surface's last T;
 *   2. ax_world_apply_pending(world, T - refresh): transactions committed after the frame's
 *      latch point wait for the next frame. The queue is the world's: a tick of one surface
 *      applies transactions that the other surfaces see at their next tick;
 *   3. the scene driver, if any (examples only, until animations);
 *   4. the root's viewport, ax_world_position;
 *   5. ax_world_record_collect, and if the root's content changed, ax_world_record_root into the
 *      surface's mailbox, published;
 *   6. ax_world_publish_mirror (after layout, decision 69);
 *   7. ax_scene_clear_dirty.
 */
#ifndef AX_RUNTIME_SDK_H
#define AX_RUNTIME_SDK_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_runtime.h"
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_jobs.h>
#include <axion/render/ax_ir_mailbox.h>

AX_EXTERN_C_BEGIN

#define AX_RUNTIME_MAX_SURFACES 32u

/* What the scene should record next for a surface. */
typedef struct ax_rt_request {
    float logical_width, logical_height;
    float scale;              /* informational: the scene records in logical units */
    uint64_t time_ns;         /* predicted present time */
    uint64_t refresh_ns;      /* refresh interval: the frame's deadline is time_ns - refresh_ns */
    bool time_is_world_clock; /* time_ns is on the world's clock already (else monotonic) */
    bool resize;              /* a configure waits for a snapshot of this size: record it now */
} ax_rt_request;

/*
 * The render side of surfaces. The platform presenter draws with a renderer and the frame step
 * of decision 57; tests use fakes.
 */
typedef struct ax_rt_presenter_vtbl {
    uint32_t struct_size; /* sizeof(ax_rt_presenter_vtbl) */
    /* A surface was added (attach its native surface) or removed. Render side. */
    ax_result (*attach)(void *self, ax_runtime rt, uint32_t surface, uint64_t native_surface,
                        uint64_t window);
    void (*detach)(void *self, uint32_t surface);
    /* Threaded mode: waits up to timeout_ns until at least one of the surfaces should draw;
     * due[i] is set for surfaces[i]. May be NULL in single-threaded mode. */
    ax_result (*wait)(void *self, const uint32_t *surfaces, uint32_t count, uint64_t timeout_ns,
                      bool *due);
    /* One frame for the surface: present the newest snapshot of its mailbox (asking for a new
     * size with ax_rt_request_frame when a configure needs one), report what reached the screen
     * (ax_rt_snapshot_presented), and ask for the next frame (ax_rt_request_frame). */
    ax_result (*frame)(void *self, uint32_t surface);
    /* The render side stops (after detaching every surface): free what it made. May be NULL. */
    void (*destroy)(void *self);
} ax_rt_presenter_vtbl;

/* Called by the scene before positioning at each tick of a surface (examples only: it moves
 * nodes until implicit animations exist, and goes away with them). Scene side. */
typedef void (*ax_rt_scene_driver)(void *user, ax_world world, ax_node root,
                                   uint64_t frame_time_ns);

typedef struct ax_rt_desc {
    ax_world world;
    ax_runtime_mode mode;
    const ax_rt_presenter_vtbl *presenter; /* NULL: snapshots are recorded, nothing presents */
    void *presenter_self;
    ax_rt_scene_driver driver; /* NULL: none */
    void *driver_user;
    /* Threaded mode, platform integration: called on the scene thread at each pass to forward
     * the windowing system's events (ax_world_push_event). NULL: none. */
    void (*pump_events)(void *user, ax_runtime rt);
    void *pump_user;
    /* Called by ax_runtime_destroy on the app thread once the threads stopped. NULL: none. */
    void (*cleanup)(void *user);
    void *cleanup_user;
    /* Threaded mode: how long the render thread waits for frames at a time (0: 50 ms). */
    uint64_t render_wait_ns;
} ax_rt_desc;

/* As ax_runtime_create, with the internal options. thread: app */
AX_API ax_result ax_rt_create(const ax_rt_desc *desc, ax_runtime *out_runtime);

typedef struct ax_rt_surface_desc {
    float width, height, scale; /* single-threaded mode: the size host frames record at */
    uint64_t refresh_ns;        /* single-threaded mode: the host's frame interval (0: none) */
    uint64_t native_surface;    /* the platform's ax_surface handle value, or 0 */
    uint64_t window;            /* the platform's ax_window handle value, or 0 */
} ax_rt_surface_desc;

/*
 * Attaches a root (a node the app thread created and committed, without parent) to a new
 * surface. The scene picks it up at its next pass; until the root's transaction applies the
 * surface records nothing.
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (null root, or a root already attached),
 * AX_ERR_OUT_OF_MEMORY (AX_RUNTIME_MAX_SURFACES surfaces). thread: app
 */
AX_API ax_result ax_rt_add_surface(ax_runtime rt, ax_node root, const ax_rt_surface_desc *desc,
                                   uint32_t *out_surface);

/* Detaches the surface. In threaded mode it waits (bounded by a second) until the scene and the
 * render threads have let it go; the root node stays. thread: app */
AX_API ax_result ax_rt_remove_surface(ax_runtime rt, uint32_t surface);

/* The surface of a root, or UINT32_MAX. thread: app */
AX_API uint32_t ax_rt_surface_of_root(ax_runtime rt, ax_node root);

/* Single-threaded mode: the size later host frames record at. thread: app */
AX_API void ax_rt_set_surface_size(ax_runtime rt, uint32_t surface, float width, float height,
                                   float scale);

/* The surface's mailbox: the scene writes it, the render side reads it. */
AX_API ax_ir_mailbox *ax_rt_surface_mailbox(ax_runtime rt, uint32_t surface);

/* The handles the surface was added with. thread: any */
AX_API uint64_t ax_rt_surface_native(ax_runtime rt, uint32_t surface);
AX_API uint64_t ax_rt_surface_window(ax_runtime rt, uint32_t surface);
AX_API ax_node ax_rt_surface_root(ax_runtime rt, uint32_t surface);

/* Render side: the newest request replaces an older one not yet taken (mailbox style), and wakes
 * the scene. In single-threaded mode only resize requests are kept: host frames set the time. */
AX_API void ax_rt_request_frame(ax_runtime rt, uint32_t surface, const ax_rt_request *request);

/* Render side: the snapshot with this mailbox sequence (or a later one) reached the screen at
 * present_ns (monotonic clock, or the world's when world_clock). Values only move forward. */
AX_API void ax_rt_snapshot_presented(ax_runtime rt, uint32_t surface, uint64_t ir_sequence,
                                     uint64_t present_ns, bool world_clock);

/* Scene side: a hidden surface does not hold ax_txn_on_presented back (decision 75). */
AX_API void ax_rt_set_surface_visible(ax_runtime rt, uint32_t surface, bool visible);

/* Scene side: one pass (commands, a tick for each surface with a new request, presentation
 * reports). Returns how many ticks ran. */
AX_API uint32_t ax_rt_scene_step(ax_runtime rt);

/* The runtime's job system: inline in single-threaded mode. thread: any */
AX_API ax_job_system *ax_rt_jobs(ax_runtime rt);

typedef struct ax_rt_counters {
    uint64_t ticks;           /* surface ticks */
    uint64_t resize_ticks;    /* of those, asked for by a configure (the size handshake) */
    uint64_t snapshots;       /* published */
    uint64_t applied;         /* transactions applied or rejected by ticks */
    uint64_t presented_until; /* the last presentation watermark reported */
} ax_rt_counters;

/* Scene side, or any thread once the scene thread stopped. */
AX_API void ax_rt_get_counters(ax_runtime rt, ax_rt_counters *out);

AX_EXTERN_C_END

#endif /* AX_RUNTIME_SDK_H */
