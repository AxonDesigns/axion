/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The queue from the scene thread to the app thread: window events, rejections and how far the
 * applied transactions are on screen; and the app thread's ax_txn_on_presented callbacks.
 *
 * Records travel through a fixed SPSC ring. When it is full they wait, in order, in a scene-side
 * backlog flushed before the next record (as the rings of decision 65), so nothing is lost but to
 * out of memory. A REJECTED record is queued when the transaction is finished, before any
 * PRESENTED record that covers its sequence, so the app thread always knows a sequence was
 * rejected before it learns that the sequences around it are on screen.
 *
 * The app thread waits with the handshake of decisions 31 and 52: it reads event_gen, finds the
 * ring empty, announces that it sleeps and sleeps only if event_gen has not moved; the scene
 * thread bumps event_gen after each record and signals only when the app thread sleeps.
 */
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>

#define EVENT_CAPACITY 256u

ax_result ax__events_init(world_impl *w)
{
    ax_atomic_init_u64(&w->event_gen, 0);
    ax_atomic_init_u32(&w->app_sleeping, 0);
    if (ax_spsc_ring_init(&w->events, &w->allocator, EVENT_CAPACITY, sizeof(world_rec)) != AX_OK) {
        return AX_ERR_OUT_OF_MEMORY;
    }
    ax_mutex_init(&w->event_lock);
    ax_cond_init(&w->event_cond);
    w->event_sync_ready = true;
    return AX_OK;
}

void ax__events_free(world_impl *w)
{
    ax_spsc_ring_deinit(&w->events);
    if (w->event_sync_ready) {
        ax_cond_deinit(&w->event_cond);
        ax_mutex_deinit(&w->event_lock);
        w->event_sync_ready = false;
    }
    ax_mem_free(&w->allocator, w->event_backlog, sizeof(world_rec) * w->event_backlog_capacity);
    ax_mem_free(&w->allocator, w->regs, sizeof(presented_reg) * w->reg_capacity);
    w->event_backlog = NULL;
    w->regs = NULL;
}

/* ---- Scene thread ---------------------------------------------------------------------------- */

static void wake_app(world_impl *w)
{
    ax_atomic_fetch_add_u64(&w->event_gen, 1, AX_MO_SEQ_CST);
    if (ax_atomic_load_u32(&w->app_sleeping, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&w->event_lock);
        ax_cond_signal(&w->event_cond);
        ax_mutex_unlock(&w->event_lock);
    }
}

static bool flush(world_impl *w)
{
    uint32_t i = 0;
    while (i < w->event_backlog_count && ax_spsc_ring_push(&w->events, &w->event_backlog[i])) {
        i++;
    }
    if (i > 0) {
        memmove(w->event_backlog, w->event_backlog + i,
                sizeof(world_rec) * (w->event_backlog_count - i));
        w->event_backlog_count -= i;
        wake_app(w);
    }
    return w->event_backlog_count == 0;
}

static bool push_rec(world_impl *w, const world_rec *rec)
{
    if (flush(w) && ax_spsc_ring_push(&w->events, rec)) {
        wake_app(w);
        return true;
    }
    /* A PRESENTED record replaces one at the end of the backlog: only the newest matters. */
    if (rec->kind == WORLD_REC_PRESENTED && w->event_backlog_count > 0 &&
        w->event_backlog[w->event_backlog_count - 1].kind == WORLD_REC_PRESENTED) {
        w->event_backlog[w->event_backlog_count - 1] = *rec;
        return true;
    }
    if (w->event_backlog_count == w->event_backlog_capacity) {
        uint32_t cap = w->event_backlog_capacity < 64u ? 64u : w->event_backlog_capacity * 2u;
        world_rec *fresh = (world_rec *)ax_mem_alloc(&w->allocator, sizeof(world_rec) * cap,
                                                     AX_ALIGNOF(world_rec));
        if (fresh == NULL) {
            return false;
        }
        if (w->event_backlog != NULL) {
            memcpy(fresh, w->event_backlog, sizeof(world_rec) * w->event_backlog_count);
            ax_mem_free(&w->allocator, w->event_backlog,
                        sizeof(world_rec) * w->event_backlog_capacity);
        }
        w->event_backlog = fresh;
        w->event_backlog_capacity = cap;
    }
    w->event_backlog[w->event_backlog_count++] = *rec;
    return true;
}

static world_impl *scene_world(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT_MSG(w != NULL, "world events: invalid world");
    if (w != NULL) {
        AX_ASSERT_MSG(ax__world_on_scene_thread(w), "world events: not the world's scene thread");
    }
    return w;
}

bool ax_world_push_event(ax_world world, const ax_world_event *event)
{
    world_impl *w = scene_world(world);
    world_rec rec;
    if (w == NULL || event == NULL) {
        return false;
    }
    memset(&rec, 0, sizeof rec);
    rec.kind = WORLD_REC_EVENT;
    rec.event = *event;
    rec.event.struct_size = sizeof rec.event;
    return push_rec(w, &rec);
}

