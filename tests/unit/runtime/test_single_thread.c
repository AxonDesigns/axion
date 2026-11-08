/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The pipeline in single-threaded mode (ax_host_frame), deterministic with a manual clock and a
 * fake presenter: frame deadlines, the recorded IR, the mirror after the frame, the size
 * handshake, and ax_txn_on_presented's statuses and window rules (decision 77).
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_allocator.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_runtime.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_ir.h"
#include "axion/runtime/ax_runtime_sdk.h"

#define MS 1000000ull

/* ---- A manual clock and a fake presenter ----------------------------------------------------- */

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

typedef struct fake_presenter {
    ax_runtime rt;
    bool auto_present;                /* report each snapshot shown at once */
    float configured_w, configured_h; /* 0: accept any size; else a configure of this size */
    uint32_t frames;                  /* frame() calls */
    uint32_t presented;               /* snapshots shown */
    uint32_t resize_requests;
    uint64_t last_sequence; /* of the newest snapshot drawn */
    uint32_t attached, detached;
    char dump[8192];
} fake_presenter;

static void dump_write(void *ctx, const char *s, size_t n)
{
    fake_presenter *p = (fake_presenter *)ctx;
    size_t len = strlen(p->dump);
    if (len + n < sizeof p->dump) {
        memcpy(p->dump + len, s, n);
        p->dump[len + n] = '\0';
    }
}

static ax_result fake_attach(void *self, ax_runtime rt, uint32_t surface, uint64_t native,
                             uint64_t window)
{
    fake_presenter *p = (fake_presenter *)self;
    (void)surface;
    (void)native;
    (void)window;
    p->rt = rt;
    p->attached++;
    return AX_OK;
}

static void fake_detach(void *self, uint32_t surface)
{
    (void)surface;
    ((fake_presenter *)self)->detached++;
}

static ax_result fake_frame(void *self, uint32_t surface)
{
    fake_presenter *p = (fake_presenter *)self;
    const ax_render_ir *ir = ax_ir_mailbox_acquire(ax_rt_surface_mailbox(p->rt, surface));
    p->frames++;
    if (ir == NULL) {
        return AX_OK;
    }
    if (p->configured_w > 0.0f &&
        (ir->logical_width != p->configured_w || ir->logical_height != p->configured_h)) {
        /* A configure of another size: ask the scene for it (decision 54). */
        ax_rt_request q;
        memset(&q, 0, sizeof q);
        q.logical_width = p->configured_w;
        q.logical_height = p->configured_h;
        q.scale = 1.0f;
        q.time_ns = ir->frame_time_ns;
        q.time_is_world_clock = true;
        q.resize = true;
        ax_rt_request_frame(p->rt, surface, &q);
        p->resize_requests++;
        return AX_OK;
    }
    p->last_sequence = ir->sequence;
    p->dump[0] = '\0';
    ax_ir_dump(ir, dump_write, p);
    if (p->auto_present) {
        ax_rt_snapshot_presented(p->rt, surface, ir->sequence, ir->frame_time_ns, true);
        p->presented++;
    }
    return AX_OK;
}

static const ax_rt_presenter_vtbl k_fake = {
    sizeof(ax_rt_presenter_vtbl), fake_attach, fake_detach, NULL, fake_frame, NULL};

/* ---- A pipeline in one thread ---------------------------------------------------------------- */

typedef struct pipeline {
    manual_clock clock;
    ax_world world;
    ax_runtime rt;
    fake_presenter presenter;
    ax_node root, card;
    uint32_t surface;
    uint32_t driver_calls;
    uint64_t driver_time;
} pipeline;

