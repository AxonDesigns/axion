/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: the runtime, which runs a world's frame pipeline.
 *
 * Threaded (the default): the runtime starts one scene thread and one render thread. The scene
 * thread applies the world's transactions at each surface's vsync, positions, records the Render
 * IR and publishes the mirror; the render thread presents every surface. The app thread (the
 * world's) only builds transactions and reads events (ax_world_poll_event): it never waits for
 * the other threads, and they never wait for it.
 *
 * Single-threaded: the same stages run in order on the caller's thread, one frame per
 * ax_host_frame call, for hosts that own the loop, deterministic tests and the web without
 * threads. Smoothness then depends on app code staying short.
 *
 *     ax_world world;  ax_world_create(NULL, &world);
 *     ax_runtime rt;   ax_runtime_desc d = {sizeof d}; d.world = world;
 *     ax_runtime_create(&d, &rt);
 *     ax_node root;    ax_runtime_add_window(rt, window, &root);   // ax_runtime_window.h
 *     ... transactions under root ...
 *     ax_runtime_destroy(rt); ax_world_destroy(world);
 */
#ifndef AX_RUNTIME_H
#define AX_RUNTIME_H

#include <stdint.h>

#include <axion/ax_config.h>
#include <axion/ax_handle.h>
#include <axion/ax_node.h>
#include <axion/ax_types.h>
#include <axion/ax_world.h>

AX_EXTERN_C_BEGIN

AX_DECLARE_HANDLE(ax_runtime);

typedef enum ax_runtime_mode {
    AX_RUNTIME_THREADED = 0,       /* scene and render threads */
    AX_RUNTIME_SINGLE_THREADED = 1 /* every stage on the caller's thread (ax_host_frame) */
} ax_runtime_mode;

typedef struct ax_runtime_desc {
    uint32_t struct_size; /* sizeof(ax_runtime_desc) */
    const void *next;     /* extension chain; none defined yet */
    ax_world world;       /* required; the caller must be its app thread */
    uint32_t mode;        /* ax_runtime_mode */
} ax_runtime_desc;

/*
 * Creates the runtime for a world. The world's scene thread role moves to the runtime's scene
 * thread (threaded mode) or stays with the caller (single-threaded).
 * Errors: AX_ERR_ABI_MISMATCH, AX_ERR_INVALID_HANDLE (world), AX_ERR_INVALID_ARGUMENT (mode),
 * AX_ERR_UNSUPPORTED (threaded mode without threads), AX_ERR_OUT_OF_MEMORY. thread: app
 */
AX_API ax_result ax_runtime_create(const ax_runtime_desc *desc, ax_runtime *out_runtime);

/* Stops the runtime's threads and gives the scene role back to the caller. The world stays valid
 * and must be destroyed after. Surfaces still attached are detached. thread: app */
AX_API ax_result ax_runtime_destroy(ax_runtime runtime);

/*
 * Single-threaded mode: runs one frame for the surface whose root is `root`, for a frame expected
 * on screen at target_time_ns (on the world's clock, which a host injects): applies the
 * transactions committed up to the frame's deadline, positions, records and presents.
 * Errors: AX_ERR_INVALID_HANDLE (runtime, or root of no surface), AX_ERR_UNSUPPORTED (threaded
 * mode), and those of the presenter. thread: app
 */
AX_API ax_result ax_host_frame(ax_runtime runtime, ax_node root, uint64_t target_time_ns);

AX_EXTERN_C_END

#endif /* AX_RUNTIME_H */