void ax_world_report_presented(ax_world world, uint64_t through_seq, uint64_t time_ns)
{
    world_impl *w = scene_world(world);
    world_rec rec;
    if (w == NULL) {
        return;
    }
    AX_ASSERT_MSG(through_seq <= ax_atomic_load_u64(&w->applied_seq, AX_MO_RELAXED),
                  "ax_world_report_presented: the sequence is not finished");
    if (through_seq <= w->reported_through) {
        (void)flush(w);
        return;
    }
    w->reported_through = through_seq;
    memset(&rec, 0, sizeof rec);
    rec.kind = WORLD_REC_PRESENTED;
    rec.seq = through_seq;
    rec.time_ns = time_ns;
    (void)push_rec(w, &rec);
}

bool ax_world_flush_events(ax_world world)
{
    world_impl *w = scene_world(world);
    return w != NULL && flush(w);
}

void ax__events_rejected(world_impl *w, uint64_t seq)
{
    world_rec rec;
    memset(&rec, 0, sizeof rec);
    rec.kind = WORLD_REC_REJECTED;
    rec.seq = seq;
    (void)push_rec(w, &rec);
}

/* ---- App thread: registrations --------------------------------------------------------------- */

/* Index of the first registration with a sequence >= seq. */
static uint32_t lower_bound(const world_impl *w, uint64_t seq)
{
    uint32_t lo = 0, hi = w->reg_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (w->regs[mid].seq < seq) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static bool was_rejected(const world_impl *w, uint64_t seq)
{
    return w->rejected_seq[seq % AX_TXN_PRESENTED_WINDOW] == seq;
}

static void on_rejected(world_impl *w, uint64_t seq)
{
    uint32_t i = lower_bound(w, seq);
    w->rejected_seq[seq % AX_TXN_PRESENTED_WINDOW] = seq;
    for (; i < w->reg_count && w->regs[i].seq == seq; ++i) {
        w->regs[i].rejected = true;
    }
}

/* Takes every record waiting; returns the events in order through `out` one at a time: true and
 * *out filled when an event was taken (records before it were applied). */
static bool take_records(world_impl *w, ax_world_event *out)
{
    world_rec rec;
    while (ax_spsc_ring_pop(&w->events, &rec)) {
        switch (rec.kind) {
        case WORLD_REC_REJECTED:
            on_rejected(w, rec.seq);
            break;
        case WORLD_REC_PRESENTED:
            if (rec.seq > w->presented_through) {
                w->presented_through = rec.seq;
                w->presented_time = rec.time_ns;
            }
            break;
        case WORLD_REC_EVENT:
        default: {
            uint32_t size = out->struct_size;
            *out = rec.event;
            out->struct_size = size;
            return true;
        }
        }
    }
    return false;
}

/* Runs the callbacks that are due, in sequence order: from the oldest registration, while it is
 * rejected or presented. Returns how many ran. */
static uint32_t deliver(world_impl *w)
{
    uint32_t n = 0;
    if (w->delivering) {
        return 0; /* called from a callback: the outer loop continues */
    }
    w->delivering = true;
    while (w->reg_count > 0) {
        presented_reg r = w->regs[0];
        ax_presented_status status;
        if (r.rejected) {
            status = AX_PRESENTED_REJECTED;
        } else if (r.seq <= w->presented_through) {
            status = AX_PRESENTED;
        } else {
            break;
        }
        /* Remove first: the callback may register more. */
        memmove(w->regs, w->regs + 1, sizeof(presented_reg) * (w->reg_count - 1u));
        w->reg_count--;
        r.fn(r.user, r.seq, status, status == AX_PRESENTED ? w->presented_time : 0);
        n++;
    }
    w->delivering = false;
    return n;
}

ax_result ax_txn_on_presented(ax_world world, uint64_t seq, ax_presented_fn fn, void *user)
{
    world_impl *w = ax__world_from_handle(world);
    presented_reg r;
    uint32_t at;
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_txn_on_presented: invalid world");
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_txn_on_presented: not the app thread");
    if (!ax__world_on_app_thread(w)) {
        return ax_set_error(AX_ERR_WRONG_THREAD, "ax_txn_on_presented: not the app thread");
    }
    if (fn == NULL || seq == 0 || seq > w->next_seq) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_txn_on_presented: sequence %llu was not committed (last %llu)",
                            (unsigned long long)seq, (unsigned long long)w->next_seq);
    }
    if (w->next_seq - seq >= AX_TXN_PRESENTED_WINDOW) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_txn_on_presented: sequence %llu is older than the last %u commits",
                            (unsigned long long)seq, AX_TXN_PRESENTED_WINDOW);
    }
    if (w->reg_count == w->reg_capacity) {
        uint32_t cap = w->reg_capacity < 16u ? 16u : w->reg_capacity * 2u;
        presented_reg *fresh = (presented_reg *)ax_mem_alloc(
            &w->allocator, sizeof(presented_reg) * cap, AX_ALIGNOF(presented_reg));
        if (fresh == NULL) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_txn_on_presented: out of memory");
        }
        if (w->regs != NULL) {
            memcpy(fresh, w->regs, sizeof(presented_reg) * w->reg_count);
            ax_mem_free(&w->allocator, w->regs, sizeof(presented_reg) * w->reg_capacity);
        }
        w->regs = fresh;
        w->reg_capacity = cap;
    }
    r.seq = seq;
    r.fn = fn;
    r.user = user;
    /* A record already taken may have said it was rejected. Within the window, the slot can
     * only hold this seq or an older one, so the comparison is exact. */
    r.rejected = was_rejected(w, seq);
    at = lower_bound(w, seq + 1u); /* after registrations of the same seq: in call order */
    memmove(w->regs + at + 1, w->regs + at, sizeof(presented_reg) * (w->reg_count - at));
    w->regs[at] = r;
    w->reg_count++;
    return AX_OK;
}

