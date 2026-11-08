/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "event_queue.h"

#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"

/* ---- set-up -----------------------------------------------------------------------------------
 */

static uint32_t round_up_pow2(uint32_t x)
{
    uint32_t p = 8u;
    while (p < x && p < 0x80000000u) {
        p <<= 1;
    }
    return p;
}

ax_result ax_evq_init(ax_event_queue *q, const ax_allocator *allocator, uint32_t capacity,
                      uint32_t max_windows, void (*wake_producer)(void *ctx), void *wake_ctx)
{
    uint32_t i;
    size_t ring_bytes;
    size_t window_bytes;

    AX_ASSERT(q != NULL && allocator != NULL && max_windows > 0);
    memset(q, 0, sizeof *q);
    q->allocator = *allocator;
    q->capacity = round_up_pow2(capacity);
    q->mask = q->capacity - 1u;
    q->window_count = max_windows;
    q->wake_producer = wake_producer;
    q->wake_ctx = wake_ctx;

    if (!ax_size_mul(q->capacity, sizeof(ax_evq_record), &ring_bytes) ||
        !ax_size_mul(max_windows, sizeof(ax_evq_window), &window_bytes)) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    q->ring = (ax_evq_record *)ax_mem_alloc(&q->allocator, ring_bytes, AX_ALIGNOF(ax_evq_record));
    q->windows =
        (ax_evq_window *)ax_mem_alloc(&q->allocator, window_bytes, AX_ALIGNOF(ax_evq_window));
    if (q->ring == NULL || q->windows == NULL) {
        ax_evq_deinit(q);
        return AX_ERR_OUT_OF_MEMORY;
    }
    memset(q->windows, 0, window_bytes);
    for (i = 0; i < max_windows; ++i) {
        ax_atomic_init_u64(&q->windows[i].handle, 0);
        ax_atomic_init_u32(&q->windows[i].resize_marker, 0);
        ax_atomic_init_u32(&q->windows[i].scale_marker, 0);
        ax_atomic_init_u32(&q->windows[i].close_marker, 0);
        ax_tb_init(&q->windows[i].geometry_tb);
    }
    ax_atomic_init_u32(&q->head, 0);
    ax_atomic_init_u32(&q->tail, 0);
    ax_atomic_init_u32(&q->has_deferred, 0);
    ax_atomic_init_u32(&q->consumer_sleeping, 0);
    ax_atomic_init_u32(&q->notify_set, 0);
    ax_mutex_init(&q->wait_lock);
    ax_cond_init(&q->wait_cond);
    ax_mutex_init(&q->notify_lock);
    return AX_OK;
}

void ax_evq_deinit(ax_event_queue *q)
{
    if (q->ring != NULL) {
        ax_mem_free(&q->allocator, q->ring, sizeof(ax_evq_record) * q->capacity);
        ax_cond_deinit(&q->wait_cond);
        ax_mutex_deinit(&q->wait_lock);
        ax_mutex_deinit(&q->notify_lock);
    }
    if (q->windows != NULL) {
        ax_mem_free(&q->allocator, q->windows, sizeof(ax_evq_window) * q->window_count);
    }
    q->ring = NULL;
    q->windows = NULL;
}

/* ---- producer ---------------------------------------------------------------------------------
 */

/* Wakes a consumer sleeping in ax_evq_wait. The mutex is only a handoff: the consumer holds it
 * just until it is inside the condition wait. */
static void wake_consumer(ax_event_queue *q)
{
    if (ax_atomic_load_u32(&q->consumer_sleeping, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&q->wait_lock);
        ax_cond_signal(&q->wait_cond);
        ax_mutex_unlock(&q->wait_lock);
    }
    /* A consumer that waits on something else (the scene thread) asked to be told. The call is
     * made under notify_lock, so clearing the hook waits for a call in progress. seq_cst pairs
     * with the record's publication: a consumer that sets the hook and then polls sees either the
     * record or the call. */
    if (ax_atomic_load_u32(&q->notify_set, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&q->notify_lock);
        if (q->notify != NULL) {
            q->notify(q->notify_user);
        }
        ax_mutex_unlock(&q->notify_lock);
    }
}

