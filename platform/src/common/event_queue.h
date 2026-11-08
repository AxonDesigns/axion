/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The platform event queue: a fixed-capacity single-producer/single-consumer ring between the
 * main thread (producer) and the event consumer, plus per-window coalescing state.
 *
 * The producer never blocks and never allocates: all memory is reserved by ax_evq_init.
 *
 * Overflow policy (decision 31):
 *   - RESIZED and SCALE_CHANGED are coalesced per window. The latest geometry lives in a per-window
 *     triple buffer; the ring holds at most one RESIZED and one SCALE_CHANGED marker per window,
 *     and the consumer reads the newest geometry when it takes the marker.
 *   - CLOSE_REQUESTED: at most one marker per window is pending; repeats while one is pending
 *     merge into it.
 *   - FOCUS and OCCLUSION are discrete records, delivered in order.
 *   - When the ring is full nothing is dropped: the event is deferred in producer-only per-window
 *     state (a marker, or the latest focus/occlusion value) and flushed, in window order, as soon
 *     as the consumer frees space; the consumer wakes the producer when it does. While anything
 *     is deferred, new events are deferred too, so ring order is kept.
 *
 * Window slots are reserved up front (max_windows). A record names its window by slot and handle;
 * records for a window closed since are dropped by the consumer.
 */
#ifndef AX_PLATFORM_EVENT_QUEUE_H
#define AX_PLATFORM_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_allocator.h"
#include "axion/ax_event.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_triple_buffer.h"

typedef struct ax_evq_geometry {
    float width, height; /* logical */
    uint32_t pixel_width, pixel_height;
    float scale;
    uint64_t time_ns;
} ax_evq_geometry;

typedef struct ax_evq_record {
    uint32_t type; /* ax_event_type */
    uint32_t window_index;
    uint64_t handle; /* raw ax_window handle */
    uint64_t time_ns;
    uint32_t value; /* focused / occluded */
} ax_evq_record;

/* Producer-only deferred bits. */
#define AX_EVQ_DEFER_RESIZE    1u
#define AX_EVQ_DEFER_SCALE     2u
#define AX_EVQ_DEFER_CLOSE     4u
#define AX_EVQ_DEFER_FOCUS     8u
#define AX_EVQ_DEFER_OCCLUSION 16u

typedef struct ax_evq_window {
    ax_atomic_u64 handle;        /* raw handle of the open window, 0 when the slot is free */
    ax_atomic_u32 resize_marker; /* 1 while a RESIZED marker is queued or deferred */
    ax_atomic_u32 scale_marker;
    ax_atomic_u32 close_marker;
    ax_triple_buffer geometry_tb; /* producer writes, consumer reads */
    ax_evq_geometry geometry[3];

    /* producer only */
    uint32_t deferred;
    uint32_t deferred_focus;
    uint32_t deferred_occlusion;
    uint64_t deferred_close_time;
    uint64_t deferred_focus_time;
    uint64_t deferred_occlusion_time;
} ax_evq_window;

typedef struct ax_event_queue {
    ax_allocator allocator;
    ax_evq_record *ring;
    uint32_t capacity; /* power of two */
    uint32_t mask;
    ax_atomic_u32 head; /* next record to read; written by the consumer */
    ax_atomic_u32 tail; /* next record to write; written by the producer */

    ax_evq_window *windows;
    uint32_t window_count;

    ax_atomic_u32 has_deferred; /* producer sets; consumer reads to know whether to wake it */
    void (*wake_producer)(void *ctx);
    void *wake_ctx;

    /* Consumer sleeping in ax_evq_wait. The producer takes the mutex only to wake it. */
    ax_mutex wait_lock;
    ax_cond wait_cond;
    ax_atomic_u32 consumer_sleeping;

    /* Optional hook called by the producer after it queues records (ax_app_set_event_notify). */
    ax_mutex notify_lock;
    ax_atomic_u32 notify_set;
    void (*notify)(void *user);
    void *notify_user;
} ax_event_queue;

/* capacity is rounded up to a power of two (minimum 8). wake_producer is called by the consumer
 * when it frees space while events are deferred (it should make the producer call
 * ax_evq_flush soon); it may be NULL. */
ax_result ax_evq_init(ax_event_queue *q, const ax_allocator *allocator, uint32_t capacity,
                      uint32_t max_windows, void (*wake_producer)(void *ctx), void *wake_ctx);
void ax_evq_deinit(ax_event_queue *q);

/* ---- producer (main thread) ---- */

void ax_evq_window_open(ax_event_queue *q, uint32_t index, uint64_t handle);
void ax_evq_window_close(ax_event_queue *q, uint32_t index);

/* Publishes the window's latest geometry and queues RESIZED and/or SCALE_CHANGED markers. */
void ax_evq_set_geometry(ax_event_queue *q, uint32_t index, const ax_evq_geometry *geometry,
                         bool size_changed, bool scale_changed);
void ax_evq_push_close(ax_event_queue *q, uint32_t index, uint64_t time_ns);
void ax_evq_push_focus(ax_event_queue *q, uint32_t index, bool focused, uint64_t time_ns);
void ax_evq_push_occlusion(ax_event_queue *q, uint32_t index, bool occluded, uint64_t time_ns);

/* Moves deferred events into the ring while there is room. Cheap when nothing is deferred. */
void ax_evq_flush(ax_event_queue *q);

/* Sets (fn non-NULL) or clears the producer's notification hook. Returns once no call to the
 * previous hook is in progress. Any thread. */
void ax_evq_set_notify(ax_event_queue *q, void (*fn)(void *user), void *user);

/* ---- consumer ---- */

/* Takes the next event; false if none. Fills type, window (raw handle), time and data. */
bool ax_evq_pop(ax_event_queue *q, ax_event *out);

/* Like ax_evq_pop, waiting up to timeout_ns for an event (unsupported without threads: returns
 * what ax_evq_pop does). */
bool ax_evq_wait(ax_event_queue *q, ax_event *out, uint64_t timeout_ns);

#endif /* AX_PLATFORM_EVENT_QUEUE_H */