static void driver(void *user, ax_world world, ax_node root, uint64_t t, float w, float h)
{
    (void)w;
    (void)h;
    pipeline *p = (pipeline *)user;
    (void)world;
    (void)root;
    p->driver_calls++;
    p->driver_time = t;
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static ax_rgba opaque(float r, float g, float b)
{
    ax_rgba c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = 1.0f;
    return c;
}

/* A root (dark blue) with one card (white, 10,20 100x50), on a 200x100 surface refreshing every
 * 16 ms. */
static bool pipeline_start(pipeline *p)
{
    ax_world_desc wd;
    ax_rt_desc rd;
    ax_rt_surface_desc sd;
    ax_txn *t;
    memset(p, 0, sizeof *p);
    p->clock.now = 50 * MS;
    memset(&wd, 0, sizeof wd);
    wd.struct_size = sizeof wd;
    wd.clock = read_clock;
    wd.clock_user = &p->clock;
    if (ax_world_create(&wd, &p->world) != AX_OK) {
        return false;
    }
    p->presenter.auto_present = true;
    memset(&rd, 0, sizeof rd);
    rd.world = p->world;
    rd.mode = AX_RUNTIME_SINGLE_THREADED;
    rd.presenter = &k_fake;
    rd.presenter_self = &p->presenter;
    rd.driver = driver;
    rd.driver_user = p;
    if (ax_rt_create(&rd, &p->rt) != AX_OK) {
        return false;
    }
    t = ax_txn_begin(p->world);
    p->root = ax_txn_create(t, AX_NODE_VIEW);
    p->card = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, p->root, AX_PROP_BACKGROUND_COLOR, opaque(0, 0, 0.25f));
    ax_txn_set_dimension(t, p->card, AX_PROP_LEFT, pt(10));
    ax_txn_set_dimension(t, p->card, AX_PROP_TOP, pt(20));
    ax_txn_set_dimension(t, p->card, AX_PROP_WIDTH, pt(100));
    ax_txn_set_dimension(t, p->card, AX_PROP_HEIGHT, pt(50));
    ax_txn_set_color(t, p->card, AX_PROP_BACKGROUND_COLOR, opaque(1, 1, 1));
    ax_txn_append_child(t, p->root, p->card);
    if (ax_txn_commit(t, NULL) != AX_OK) {
        return false;
    }
    memset(&sd, 0, sizeof sd);
    sd.width = 200;
    sd.height = 100;
    sd.scale = 1;
    sd.refresh_ns = 16 * MS;
    return ax_rt_add_surface(p->rt, p->root, &sd, &p->surface) == AX_OK;
}

static void pipeline_stop(pipeline *p)
{
    ax_runtime_destroy(p->rt);
    ax_world_destroy(p->world);
}

/* Commits one change of the card's color at the clock's current time; returns its sequence. */
static uint64_t set_card_color(pipeline *p, ax_rgba c)
{
    ax_txn *t = ax_txn_begin(p->world);
    uint64_t seq = 0;
    ax_txn_set_color(t, p->card, AX_PROP_BACKGROUND_COLOR, c);
    return ax_txn_commit(t, &seq) == AX_OK ? seq : 0;
}

/* A transaction that changes nothing any surface shows: a node outside every root. */
static uint64_t detached_change(pipeline *p)
{
    ax_txn *t = ax_txn_begin(p->world);
    uint64_t seq = 0;
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, n, AX_PROP_OPACITY, 0.5f);
    return ax_txn_commit(t, &seq) == AX_OK ? seq : 0;
}

static bool frame_at(pipeline *p, uint64_t t)
{
    p->clock.now = t - 8 * MS; /* the host calls half a refresh before the target */
    return ax_host_frame(p->rt, p->root, t) == AX_OK;
}

static void poll_all(ax_world w)
{
    ax_world_event e;
    do {
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        (void)ax_world_poll_event(w, &e);
    } while (e.type != AX_WORLD_EVENT_NONE);
}

/* ---- Tests ----------------------------------------------------------------------------------- */

static const char *const k_first =
    "render_ir 1.0 size 200x100 time_ns 100000000 seq 1 background (0 0 0.25 1)\n"
    "damage full\n"
    "layer 0 root items 2\n"
    "  rect (0 0 200 100) color (0 0 0.25 1)\n"
    "  rect (10 20 100 50) color (1 1 1 1)\n";

