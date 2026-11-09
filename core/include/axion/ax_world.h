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
 *
 * Events. The scene thread sends the app thread what happened (window events, and when
 * transactions reached the screen) through a queue the app thread drains with ax_world_poll_event
 * or ax_world_wait_event.
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

/* Worlds that can exist at once in a process: at least this many (a guaranteed minimum, not a
 * fixed value; the headless host runs one world per instance, so test suites can run many). */
#define AX_MAX_WORLDS 256u

/* desc may be NULL for the defaults. Errors: AX_ERR_ABI_MISMATCH (struct_size too small),
 * AX_ERR_OUT_OF_MEMORY (also when too many worlds exist). thread: any (the caller becomes the
 * world's app and scene thread) */
AX_API ax_result ax_world_create(const ax_world_desc *desc, ax_world *out_world);

/* The system's reduce-motion setting (decision 94): while on, layout and transform changes take
 * effect at once and other animations become a short linear fade; running animations finish
 * their course. Runtimes set it from the platform; apps may override it. thread: any */
AX_API void ax_world_set_reduce_motion(ax_world world, bool reduce);

/* Destroys the world, its tree and every transaction not applied yet. No other thread may be
 * using it. thread: app */
AX_API ax_result ax_world_destroy(ax_world world);

/* The world's clock, in nanoseconds. thread: any */
AX_API uint64_t ax_world_now(ax_world world);

/* ---- Events (scene thread -> app thread) ----------------------------------------------------- */

/*
 * What the scene thread tells the app thread, in order: the windowing system's events for every
 * window of the app (the scene thread consumes the platform's events and forwards them), with the
 * window's root, or 0 for a window the runtime does not have, and times on the world's clock.
 * Values below 0x100 match the platform's ax_event_type.
 * Delivery: nothing is dropped (resizes are already coalesced per window by the platform).
 */
typedef enum ax_world_event_type {
    AX_WORLD_EVENT_NONE = 0,            /* nothing was waiting */
    AX_WORLD_EVENT_CLOSE_REQUESTED = 1, /* the user asked to close the window */
    AX_WORLD_EVENT_RESIZED = 2,         /* the window has a new size (and scale) */
    AX_WORLD_EVENT_SCALE_CHANGED = 3,
    AX_WORLD_EVENT_FOCUS = 4,
    AX_WORLD_EVENT_OCCLUSION = 5,
    /* From 0x100: events of the scene itself, not the platform's (whose values stay below). */
    /* An assistive technology asked for an action on a node (decision 108); window is 0. */
    AX_WORLD_EVENT_A11Y_ACTION = 0x100
} ax_world_event_type;

typedef struct ax_world_event {
    uint32_t struct_size; /* set by the caller: sizeof(ax_world_event) */
    uint32_t type;        /* ax_world_event_type */
    uint64_t window;      /* the window's handle value (platform's ax_window) */
    uint64_t root;        /* the window's root node (ax_node.h); 0: the runtime does not have it */
    uint64_t time_ns;     /* world clock */
    union {
        struct {
            float width, height; /* logical units */
            uint32_t pixel_width, pixel_height;
            float scale;
        } resized;
        struct {
            float scale;
        } scale_changed;
        struct {
            bool focused;
        } focus;
        struct {
            bool occluded;
        } occlusion;
        struct {
            uint64_t node;   /* the node's handle */
            uint32_t action; /* one AX_ACTION_* bit (ax_prop.h) */
            float value;     /* SET_VALUE */
        } a11y_action;
        uint64_t reserved[8];
    } data;
} ax_world_event;

/*
 * Takes the next event (out->type AX_WORLD_EVENT_NONE when there is none). First runs, in sequence
 * order, the ax_txn_on_presented callbacks that are due (ax_txn.h).
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_ABI_MISMATCH (struct_size). thread: app
 */
AX_API ax_result ax_world_poll_event(ax_world world, ax_world_event *out);

/*
 * As ax_world_poll_event, waiting up to timeout_ns until an event arrives or a callback is due.
 * Returns with out->type AX_WORLD_EVENT_NONE when callbacks ran but no event came, or on timeout.
 * Without threads it does not wait. thread: app
 */
AX_API ax_result ax_world_wait_event(ax_world world, ax_world_event *out, uint64_t timeout_ns);

AX_EXTERN_C_END

#endif /* AX_WORLD_H */