void ax_evq_set_notify(ax_event_queue *q, void (*fn)(void *user), void *user)
{
    ax_mutex_lock(&q->notify_lock);
    q->notify = fn;
    q->notify_user = user;
    ax_atomic_store_u32(&q->notify_set, fn != NULL ? 1u : 0u, AX_MO_SEQ_CST);
    ax_mutex_unlock(&q->notify_lock);
}

static bool ring_push(ax_event_queue *q, const ax_evq_record *rec)
{
    uint32_t tail = ax_atomic_load_u32(&q->tail, AX_MO_RELAXED); /* only the producer writes it */
    uint32_t head = ax_atomic_load_u32(&q->head, AX_MO_ACQUIRE);
    if (tail - head == q->capacity) {
        return false;
    }
    q->ring[tail & q->mask] = *rec;
    /* seq_cst pairs with the consumer's sleeping flag (see ax_evq_wait). */
    ax_atomic_store_u32(&q->tail, tail + 1u, AX_MO_SEQ_CST);
    return true;
}

static uint64_t window_handle(ax_event_queue *q, uint32_t index)
{
    return ax_atomic_load_u64(&q->windows[index].handle, AX_MO_RELAXED);
}

static bool push_record(ax_event_queue *q, uint32_t type, uint32_t index, uint64_t time_ns,
                        uint32_t value)
{
    ax_evq_record rec;
    rec.type = type;
    rec.window_index = index;
    rec.handle = window_handle(q, index);
    rec.time_ns = time_ns;
    rec.value = value;
    return ring_push(q, &rec);
}

static void defer(ax_event_queue *q, ax_evq_window *w, uint32_t bit)
{
    w->deferred |= bit;
    ax_atomic_store_u32(&q->has_deferred, 1, AX_MO_SEQ_CST);
}

/* Flushes one window's deferred events; false if the ring filled up. Coalesced markers carry no
 * time: the consumer reports the time of the geometry it reads. */
static bool flush_window(ax_event_queue *q, uint32_t index)
{
    ax_evq_window *w = &q->windows[index];

    if ((w->deferred & AX_EVQ_DEFER_CLOSE) != 0) {
        if (!push_record(q, AX_EVENT_CLOSE_REQUESTED, index, w->deferred_close_time, 0)) {
            return false;
        }
        w->deferred &= ~AX_EVQ_DEFER_CLOSE;
    }
    if ((w->deferred & AX_EVQ_DEFER_FOCUS) != 0) {
        if (!push_record(q, AX_EVENT_FOCUS, index, w->deferred_focus_time, w->deferred_focus)) {
            return false;
        }
        w->deferred &= ~AX_EVQ_DEFER_FOCUS;
    }
    if ((w->deferred & AX_EVQ_DEFER_OCCLUSION) != 0) {
        if (!push_record(q, AX_EVENT_OCCLUSION, index, w->deferred_occlusion_time,
                         w->deferred_occlusion)) {
            return false;
        }
        w->deferred &= ~AX_EVQ_DEFER_OCCLUSION;
    }
    if ((w->deferred & AX_EVQ_DEFER_RESIZE) != 0) {
        if (!push_record(q, AX_EVENT_RESIZED, index, 0, 0)) {
            return false;
        }
        w->deferred &= ~AX_EVQ_DEFER_RESIZE;
    }
    if ((w->deferred & AX_EVQ_DEFER_SCALE) != 0) {
        if (!push_record(q, AX_EVENT_SCALE_CHANGED, index, 0, 0)) {
            return false;
        }
        w->deferred &= ~AX_EVQ_DEFER_SCALE;
    }
    return true;
}

void ax_evq_flush(ax_event_queue *q)
{
    uint32_t i;
    bool pushed = false;
    if (ax_atomic_load_u32(&q->has_deferred, AX_MO_RELAXED) == 0) {
        return;
    }
    for (i = 0; i < q->window_count; ++i) {
        if (q->windows[i].deferred != 0) {
            uint32_t before = ax_atomic_load_u32(&q->tail, AX_MO_RELAXED);
            bool done = flush_window(q, i);
            pushed = pushed || ax_atomic_load_u32(&q->tail, AX_MO_RELAXED) != before;
            if (!done) {
                if (pushed) {
                    wake_consumer(q);
                }
                return; /* still full; the consumer wakes us when it frees space */
            }
        }
    }
    ax_atomic_store_u32(&q->has_deferred, 0, AX_MO_SEQ_CST);
    if (pushed) {
        wake_consumer(q);
    }
}