static const char *const k_red =
    "render_ir 1.0 size 200x100 time_ns 132000000 seq 2 background (0 0 0.25 1)\n"
    "damage full\n"
    "layer 0 root items 2\n"
    "  rect (0 0 200 100) color (0 0 0.25 1)\n"
    "  rect (10 20 100 50) color (1 0 0 1)\n";

static bool dump_is(const fake_presenter *f, const char *expected)
{
    if (strcmp(f->dump, expected) != 0) {
        printf("got:\n%s\nexpected:\n%s\n", f->dump, expected);
        return false;
    }
    return true;
}

AX_TEST(transactions_apply_at_the_frame_their_deadline_allows)
{
    static pipeline p;
    uint64_t red, blue;
    ax_rt_counters c;
    AX_REQUIRE(pipeline_start(&p));
    /* Frame at T = 100 ms: its deadline is T - refresh = 84 ms; the tree committed at 50 ms. */
    AX_REQUIRE(frame_at(&p, 100 * MS));
    AX_CHECK(dump_is(&p.presenter, k_first));
    AX_CHECK_EQ_U(p.driver_calls, 1);
    AX_CHECK_EQ_U(p.driver_time, 100 * MS);

    /* Committed at 110 ms: after the next frame's deadline (116 - 16 = 100), so it waits. */
    p.clock.now = 110 * MS;
    red = set_card_color(&p, opaque(1, 0, 0));
    AX_REQUIRE(ax_host_frame(p.rt, p.root, 116 * MS) == AX_OK);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(p.world), red - 1u);
    AX_CHECK_EQ_U(p.presenter.last_sequence, 1); /* nothing new to record */

    /* At the frame after, its deadline (116) has passed the commit. */
    p.clock.now = 120 * MS;
    blue = set_card_color(&p, opaque(0, 0, 1)); /* committed at 120: after 132 - 16 = 116 */
    AX_REQUIRE(ax_host_frame(p.rt, p.root, 132 * MS) == AX_OK);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(p.world), red);
    AX_CHECK(dump_is(&p.presenter, k_red));
    p.clock.now = 140 * MS;
    AX_REQUIRE(ax_host_frame(p.rt, p.root, 148 * MS) == AX_OK);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(p.world), blue);

    ax_rt_get_counters(p.rt, &c);
    AX_CHECK_EQ_U(c.ticks, 4);
    AX_CHECK_EQ_U(c.snapshots, 3);
    pipeline_stop(&p);
}

AX_TEST(the_mirror_is_published_after_layout)
{
    static pipeline p;
    ax_txn *t;
    uint64_t seq = 0;
    float opacity = 0.0f;
    ax_rectf f;
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    t = ax_txn_begin(p.world);
    ax_txn_set_dimension(t, p.card, AX_PROP_LEFT, pt(60));
    ax_txn_set_f32(t, p.card, AX_PROP_OPACITY, 0.5f);
    AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
    AX_REQUIRE(frame_at(&p, 132 * MS));
    /* One sync shows the transaction's values and the frame layout computed from it, together. */
    AX_CHECK_EQ_U(ax_world_mirror_sync(p.world), seq);
    AX_REQUIRE(ax_mirror_get_f32(p.world, p.card, AX_PROP_OPACITY, &opacity) == AX_OK);
    AX_REQUIRE(ax_mirror_get_frame(p.world, p.card, &f) == AX_OK);
    AX_CHECK(opacity == 0.5f);
    AX_CHECK(f.x == 60.0f && f.y == 20.0f && f.w == 100.0f && f.h == 50.0f);
    AX_REQUIRE(ax_mirror_get_frame(p.world, p.root, &f) == AX_OK);
    AX_CHECK(f.w == 200.0f && f.h == 100.0f);
    pipeline_stop(&p);
}

