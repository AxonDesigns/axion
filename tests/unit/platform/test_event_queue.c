/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_tracking_allocator.h"
#include "common/event_queue.h"

#define W0 0x0000000100000000ull /* raw handles: generation 1, slots 0 and 1 */
#define W1 0x0000000100000001ull

typedef struct fixture {
    ax_tracking_allocator tracker;
    ax_allocator allocator;
    ax_event_queue q;
    int wakes;
} fixture;

static void count_wake(void *ctx)
{
    ((fixture *)ctx)->wakes++;
}

static void setup(fixture *f, uint32_t capacity)
{
    ax_tracking_allocator_init(&f->tracker, ax_default_allocator());
    f->allocator = ax_tracking_allocator_get(&f->tracker);
    f->wakes = 0;
    ax_evq_init(&f->q, &f->allocator, capacity, 4, count_wake, f);
    ax_evq_window_open(&f->q, 0, W0);
    ax_evq_window_open(&f->q, 1, W1);
}

static size_t teardown(fixture *f)
{
    ax_evq_deinit(&f->q);
    return ax_tracking_allocator_deinit(&f->tracker);
}

static ax_evq_geometry geometry(float w, float h, float scale, uint64_t t)
{
    ax_evq_geometry g;
    g.width = w;
    g.height = h;
    g.scale = scale;
    g.pixel_width = (uint32_t)(w * scale);
    g.pixel_height = (uint32_t)(h * scale);
    g.time_ns = t;
    return g;
}

static ax_event pop(fixture *f)
{
    ax_event e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    if (!ax_evq_pop(&f->q, &e)) {
        e.type = AX_EVENT_NONE;
    }
    return e;
}