/* Queues a record, or defers it if the ring is full or anything is already deferred (to keep
 * ring order). Returns true if the record went into the ring. */
static bool push_or_defer(ax_event_queue *q, uint32_t index, uint32_t type, uint32_t defer_bit,
                          uint64_t time_ns, uint32_t value)
{
    ax_evq_window *w = &q->windows[index];
    ax_evq_flush(q);
    if (ax_atomic_load_u32(&q->has_deferred, AX_MO_RELAXED) == 0 &&
        push_record(q, type, index, time_ns, value)) {
        wake_consumer(q);
        return true;
    }
    switch (defer_bit) {
    case AX_EVQ_DEFER_CLOSE:
        w->deferred_close_time = time_ns;
        break;
    case AX_EVQ_DEFER_FOCUS:
        w->deferred_focus = value;
        w->deferred_focus_time = time_ns;
        break;
    case AX_EVQ_DEFER_OCCLUSION:
        w->deferred_occlusion = value;
        w->deferred_occlusion_time = time_ns;
        break;
    default:
        break;
    }
    defer(q, w, defer_bit);
    return false;
}

void ax_evq_window_open(ax_event_queue *q, uint32_t index, uint64_t handle)
{
    ax_evq_window *w;
    AX_ASSERT(index < q->window_count && handle != 0);
    w = &q->windows[index];
    w->deferred = 0;
    ax_atomic_store_u32(&w->resize_marker, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&w->scale_marker, 0, AX_MO_RELAXED);
    ax_atomic_store_u32(&w->close_marker, 0, AX_MO_RELAXED);
    ax_atomic_store_u64(&w->handle, handle, AX_MO_RELEASE);
}

void ax_evq_window_close(ax_event_queue *q, uint32_t index)
{
    ax_evq_window *w;
    AX_ASSERT(index < q->window_count);
    w = &q->windows[index];
    w->deferred = 0;
    ax_atomic_store_u64(&w->handle, 0, AX_MO_RELEASE);
}

/* Claims a window's marker; true if this call must queue it (no marker was pending). */
static bool claim_marker(ax_atomic_u32 *marker)
{
    uint32_t expected = 0;
    return ax_atomic_compare_exchange_u32(marker, &expected, 1, AX_MO_SEQ_CST, AX_MO_SEQ_CST);
}

void ax_evq_set_geometry(ax_event_queue *q, uint32_t index, const ax_evq_geometry *geometry,
                         bool size_changed, bool scale_changed)
{
    ax_evq_window *w;
    AX_ASSERT(index < q->window_count);
    w = &q->windows[index];
    w->geometry[ax_tb_write_index(&w->geometry_tb)] = *geometry;
    ax_tb_publish(&w->geometry_tb);

    if (size_changed && claim_marker(&w->resize_marker)) {
        push_or_defer(q, index, AX_EVENT_RESIZED, AX_EVQ_DEFER_RESIZE, geometry->time_ns, 0);
    }
    if (scale_changed && claim_marker(&w->scale_marker)) {
        push_or_defer(q, index, AX_EVENT_SCALE_CHANGED, AX_EVQ_DEFER_SCALE, geometry->time_ns, 0);
    }
}

void ax_evq_push_close(ax_event_queue *q, uint32_t index, uint64_t time_ns)
{
    AX_ASSERT(index < q->window_count);
    if (claim_marker(&q->windows[index].close_marker)) {
        push_or_defer(q, index, AX_EVENT_CLOSE_REQUESTED, AX_EVQ_DEFER_CLOSE, time_ns, 0);
    }
}

void ax_evq_push_focus(ax_event_queue *q, uint32_t index, bool focused, uint64_t time_ns)
{
    AX_ASSERT(index < q->window_count);
    push_or_defer(q, index, AX_EVENT_FOCUS, AX_EVQ_DEFER_FOCUS, time_ns, focused ? 1u : 0u);
}

void ax_evq_push_occlusion(ax_event_queue *q, uint32_t index, bool occluded, uint64_t time_ns)
{
    AX_ASSERT(index < q->window_count);
    push_or_defer(q, index, AX_EVENT_OCCLUSION, AX_EVQ_DEFER_OCCLUSION, time_ns,
                  occluded ? 1u : 0u);
}

