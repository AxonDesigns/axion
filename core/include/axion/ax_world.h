/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: the world, which owns one retained tree and the queues between the app thread and
 * the scene thread.
 *
 * Threads. The app thread builds and commits transactions (ax_txn.h) and reads results; the
 * scene thread applies them. The thread that calls ax_world_create is the world's app thread, and
 * also its scene thread until the scene thread takes over (the frame pipeline does that; tests
 * use the internal SDK). Functions are tagged with the thread they may run on, and debug builds
 * check the tag on entry.
 *
 * Clock. Commit times and apply deadlines come from the world's clock: ax_world_desc.clock, or by
 * default the monotonic clock base uses (nanoseconds, arbitrary origin). A headless host or a
 * test injects a manual clock; the frame pipeline compares deadlines with the vsync's predicted
 * present time on the same clock.
 */
#ifndef AX_WORLD_H
#define AX_WORLD_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_config.h>
#include <axion/ax_handle.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

AX_DECLARE_HANDLE(ax_world);

/* Returns the current time in nanoseconds. Called on the app thread (commit) and the scene thread
 * (apply), so it must be safe to call from both. */
typedef uint64_t (*ax_clock_fn)(void *user);

typedef struct ax_world_desc {
    uint32_t struct_size; /* sizeof(ax_world_desc) */
    const void *next;     /* extension chain; none defined yet */
    /* Used from the app and the scene threads, so it must be thread-safe. NULL: the default. */
    const ax_allocator *allocator;
    ax_clock_fn clock; /* NULL: the monotonic clock */
    void *clock_user;
} ax_world_desc;

/* desc may be NULL for the defaults. Errors: AX_ERR_ABI_MISMATCH (struct_size too small),
 * AX_ERR_OUT_OF_MEMORY (also when too many worlds exist). thread: any (the caller becomes the
 * world's app and scene thread) */
AX_API ax_result ax_world_create(const ax_world_desc *desc, ax_world *out_world);

/* Destroys the world, its tree and every transaction not applied yet. No other thread may be
 * using it. thread: app */
AX_API ax_result ax_world_destroy(ax_world world);

/* The world's clock, in nanoseconds. thread: any */
AX_API uint64_t ax_world_now(ax_world world);

AX_EXTERN_C_END

#endif /* AX_WORLD_H */