/* ---- App thread: polling --------------------------------------------------------------------- */

static ax_result check_out(world_impl *w, ax_world_event *out, const char *fn)
{
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "%s: invalid world", fn);
    }
    if (out == NULL || out->struct_size < sizeof(ax_world_event)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "%s: out must have struct_size >= %u", fn,
                            (unsigned)sizeof(ax_world_event));
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "world events: not the app thread");
    return AX_OK;
}

static void clear_event(ax_world_event *out)
{
    uint32_t size = out->struct_size;
    memset(out, 0, sizeof *out);
    out->struct_size = size;
    out->type = AX_WORLD_EVENT_NONE;
}

/* Takes records up to the next event and runs due callbacks. True when an event was taken;
 * *ran counts the callbacks. */
static bool poll_once(world_impl *w, ax_world_event *out, uint32_t *ran)
{
    bool got;
    ax__world_reclaim(w);
    got = take_records(w, out);
    *ran += deliver(w);
    return got;
}

ax_result ax_world_poll_event(ax_world world, ax_world_event *out)
{
    world_impl *w = ax__world_from_handle(world);
    ax_result r = check_out(w, out, "ax_world_poll_event");
    uint32_t ran = 0;
    if (r != AX_OK) {
        return r;
    }
    clear_event(out);
    (void)poll_once(w, out, &ran);
    return AX_OK;
}

ax_result ax_world_wait_event(ax_world world, ax_world_event *out, uint64_t timeout_ns)
{
    world_impl *w = ax__world_from_handle(world);
    ax_result r = check_out(w, out, "ax_world_wait_event");
    uint64_t deadline;
    uint32_t ran = 0;
    if (r != AX_OK) {
        return r;
    }
    clear_event(out);
    if (timeout_ns == 0 || !ax_threads_supported()) {
        (void)poll_once(w, out, &ran);
        return AX_OK;
    }
    deadline = timeout_ns > UINT64_MAX - ax_time_monotonic_ns()
                   ? UINT64_MAX
                   : ax_time_monotonic_ns() + timeout_ns;
    for (;;) {
        uint64_t gen = ax_atomic_load_u64(&w->event_gen, AX_MO_SEQ_CST);
        uint64_t now;
        if (poll_once(w, out, &ran) || ran > 0) {
            return AX_OK;
        }
        now = ax_time_monotonic_ns();
        if (now >= deadline) {
            return AX_OK;
        }
        ax_mutex_lock(&w->event_lock);
        ax_atomic_store_u32(&w->app_sleeping, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&w->event_gen, AX_MO_SEQ_CST) == gen) {
            ax_cond_wait_timeout(&w->event_cond, &w->event_lock, deadline - now);
        }
        ax_atomic_store_u32(&w->app_sleeping, 0, AX_MO_SEQ_CST);
        ax_mutex_unlock(&w->event_lock);
    }
}

void ax__events_world_destroyed(world_impl *w)
{
    ax_world_event ignored;
    memset(&ignored, 0, sizeof ignored);
    ignored.struct_size = sizeof ignored;
    /* What was already decided is reported as such; the rest never will be. */
    while (take_records(w, &ignored)) {
    }
    (void)deliver(w);
    w->delivering = true; /* a callback must not register more now */
    while (w->reg_count > 0) {
        presented_reg r = w->regs[0];
        memmove(w->regs, w->regs + 1, sizeof(presented_reg) * (w->reg_count - 1u));
        w->reg_count--;
        r.fn(r.user, r.seq, r.rejected ? AX_PRESENTED_REJECTED : AX_PRESENTED_WORLD_DESTROYED, 0);
    }
}