/* ---- consumer ---------------------------------------------------------------------------------
 */

/* Releases a coalescing marker. An atomic read-modify-write, so it is ordered with the producer's
 * claim: a geometry published after this point always gets a new marker. */
static void release_marker(ax_atomic_u32 *marker)
{
    uint32_t expected = 1;
    ax_atomic_compare_exchange_u32(marker, &expected, 0, AX_MO_SEQ_CST, AX_MO_SEQ_CST);
}

bool ax_evq_pop(ax_event_queue *q, ax_event *out)
{
    for (;;) {
        uint32_t head = ax_atomic_load_u32(&q->head, AX_MO_RELAXED); /* only the consumer writes */
        uint32_t tail = ax_atomic_load_u32(&q->tail, AX_MO_ACQUIRE);
        ax_evq_record rec;
        ax_evq_window *w;

        if (head == tail) {
            return false;
        }
        rec = q->ring[head & q->mask];
        ax_atomic_store_u32(&q->head, head + 1u, AX_MO_RELEASE);
        if (ax_atomic_load_u32(&q->has_deferred, AX_MO_SEQ_CST) != 0 && q->wake_producer != NULL) {
            q->wake_producer(q->wake_ctx); /* there is room again for what was held back */
        }

        w = &q->windows[rec.window_index];
        if (ax_atomic_load_u64(&w->handle, AX_MO_ACQUIRE) != rec.handle) {
            continue; /* the window was closed after the record was queued */
        }

        memset(&out->data, 0, sizeof out->data);
        out->type = rec.type;
        out->window.h = rec.handle;
        out->time_ns = rec.time_ns;
        switch (rec.type) {
        case AX_EVENT_RESIZED:
        case AX_EVENT_SCALE_CHANGED: {
            const ax_evq_geometry *g;
            release_marker(rec.type == AX_EVENT_RESIZED ? &w->resize_marker : &w->scale_marker);
            g = &w->geometry[ax_tb_acquire(&w->geometry_tb)];
            out->time_ns = g->time_ns;
            if (rec.type == AX_EVENT_RESIZED) {
                out->data.resized.width = g->width;
                out->data.resized.height = g->height;
                out->data.resized.pixel_width = g->pixel_width;
                out->data.resized.pixel_height = g->pixel_height;
                out->data.resized.scale = g->scale;
            } else {
                out->data.scale_changed.scale = g->scale;
            }
            break;
        }
        case AX_EVENT_CLOSE_REQUESTED:
            release_marker(&w->close_marker);
            break;
        case AX_EVENT_FOCUS:
            out->data.focus.focused = rec.value != 0;
            break;
        case AX_EVENT_OCCLUSION:
            out->data.occlusion.occluded = rec.value != 0;
            break;
        default:
            break;
        }
        return true;
    }
}

bool ax_evq_wait(ax_event_queue *q, ax_event *out, uint64_t timeout_ns)
{
    uint64_t deadline;
    if (ax_evq_pop(q, out)) {
        return true;
    }
    if (timeout_ns == 0 || !ax_threads_supported()) {
        return false; /* no waiting without threads (decision 25) */
    }
    deadline = ax_time_monotonic_ns() + timeout_ns;
    for (;;) {
        uint64_t now;
        if (ax_evq_pop(q, out)) {
            return true;
        }
        now = ax_time_monotonic_ns();
        if (now >= deadline) {
            return false;
        }
        ax_mutex_lock(&q->wait_lock);
        /* Dekker handshake with the producer: announce sleeping, then re-check the ring. A record
         * pushed before this point is seen by the check; one pushed after sees the flag and
         * signals, which cannot happen before we wait because the producer must take the lock. */
        ax_atomic_store_u32(&q->consumer_sleeping, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u32(&q->tail, AX_MO_SEQ_CST) ==
            ax_atomic_load_u32(&q->head, AX_MO_RELAXED)) {
            ax_cond_wait_timeout(&q->wait_cond, &q->wait_lock, deadline - now);
        }
        ax_atomic_store_u32(&q->consumer_sleeping, 0, AX_MO_SEQ_CST);
        ax_mutex_unlock(&q->wait_lock);
    }
}
