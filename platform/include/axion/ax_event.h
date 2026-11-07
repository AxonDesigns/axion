/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: events from the windowing system.
 *
 * The main thread never runs app code: it queues events, and a consumer thread (normally the app
 * thread) takes them with ax_app_poll_event or ax_app_wait_event.
 *
 * Delivery guarantees:
 *   - RESIZED and SCALE_CHANGED are coalesced per window: a slow consumer sees the latest size
 *     and scale, not every intermediate one.
 *   - CLOSE_REQUESTED is never lost. Repeated requests while one is still undelivered are
 *     delivered once.
 *   - FOCUS and OCCLUSION are never lost: each change is delivered in order. Only if the queue
 *     is full are they held back, and then the latest state per window is delivered once there
 *     is room.
 */
#ifndef AX_EVENT_H
#define AX_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "ax_app.h"
#include "ax_window.h"
#include <axion/ax_config.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

typedef enum ax_event_type {
    AX_EVENT_NONE = 0,            /* the queue was empty */
    AX_EVENT_CLOSE_REQUESTED = 1, /* the user asked to close; ignore it to cancel */
    AX_EVENT_RESIZED = 2,         /* the system configured a new size (and scale) */
    AX_EVENT_SCALE_CHANGED = 3,   /* device pixels per logical unit changed */
    AX_EVENT_FOCUS = 4,           /* the window gained or lost focus */
    AX_EVENT_OCCLUSION = 5        /* the window became hidden or visible */
} ax_event_type;

typedef struct ax_event {
    uint32_t struct_size; /* set by the caller: sizeof(ax_event) */
    uint32_t type;        /* ax_event_type */
    ax_window window;
    uint64_t time_ns; /* when the main thread received it, on ax_time_monotonic_ns's clock */
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
        uint64_t reserved[8]; /* room for future event types without changing the size */
    } data;
} ax_event;

/* Takes the next event, or sets out_event->type to AX_EVENT_NONE if there is none.
 * thread: consumer */
AX_API ax_result ax_app_poll_event(ax_app app, ax_event *out_event);

/* Like ax_app_poll_event, but waits up to timeout_ns for an event to arrive. thread: consumer */
AX_API ax_result ax_app_wait_event(ax_app app, ax_event *out_event, uint64_t timeout_ns);

AX_EXTERN_C_END

#endif /* AX_EVENT_H */