AX_TEST(events_arrive_in_order_with_their_data)
{
    fixture f;
    ax_evq_geometry g = geometry(800, 600, 1.5f, 42);
    ax_event e;
    setup(&f, 16);

    ax_evq_push_focus(&f.q, 0, true, 1);
    ax_evq_set_geometry(&f.q, 0, &g, true, true);
    ax_evq_push_occlusion(&f.q, 1, true, 3);
    ax_evq_push_close(&f.q, 1, 4);

    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_FOCUS);
    AX_CHECK_EQ_U(e.window.h, W0);
    AX_CHECK(e.data.focus.focused);
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_RESIZED);
    AX_CHECK(e.data.resized.width == 800.0f && e.data.resized.height == 600.0f);
    AX_CHECK_EQ_U(e.data.resized.pixel_width, 1200);
    AX_CHECK(e.data.resized.scale == 1.5f);
    AX_CHECK_EQ_U(e.time_ns, 42);
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_SCALE_CHANGED);
    AX_CHECK(e.data.scale_changed.scale == 1.5f);
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_OCCLUSION);
    AX_CHECK_EQ_U(e.window.h, W1);
    AX_CHECK(e.data.occlusion.occluded);
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_CLOSE_REQUESTED);
    AX_CHECK_EQ_U(e.time_ns, 4);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(resizes_coalesce_to_the_latest)
{
    fixture f;
    int i;
    ax_event e;
    setup(&f, 16);

    for (i = 1; i <= 100; ++i) {
        ax_evq_geometry g = geometry((float)(100 + i), 200, 1.0f, (uint64_t)i);
        ax_evq_set_geometry(&f.q, 0, &g, true, false);
    }
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_RESIZED);
    AX_CHECK(e.data.resized.width == 200.0f);   /* the 100th */
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE); /* one marker only */

    /* After the consumer took it, a new change queues a new marker. */
    {
        ax_evq_geometry g = geometry(321, 200, 1.0f, 500);
        ax_evq_set_geometry(&f.q, 0, &g, true, false);
    }
    e = pop(&f);
    AX_CHECK_EQ_U(e.type, AX_EVENT_RESIZED);
    AX_CHECK(e.data.resized.width == 321.0f);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(scale_changes_coalesce_per_window)
{
    fixture f;
    ax_evq_geometry a = geometry(100, 100, 1.25f, 1);
    ax_evq_geometry b = geometry(100, 100, 2.0f, 2);
    ax_evq_geometry c = geometry(50, 50, 1.5f, 3);
    ax_event e;
    setup(&f, 16);

    ax_evq_set_geometry(&f.q, 0, &a, false, true);
    ax_evq_set_geometry(&f.q, 1, &c, false, true);
    ax_evq_set_geometry(&f.q, 0, &b, false, true);
    e = pop(&f);
    AX_CHECK_EQ_U(e.window.h, W0);
    AX_CHECK(e.data.scale_changed.scale == 2.0f);
    e = pop(&f);
    AX_CHECK_EQ_U(e.window.h, W1);
    AX_CHECK(e.data.scale_changed.scale == 1.5f);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(repeated_close_merges_until_delivered)
{
    fixture f;
    setup(&f, 16);
    ax_evq_push_close(&f.q, 0, 1);
    ax_evq_push_close(&f.q, 0, 2);
    ax_evq_push_close(&f.q, 0, 3);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_CLOSE_REQUESTED);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE);
    ax_evq_push_close(&f.q, 0, 4); /* the app ignored the first: the user asks again */
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_CLOSE_REQUESTED);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(full_ring_defers_and_never_loses_close_or_focus)
{
    fixture f;
    int i;
    int focus_events = 0;
    int close_events = 0;
    bool last_focus = false;
    ax_event e;
    setup(&f, 8); /* minimum capacity */

    /* Fill the ring with focus changes, then keep going: the rest is deferred. */
    for (i = 0; i < 8; ++i) {
        ax_evq_push_focus(&f.q, 0, (i % 2) == 0, (uint64_t)i);
    }
    ax_evq_push_focus(&f.q, 0, true, 100);  /* deferred */
    ax_evq_push_focus(&f.q, 0, false, 101); /* deferred, replaces the previous */
    ax_evq_push_close(&f.q, 1, 102);        /* deferred */
    AX_CHECK(f.q.windows[0].deferred != 0);

    /* The consumer frees space and wakes the producer; the producer flushes. */
    for (i = 0; i < 8; ++i) {
        e = pop(&f);
        AX_REQUIRE(e.type == AX_EVENT_FOCUS);
        AX_CHECK(e.data.focus.focused == ((i % 2) == 0));
    }
    AX_CHECK(f.wakes > 0);
    ax_evq_flush(&f.q);
    while ((e = pop(&f)).type != AX_EVENT_NONE) {
        if (e.type == AX_EVENT_FOCUS) {
            focus_events++;
            last_focus = e.data.focus.focused;
        } else if (e.type == AX_EVENT_CLOSE_REQUESTED) {
            close_events++;
        }
    }
    AX_CHECK_EQ_I(focus_events, 1); /* held-back changes collapse to the latest state */
    AX_CHECK(!last_focus);
    AX_CHECK_EQ_I(close_events, 1);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.q.has_deferred, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(coalesced_markers_never_overflow)
{
    fixture f;
    int i;
    setup(&f, 8);
    /* Thousands of resizes on two windows use at most two ring slots. */
    for (i = 0; i < 5000; ++i) {
        ax_evq_geometry g = geometry((float)i, 10, 1.0f, (uint64_t)i);
        ax_evq_set_geometry(&f.q, (uint32_t)(i % 2), &g, true, false);
    }
    AX_CHECK_EQ_U(ax_atomic_load_u32(&f.q.has_deferred, AX_MO_RELAXED), 0);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_RESIZED);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_RESIZED);
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