AX_TEST(a_configure_of_another_size_is_recorded_in_the_same_frame)
{
    static pipeline p;
    ax_rt_counters c;
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    /* The window was resized to 300x150: the presenter asks, the scene records, it presents. */
    p.presenter.configured_w = 300;
    p.presenter.configured_h = 150;
    AX_REQUIRE(frame_at(&p, 116 * MS));
    AX_CHECK_EQ_U(p.presenter.resize_requests, 1);
    AX_CHECK(strstr(p.presenter.dump, "size 300x150") != NULL);
    AX_CHECK(strstr(p.presenter.dump, "rect (0 0 300 150)") != NULL);
    ax_rt_get_counters(p.rt, &c);
    AX_CHECK_EQ_U(c.resize_ticks, 1);
    pipeline_stop(&p);
}

/* ---- on_presented ---------------------------------------------------------------------------- */

typedef struct calls {
    uint32_t count;
    uint64_t seq[16];
    ax_presented_status status[16];
    uint64_t time[16];
} calls;

static void on_presented(void *user, uint64_t seq, ax_presented_status status, uint64_t t)
{
    calls *c = (calls *)user;
    if (c->count < 16) {
        c->seq[c->count] = seq;
        c->status[c->count] = status;
        c->time[c->count] = t;
    }
    c->count++;
}

static uint64_t rejected_change(pipeline *p)
{
    ax_txn *t = ax_txn_begin(p->world);
    uint64_t seq = 0;
    ax_node ghost;
    ghost.h = ax_handle_make(5000u, 1u);
    ax_txn_set_f32(t, ghost, AX_PROP_OPACITY, 0.5f);
    return ax_txn_commit(t, &seq) == AX_OK ? seq : 0;
}

AX_TEST(on_presented_comes_in_order_when_the_frame_is_shown)
{
    static pipeline p;
    calls c;
    uint64_t a, bad, b;
    memset(&c, 0, sizeof c);
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    p.presenter.auto_present = false; /* the test says when frames reach the screen */

    p.clock.now = 101 * MS;
    a = set_card_color(&p, opaque(1, 0, 0));
    bad = rejected_change(&p);
    b = set_card_color(&p, opaque(0, 1, 0));
    AX_REQUIRE(ax_txn_on_presented(p.world, b, on_presented, &c) == AX_OK);
    AX_REQUIRE(ax_txn_on_presented(p.world, bad, on_presented, &c) == AX_OK);
    AX_REQUIRE(ax_txn_on_presented(p.world, a, on_presented, &c) == AX_OK);
    AX_REQUIRE(frame_at(&p, 132 * MS));
    poll_all(p.world);
    AX_CHECK_EQ_U(c.count, 0); /* recorded, not shown */

    ax_rt_snapshot_presented(p.rt, p.surface, p.presenter.last_sequence, 131 * MS, true);
    (void)ax_rt_scene_step(p.rt);
    poll_all(p.world);
    AX_REQUIRE(c.count == 3);
    AX_CHECK_EQ_U(c.seq[0], a);
    AX_CHECK_EQ_U(c.status[0], AX_PRESENTED);
    AX_CHECK_EQ_U(c.time[0], 131 * MS);
    AX_CHECK_EQ_U(c.seq[1], bad);
    AX_CHECK_EQ_U(c.status[1], AX_PRESENTED_REJECTED);
    AX_CHECK_EQ_U(c.seq[2], b);
    AX_CHECK_EQ_U(c.status[2], AX_PRESENTED);
    pipeline_stop(&p);
}

AX_TEST(on_presented_for_a_change_no_window_shows_comes_when_applied)
{
    static pipeline p;
    calls c;
    uint64_t s;
    memset(&c, 0, sizeof c);
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    p.presenter.auto_present = false;
    p.clock.now = 101 * MS;
    s = detached_change(&p);
    AX_REQUIRE(ax_txn_on_presented(p.world, s, on_presented, &c) == AX_OK);
    AX_REQUIRE(frame_at(&p, 132 * MS));
    poll_all(p.world);
    AX_REQUIRE(c.count == 1);
    AX_CHECK_EQ_U(c.status[0], AX_PRESENTED);
    pipeline_stop(&p);
}

