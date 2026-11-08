/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The queue from the scene thread to the app thread (events, ax_txn_on_presented) and the clock's
 * translation from the platform's monotonic times.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_manual_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

static ax_world make_world(manual_clock *clock)
{
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    if (clock != NULL) {
        desc.clock = read_manual_clock;
        desc.clock_user = clock;
    }
    if (ax_world_create(&desc, &w) != AX_OK) {
        w.h = 0;
    }
    return w;
}

/* Commits a transaction creating one node, or (bad) setting a property of a node that never
 * existed, which the scene rejects. Returns its sequence. */
static uint64_t commit_one(ax_world w, bool bad)
{
    ax_txn *t = ax_txn_begin(w);
    uint64_t seq = 0;
    if (bad) {
        ax_node ghost;
        ghost.h = ax_handle_make(100000u, 1u);
        ax_txn_set_f32(t, ghost, AX_PROP_OPACITY, 0.5f);
    } else {
        (void)ax_txn_create(t, AX_NODE_VIEW);
    }
    if (ax_txn_commit(t, &seq) != AX_OK) {
        return 0;
    }
    return seq;
}

static ax_world_event empty_event(void)
{
    ax_world_event e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    return e;
}

/* ---- Clock ----------------------------------------------------------------------------------- */

AX_TEST(the_default_clock_is_the_platforms)
{
    ax_world w = make_world(NULL);
    uint64_t t = ax_time_monotonic_ns() + 16000000u;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK(ax_world_clock_is_monotonic(w));
    AX_CHECK_EQ_U(ax_world_time_from_monotonic(w, t), t);
    AX_CHECK_EQ_U(ax_world_time_from_monotonic(w, 12345u), 12345u);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(an_injected_clock_keeps_the_distance_from_now)
{
    manual_clock clock = {5000000000ull};
    ax_world w = make_world(&clock);
    uint64_t ahead, behind;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK(!ax_world_clock_is_monotonic(w));
    /* 16 ms ahead on the monotonic clock is about 16 ms ahead of the manual clock's now (the two
     * reads of the monotonic clock are microseconds apart). */
    ahead = ax_world_time_from_monotonic(w, ax_time_monotonic_ns() + 16000000u);
    AX_CHECK(ahead > clock.now + 15000000u && ahead <= clock.now + 16000000u);
    behind = ax_world_time_from_monotonic(w, ax_time_monotonic_ns() - 4000000u);
    AX_CHECK(behind <= clock.now - 4000000u && behind > clock.now - 5000000u);
    /* Far in the past: saturates at 0. */
    clock.now = 1000u;
    AX_CHECK_EQ_U(ax_world_time_from_monotonic(w, 1u), 0);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Events ---------------------------------------------------------------------------------- */

AX_TEST(events_reach_the_app_in_order_even_past_the_ring)
{
    ax_world w = make_world(NULL);
    ax_world_event e = empty_event();
    uint32_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK_RESULT(ax_world_poll_event(w, &e), AX_OK);
    AX_CHECK_EQ_U(e.type, AX_WORLD_EVENT_NONE);
    e.struct_size = 8;
    AX_CHECK_RESULT(ax_world_poll_event(w, &e), AX_ERR_ABI_MISMATCH);

    /* 1000 events: more than the ring holds, so most wait in the scene's backlog. */
    for (i = 0; i < 1000u; ++i) {
        ax_world_event ev = empty_event();
        ev.type = AX_WORLD_EVENT_RESIZED;
        ev.window = 7;
        ev.time_ns = i;
        ev.data.resized.width = (float)i;
        AX_REQUIRE(ax_world_push_event(w, &ev));
    }
    for (i = 0; i < 1000u; ++i) {
        e = empty_event();
        AX_REQUIRE(ax_world_poll_event(w, &e) == AX_OK);
        if (e.type == AX_WORLD_EVENT_NONE) {
            /* The ring was drained; the scene sends the backlog on its next call. */
            (void)ax_world_flush_events(w);
            AX_REQUIRE(ax_world_poll_event(w, &e) == AX_OK);
        }
        AX_REQUIRE(e.type == AX_WORLD_EVENT_RESIZED);
        AX_CHECK_EQ_U(e.time_ns, i);
        AX_CHECK(e.data.resized.width == (float)i);
        AX_CHECK_EQ_U(e.struct_size, sizeof e);
    }
    e = empty_event();
    (void)ax_world_flush_events(w);
    AX_CHECK_RESULT(ax_world_poll_event(w, &e), AX_OK);
    AX_CHECK_EQ_U(e.type, AX_WORLD_EVENT_NONE);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

typedef struct cross {
    ax_world world;
    uint32_t count;
    ax_atomic_u32 bound;
} cross;

static int scene_pushes(void *arg)
{
    cross *c = (cross *)arg;
    uint32_t i;
    ax_world_bind_scene_thread(c->world);
    ax_atomic_store_u32(&c->bound, 1, AX_MO_RELEASE);
    for (i = 0; i < c->count; ++i) {
        ax_world_event ev = empty_event();
        ev.type = AX_WORLD_EVENT_FOCUS;
        ev.time_ns = i;
        ev.data.focus.focused = (i % 2u) == 0;
        (void)ax_world_push_event(c->world, &ev);
        if (i % 64u == 0) {
            ax_thread_yield();
        }
    }
    while (!ax_world_flush_events(c->world)) {
        ax_thread_yield();
    }
    return 0;
}

/* Under TSan: the scene thread pushes while the app thread waits; every event arrives, in order. */
AX_TEST(events_cross_threads_while_the_app_waits)
{
    static cross c;
    ax_thread t;
    uint32_t next = 0;
    uint64_t until;
    memset(&c, 0, sizeof c);
    c.world = make_world(NULL);
    c.count = 20000u;
    ax_atomic_init_u32(&c.bound, 0);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(c.world));
    AX_REQUIRE(ax_thread_create(&t, scene_pushes, &c) == AX_OK);
    until = ax_time_monotonic_ns() + 20000000000ull;
    while (next < c.count && ax_time_monotonic_ns() < until) {
        ax_world_event e = empty_event();
        AX_REQUIRE(ax_world_wait_event(c.world, &e, 100000000u) == AX_OK);
        if (e.type == AX_WORLD_EVENT_NONE) {
            continue;
        }
        AX_REQUIRE(e.type == AX_WORLD_EVENT_FOCUS);
        AX_REQUIRE(e.time_ns == next);
        AX_REQUIRE(e.data.focus.focused == ((next % 2u) == 0));
        next++;
    }
    ax_thread_join(&t);
    AX_CHECK_EQ_U(next, c.count);
    ax_world_bind_scene_thread(c.world);
    AX_CHECK_RESULT(ax_world_destroy(c.world), AX_OK);
}

/* ---- on_presented ---------------------------------------------------------------------------- */

#define MAX_CALLS 32

typedef struct call_log {
    uint32_t count;
    uint64_t seq[MAX_CALLS];
    ax_presented_status status[MAX_CALLS];
    uint64_t time[MAX_CALLS];
    uint32_t tag[MAX_CALLS];
} call_log;

typedef struct tagged {
    call_log *log;
    uint32_t tag;
} tagged;

static void record_call(void *user, uint64_t seq, ax_presented_status status, uint64_t time)
{
    tagged *t = (tagged *)user;
    call_log *l = t->log;
    if (l->count < MAX_CALLS) {
        l->seq[l->count] = seq;
        l->status[l->count] = status;
        l->time[l->count] = time;
        l->tag[l->count] = t->tag;
    }
    l->count++;
}

static void poll_all(ax_world w)
{
    ax_world_event e;
    do {
        e = empty_event();
        (void)ax_world_poll_event(w, &e);
    } while (e.type != AX_WORLD_EVENT_NONE);
}

AX_TEST(presented_callbacks_come_once_in_order_with_their_status)
{
    manual_clock clock = {1000u};
    ax_world w = make_world(&clock);
    call_log log;
    tagged tags[5];
    uint64_t s1, s2, s3;
    uint32_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    memset(&log, 0, sizeof log);
    for (i = 0; i < 5; ++i) {
        tags[i].log = &log;
        tags[i].tag = i;
    }
    s1 = commit_one(w, false);
    s2 = commit_one(w, true);
    s3 = commit_one(w, false);
    AX_REQUIRE(s1 == 1 && s2 == 2 && s3 == 3);
    AX_CHECK_RESULT(ax_txn_on_presented(w, s3, record_call, &tags[3]), AX_OK);
    AX_CHECK_RESULT(ax_txn_on_presented(w, s1, record_call, &tags[1]), AX_OK);
    AX_CHECK_RESULT(ax_txn_on_presented(w, s2, record_call, &tags[2]), AX_OK);
    AX_CHECK_RESULT(ax_txn_on_presented(w, s3, record_call, &tags[4]), AX_OK);

    AX_REQUIRE(ax_world_apply_pending(w, clock.now) == 3);
    poll_all(w);
    /* 2 is rejected, but 1 is not on screen yet: nothing comes out of order. */
    AX_CHECK_EQ_U(log.count, 0);

    clock.now = 2000u;
    ax_world_report_presented(w, s1, 1900u);
    poll_all(w);
    AX_REQUIRE(log.count == 2);
    AX_CHECK_EQ_U(log.seq[0], 1);
    AX_CHECK_EQ_U(log.status[0], AX_PRESENTED);
    AX_CHECK_EQ_U(log.time[0], 1900u);
    AX_CHECK_EQ_U(log.seq[1], 2);
    AX_CHECK_EQ_U(log.status[1], AX_PRESENTED_REJECTED);
    AX_CHECK_EQ_U(log.time[1], 0);

    /* An older report is ignored; the newest one covers 3, called once per registration in the
     * order they were made. */
    ax_world_report_presented(w, s3, 2900u);
    ax_world_report_presented(w, s1, 3000u);
    poll_all(w);
    AX_REQUIRE(log.count == 4);
    AX_CHECK_EQ_U(log.seq[2], 3);
    AX_CHECK_EQ_U(log.tag[2], 3);
    AX_CHECK_EQ_U(log.seq[3], 3);
    AX_CHECK_EQ_U(log.tag[3], 4);
    AX_CHECK_EQ_U(log.status[3], AX_PRESENTED);
    AX_CHECK_EQ_U(log.time[3], 2900u);
    poll_all(w);
    AX_CHECK_EQ_U(log.count, 4); /* once */
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_CHECK_EQ_U(log.count, 4);
}

AX_TEST(a_registration_after_the_outcome_gets_it_on_the_next_poll)
{
    manual_clock clock = {1000u};
    ax_world w = make_world(&clock);
    call_log log;
    tagged tag;
    uint64_t good, bad;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    memset(&log, 0, sizeof log);
    tag.log = &log;
    tag.tag = 0;
    good = commit_one(w, false);
    bad = commit_one(w, true);
    AX_REQUIRE(ax_world_apply_pending(w, clock.now) == 2);
    ax_world_report_presented(w, bad, 1500u); /* 2 is covered too: it is rejected */
    poll_all(w);

    AX_CHECK_RESULT(ax_txn_on_presented(w, bad, record_call, &tag), AX_OK);
    AX_CHECK_RESULT(ax_txn_on_presented(w, good, record_call, &tag), AX_OK);
    AX_CHECK_EQ_U(log.count, 0); /* never from inside the registration */
    poll_all(w);
    AX_REQUIRE(log.count == 2);
    AX_CHECK_EQ_U(log.seq[0], good);
    AX_CHECK_EQ_U(log.status[0], AX_PRESENTED);
    AX_CHECK_EQ_U(log.time[0], 1500u);
    AX_CHECK_EQ_U(log.seq[1], bad);
    AX_CHECK_EQ_U(log.status[1], AX_PRESENTED_REJECTED);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(registrations_are_checked)
{
    ax_world w = make_world(NULL);
    call_log log;
    tagged tag;
    uint32_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    memset(&log, 0, sizeof log);
    tag.log = &log;
    tag.tag = 0;
    AX_CHECK_RESULT(ax_txn_on_presented(w, 1, record_call, &tag), AX_ERR_INVALID_ARGUMENT);
    AX_REQUIRE(commit_one(w, false) == 1);
    AX_CHECK_RESULT(ax_txn_on_presented(w, 0, record_call, &tag), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_txn_on_presented(w, 2, record_call, &tag), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_txn_on_presented(w, 1, NULL, &tag), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_txn_on_presented((ax_world){0}, 1, record_call, &tag),
                    AX_ERR_INVALID_HANDLE);
    /* Only the last AX_TXN_PRESENTED_WINDOW commits. */
    for (i = 0; i < AX_TXN_PRESENTED_WINDOW - 1u; ++i) {
        AX_REQUIRE(commit_one(w, false) != 0);
    }
    AX_CHECK_RESULT(ax_txn_on_presented(w, 1, record_call, &tag), AX_OK);
    AX_REQUIRE(commit_one(w, false) != 0);
    AX_CHECK_RESULT(ax_txn_on_presented(w, 1, record_call, &tag), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_txn_on_presented(w, 2, record_call, &tag), AX_OK);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_CHECK_EQ_U(log.count, 2); /* both waiting ones, at destruction */
}

AX_TEST(destroying_the_world_reports_what_never_came)
{
    manual_clock clock = {1000u};
    ax_world w = make_world(&clock);
    call_log log;
    tagged tag;
    uint64_t s1, s2, s3;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    memset(&log, 0, sizeof log);
    tag.log = &log;
    tag.tag = 0;
    s1 = commit_one(w, false);
    s2 = commit_one(w, true);
    AX_REQUIRE(ax_world_apply_pending(w, clock.now) == 2);
    s3 = commit_one(w, false); /* never applied */
    AX_REQUIRE(ax_txn_on_presented(w, s1, record_call, &tag) == AX_OK);
    AX_REQUIRE(ax_txn_on_presented(w, s2, record_call, &tag) == AX_OK);
    AX_REQUIRE(ax_txn_on_presented(w, s3, record_call, &tag) == AX_OK);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_REQUIRE(log.count == 3);
    AX_CHECK_EQ_U(log.status[0], AX_PRESENTED_WORLD_DESTROYED); /* applied, never shown */
    AX_CHECK_EQ_U(log.status[1], AX_PRESENTED_REJECTED);        /* known before */
    AX_CHECK_EQ_U(log.status[2], AX_PRESENTED_WORLD_DESTROYED);
}

typedef struct late_report {
    ax_world world;
    ax_atomic_u32 bound;
    ax_atomic_u32 go;
} late_report;

static int scene_reports_later(void *arg)
{
    late_report *r = (late_report *)arg;
    ax_world_bind_scene_thread(r->world);
    ax_atomic_store_u32(&r->bound, 1, AX_MO_RELEASE);
    while (ax_atomic_load_u32(&r->go, AX_MO_ACQUIRE) == 0) {
        ax_thread_yield();
    }
    (void)ax_world_apply_pending(r->world, UINT64_MAX);
    {
        uint64_t until = ax_time_monotonic_ns() + 20000000u; /* base has no sleep */
        while (ax_time_monotonic_ns() < until) {
            ax_thread_yield();
        }
    }
    ax_world_report_presented(r->world, 1, 42u);
    return 0;
}

/* A waiting app thread wakes up for a callback that becomes due, even without an event. */
AX_TEST(waiting_for_events_returns_when_a_callback_is_due)
{
    static late_report r;
    call_log log;
    tagged tag;
    ax_thread t;
    ax_world_event e = empty_event();
    uint64_t start;
    memset(&r, 0, sizeof r);
    memset(&log, 0, sizeof log);
    tag.log = &log;
    tag.tag = 0;
    r.world = make_world(NULL);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(r.world));
    AX_REQUIRE(commit_one(r.world, false) == 1);
    AX_REQUIRE(ax_txn_on_presented(r.world, 1, record_call, &tag) == AX_OK);
    ax_atomic_init_u32(&r.bound, 0);
    ax_atomic_init_u32(&r.go, 0);
    AX_REQUIRE(ax_thread_create(&t, scene_reports_later, &r) == AX_OK);
    while (ax_atomic_load_u32(&r.bound, AX_MO_ACQUIRE) == 0) {
        ax_thread_yield();
    }
    ax_atomic_store_u32(&r.go, 1, AX_MO_RELEASE);
    start = ax_time_monotonic_ns();
    while (log.count == 0 && ax_time_monotonic_ns() - start < 5000000000ull) {
        AX_REQUIRE(ax_world_wait_event(r.world, &e, 5000000000ull) == AX_OK);
    }
    AX_CHECK_EQ_U(e.type, AX_WORLD_EVENT_NONE);
    AX_REQUIRE(log.count == 1);
    AX_CHECK_EQ_U(log.time[0], 42u);
    AX_CHECK(ax_time_monotonic_ns() - start < 2000000000ull); /* woken, not timed out */
    ax_thread_join(&t);
    ax_world_bind_scene_thread(r.world);
    AX_CHECK_RESULT(ax_world_destroy(r.world), AX_OK);
}