AX_TEST(events_of_closed_windows_are_dropped)
{
    fixture f;
    ax_event e;
    setup(&f, 16);
    ax_evq_push_focus(&f.q, 0, true, 1);
    ax_evq_push_focus(&f.q, 1, true, 2);
    ax_evq_window_close(&f.q, 0);
    ax_evq_window_open(&f.q, 0, W0 + (1ull << 32)); /* slot reused by a new window */
    e = pop(&f);
    AX_CHECK_EQ_U(e.window.h, W1); /* the old window's event was dropped */
    AX_CHECK_EQ_U(pop(&f).type, AX_EVENT_NONE);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

typedef struct wait_ctx {
    fixture *f;
} wait_ctx;

static int push_after_a_while(void *arg)
{
    wait_ctx *c = (wait_ctx *)arg;
    uint64_t until = ax_time_monotonic_ns() + 20ull * 1000000u;
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
    ax_evq_push_close(&c->f->q, 0, 7);
    return 0;
}

AX_TEST(wait_wakes_on_push_and_times_out)
{
    fixture f;
    wait_ctx c;
    ax_thread t;
    ax_event e;
    uint64_t start;
    setup(&f, 16);

    memset(&e, 0, sizeof e);
    start = ax_time_monotonic_ns();
    AX_CHECK(!ax_evq_wait(&f.q, &e, 10ull * 1000000u));
    AX_CHECK(ax_time_monotonic_ns() - start >= 10ull * 1000000u);

    c.f = &f;
    AX_REQUIRE(ax_thread_create(&t, push_after_a_while, &c) == AX_OK);
    AX_CHECK(ax_evq_wait(&f.q, &e, 10ull * 1000000000u));
    AX_CHECK_EQ_U(e.type, AX_EVENT_CLOSE_REQUESTED);
    ax_thread_join(&t);
    AX_CHECK_EQ_U(teardown(&f), 0);
}

/* A producer thread pushes alternating focus changes and flushes until nothing is deferred before
 * each push, so no change may be collapsed; the consumer must see every one, in order. */
enum { STRESS_EVENTS = 100000 };

static int stress_producer(void *arg)
{
    fixture *f = (fixture *)arg;
    int i;
    for (i = 0; i < STRESS_EVENTS; ++i) {
        while (ax_atomic_load_u32(&f->q.has_deferred, AX_MO_ACQUIRE) != 0) {
            ax_evq_flush(&f->q);
            ax_thread_yield();
        }
        ax_evq_push_focus(&f->q, 0, (i % 2) == 0, (uint64_t)i);
        if ((i % 64) == 0) {
            ax_evq_geometry g = geometry((float)i, 1, 1.0f, (uint64_t)i);
            ax_evq_set_geometry(&f->q, 1, &g, true, false);
        }
    }
    while (ax_atomic_load_u32(&f->q.has_deferred, AX_MO_ACQUIRE) != 0) {
        ax_evq_flush(&f->q);
        ax_thread_yield();
    }
    return 0;
}

AX_TEST(concurrent_producer_and_consumer_keep_order)
{
    static fixture f;
    ax_thread t;
    int seen = 0;
    int out_of_order = 0;
    float last_width = -1.0f;
    setup(&f, 64);
    /* The producer flushes on its own here; the wake callback only counts. */
    AX_REQUIRE(ax_thread_create(&t, stress_producer, &f) == AX_OK);
    while (seen < STRESS_EVENTS) {
        ax_event e;
        memset(&e, 0, sizeof e);
        if (!ax_evq_wait(&f.q, &e, 1000000000u)) {
            break;
        }
        if (e.type == AX_EVENT_FOCUS) {
            if (e.data.focus.focused != ((seen % 2) == 0) || e.time_ns != (uint64_t)seen) {
                out_of_order++;
            }
            seen++;
        } else if (e.type == AX_EVENT_RESIZED) {
            if (e.data.resized.width < last_width) {
                out_of_order++;
            }
            last_width = e.data.resized.width;
        }
    }
    ax_thread_join(&t);
    AX_CHECK_EQ_I(seen, STRESS_EVENTS);
    AX_CHECK_EQ_I(out_of_order, 0);
    AX_CHECK_EQ_U(teardown(&f), 0);
}