AX_TEST(hidden_windows_do_not_hold_back_and_visible_again_they_do)
{
    static pipeline p;
    calls c;
    uint64_t hidden_change, later;
    memset(&c, 0, sizeof c);
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    p.presenter.auto_present = false;

    /* Hidden: a change it records but never shows does not hold the callback. */
    ax_rt_set_surface_visible(p.rt, p.surface, false);
    p.clock.now = 101 * MS;
    hidden_change = set_card_color(&p, opaque(1, 0, 0));
    AX_REQUIRE(ax_txn_on_presented(p.world, hidden_change, on_presented, &c) == AX_OK);
    AX_REQUIRE(frame_at(&p, 132 * MS));
    poll_all(p.world);
    AX_REQUIRE(c.count == 1);
    AX_CHECK_EQ_U(c.status[0], AX_PRESENTED);

    /* Visible again with that change still not shown: it holds back what comes after. */
    ax_rt_set_surface_visible(p.rt, p.surface, true);
    p.clock.now = 133 * MS;
    later = detached_change(&p);
    AX_REQUIRE(ax_txn_on_presented(p.world, later, on_presented, &c) == AX_OK);
    AX_REQUIRE(frame_at(&p, 164 * MS));
    poll_all(p.world);
    AX_CHECK_EQ_U(c.count, 1);
    ax_rt_snapshot_presented(p.rt, p.surface, p.presenter.last_sequence, 163 * MS, true);
    (void)ax_rt_scene_step(p.rt);
    poll_all(p.world);
    AX_REQUIRE(c.count == 2);
    AX_CHECK_EQ_U(c.seq[1], later);
    AX_CHECK_EQ_U(c.status[1], AX_PRESENTED);
    pipeline_stop(&p);
}

AX_TEST(a_closed_window_stops_holding_back)
{
    static pipeline p;
    calls c;
    uint64_t s;
    memset(&c, 0, sizeof c);
    AX_REQUIRE(pipeline_start(&p));
    AX_REQUIRE(frame_at(&p, 100 * MS));
    p.presenter.auto_present = false;
    p.clock.now = 101 * MS;
    s = set_card_color(&p, opaque(1, 0, 0));
    AX_REQUIRE(ax_txn_on_presented(p.world, s, on_presented, &c) == AX_OK);
    AX_REQUIRE(frame_at(&p, 132 * MS));
    poll_all(p.world);
    AX_CHECK_EQ_U(c.count, 0);
    AX_REQUIRE(ax_rt_remove_surface(p.rt, p.surface) == AX_OK);
    AX_CHECK_EQ_U(p.presenter.detached, 1);
    poll_all(p.world);
    AX_REQUIRE(c.count == 1);
    AX_CHECK_EQ_U(c.status[0], AX_PRESENTED);
    pipeline_stop(&p);
}

AX_TEST(host_frames_are_checked)
{
    static pipeline p;
    ax_node other;
    AX_REQUIRE(pipeline_start(&p));
    other.h = ax_handle_make(77u, 1u);
    AX_CHECK_RESULT(ax_host_frame(p.rt, other, 100 * MS), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_host_frame((ax_runtime){0}, p.root, 100 * MS), AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(ax_rt_surface_of_root(p.rt, p.root), p.surface);
    AX_CHECK_RESULT(ax_rt_add_surface(p.rt, p.root, &(ax_rt_surface_desc){0}, NULL),
                    AX_ERR_INVALID_ARGUMENT); /* already attached */
    AX_CHECK(ax_jobs_is_inline(ax_rt_jobs(p.rt)));
    pipeline_stop(&p);
}
