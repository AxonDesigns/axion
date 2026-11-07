/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The app thread's mirror: what it shows, when, and that a reader on another thread never sees a
 * transaction in part while the scene applies.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
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

static ax_dimension points(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* One frame on the (same) scene thread: apply everything, publish. */
static void frame(ax_world w)
{
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_publish_mirror(w);
}

static float opacity_of(ax_world w, ax_node n)
{
    float f = -1.0f;
    if (ax_mirror_get_f32(w, n, AX_PROP_OPACITY, &f) != AX_OK) {
        return -1.0f;
    }
    return f;
}

/* ---- What the mirror shows, and when --------------------------------------------------------- */

AX_TEST(nothing_is_visible_before_a_publication_and_a_sync)
{
    ax_world w = make_world(NULL);
    ax_txn *t;
    ax_node n;
    uint64_t seq = 0;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK_EQ_U(ax_world_mirror_sync(w), 0);

    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK_EQ_U(ax_world_mirror_sync(w), 0); /* applied, not published */
    AX_CHECK(!ax_mirror_is_live(w, n));

    AX_CHECK(ax_world_publish_mirror(w));
    AX_CHECK(!ax_mirror_is_live(w, n)); /* published, not latched */
    AX_CHECK_EQ_U(ax_world_mirror_sync(w), seq);
    AX_CHECK(ax_mirror_is_live(w, n));
    AX_CHECK(!ax_world_publish_mirror(w)); /* nothing changed since */
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(values_parents_and_errors)
{
    ax_world w = make_world(NULL);
    ax_txn *t;
    ax_node parent;
    ax_node child;
    ax_node stale;
    ax_node again;
    float f;
    ax_dimension d;
    ax_rgba c;
    ax_transform2d m;
    ax_rectf frame_rect;
    ax_transform2d shifted = {1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 5.0f};
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));

    t = ax_txn_begin(w);
    parent = ax_txn_create(t, AX_NODE_VIEW);
    child = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_append_child(t, parent, child);
    ax_txn_set_f32(t, child, AX_PROP_OPACITY, 0.25f);
    ax_txn_set_dimension(t, child, AX_PROP_LEFT, points(-3.0f));
    ax_txn_set_color(t, child, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(1.0f, 1.0f, 1.0f, 0.5f));
    ax_txn_set_transform(t, child, AX_PROP_TRANSFORM, shifted);
    ax_txn_set_f32(t, child, AX_PROP_CORNER_RADIUS, 6.0f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);

    AX_CHECK_EQ_U(ax_mirror_parent(w, child).h, parent.h);
    AX_CHECK(AX_HANDLE_IS_NULL(ax_mirror_parent(w, parent)));
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, AX_PROP_OPACITY, &f), AX_OK);
    AX_CHECK(f == 0.25f);
    AX_CHECK_RESULT(ax_mirror_get_dimension(w, child, AX_PROP_LEFT, &d), AX_OK);
    AX_CHECK(d.value == -3.0f && d.unit == AX_UNIT_POINTS);
    AX_CHECK_RESULT(ax_mirror_get_dimension(w, child, AX_PROP_WIDTH, &d), AX_OK);
    AX_CHECK_EQ_U(d.unit, AX_UNIT_AUTO);
    AX_CHECK_RESULT(ax_mirror_get_color(w, child, AX_PROP_BACKGROUND_COLOR, &c), AX_OK);
    AX_CHECK(c.a == 0.5f && c.r == 0.5f); /* premultiplied */
    AX_CHECK_RESULT(ax_mirror_get_transform(w, child, AX_PROP_TRANSFORM, &m), AX_OK);
    AX_CHECK(m.tx == 4.0f && m.ty == 5.0f && m.a == 1.0f);
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, AX_PROP_CORNER_RADIUS, &f), AX_OK);
    AX_CHECK(f == 6.0f);
    AX_CHECK_RESULT(ax_mirror_get_frame(w, child, &frame_rect), AX_OK);
    AX_CHECK(frame_rect.w == 0.0f); /* until layout exists */

    /* Errors. */
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, AX_PROP_WIDTH, &f), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, 0x0999, &f), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, AX_PROP_FIRST_DYNAMIC, &f), AX_ERR_UNSUPPORTED);

    /* The shorthand's rule, and a removal followed by the slot's reuse. */
    t = ax_txn_begin(w);
    ax_txn_set_f32(t, child, AX_PROP_CORNER_RADIUS_TOP_LEFT, 1.0f);
    ax_txn_remove(t, parent);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    /* Still the latched publication: the old state. */
    AX_CHECK(ax_mirror_is_live(w, parent));
    (void)ax_world_mirror_sync(w);
    AX_CHECK(!ax_mirror_is_live(w, parent));
    AX_CHECK(!ax_mirror_is_live(w, child));
    AX_CHECK_RESULT(ax_mirror_get_f32(w, child, AX_PROP_OPACITY, &f), AX_ERR_INVALID_HANDLE);
    stale = child;

    t = ax_txn_begin(w);
    again = ax_txn_create(t, AX_NODE_VIEW); /* a recycled slot, next generation */
    ax_txn_set_f32(t, again, AX_PROP_CORNER_RADIUS_TOP_LEFT, 1.0f);
    ax_txn_set_f32(t, again, AX_PROP_CORNER_RADIUS_TOP_RIGHT, 2.0f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);
    AX_CHECK(ax_mirror_is_live(w, again));
    AX_CHECK(ax_handle_slot(again.h) == ax_handle_slot(parent.h) ||
             ax_handle_slot(again.h) == ax_handle_slot(stale.h));
    AX_CHECK(!ax_mirror_is_live(w, stale));
    AX_CHECK(!ax_mirror_is_live(w, parent));
    AX_CHECK_RESULT(ax_mirror_get_f32(w, again, AX_PROP_CORNER_RADIUS, &f),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(strstr(ax_last_error()->message.ptr, "longhands differ") != NULL);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(a_held_copy_catches_up_over_several_publications)
{
    enum { NODES = 600 };
    static ax_node nodes[NODES];
    ax_world w = make_world(NULL);
    ax_txn *t;
    int i;
    int round;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    for (i = 0; i < NODES; ++i) {
        nodes[i] = ax_txn_create(t, AX_NODE_VIEW);
    }
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);

    /* The app holds its copy while the scene publishes ten times, each changing other nodes, so
     * each copy the scene writes is behind by a different set of changes. */
    for (round = 0; round < 10; ++round) {
        t = ax_txn_begin(w);
        for (i = round; i < NODES; i += 10) {
            ax_txn_set_f32(t, nodes[i], AX_PROP_OPACITY, (float)(round + 1) / 20.0f);
        }
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        frame(w);
        AX_CHECK(opacity_of(w, nodes[round]) == 1.0f); /* the held copy */
        if (round % 3 == 2) {
            (void)ax_world_mirror_sync(w); /* sometimes the app takes one in between */
        }
    }
    (void)ax_world_mirror_sync(w);
    for (i = 0; i < NODES; ++i) {
        if (opacity_of(w, nodes[i]) != (float)(i % 10 + 1) / 20.0f) {
            ax_test_fail(__FILE__, __LINE__, "node %d: %g", i, (double)opacity_of(w, nodes[i]));
            break;
        }
    }
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(layout_results_reach_the_mirror_through_touch)
{
    ax_world w = make_world(NULL);
    ax_txn *t;
    ax_node n;
    ax_scene *scene;
    ax_rectf r;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    /* What layout will do: write the frame, mark the node, publish with the frame. */
    scene = ax_world_scene(w);
    scene->store.layout[ax_node_store_row(&scene->store, n)].frame = ax_rect_make(1, 2, 30, 40);
    ax_world_touch_node(w, n);
    AX_CHECK(ax_world_publish_mirror(w));
    (void)ax_world_mirror_sync(w);
    AX_CHECK_RESULT(ax_mirror_get_frame(w, n, &r), AX_OK);
    AX_CHECK(r.x == 1.0f && r.y == 2.0f && r.w == 30.0f && r.h == 40.0f);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Atomicity: a reader on the app thread while the scene thread applies -------------------- */

enum { ATOM_NODES = 32, ATOM_TXNS = 1500 };

typedef struct atom_ctx {
    ax_world world;
    ax_node group[2];
    ax_node nodes[ATOM_NODES];
    ax_node mover;
    ax_node marker[ATOM_TXNS + 1]; /* the marker transaction k creates */
    bool valid[ATOM_TXNS + 1];
    ax_atomic_u32 reads; /* the reader's syncs, so the scene can let it keep up */
} atom_ctx;

static float value_of(uint32_t k)
{
    return (float)(k % 97u) / 97.0f;
}

static int atom_scene_main(void *arg)
{
    atom_ctx *ctx = (atom_ctx *)arg;
    uint64_t deadline = 1;
    uint32_t frames = 0;
    ax_world_bind_scene_thread(ctx->world);
    while (deadline <= ATOM_TXNS) {
        /* One transaction per frame, sometimes two. */
        deadline += (deadline % 5u == 0u) ? 2u : 1u;
        (void)ax_world_apply_pending(ctx->world, deadline - 1u);
        (void)ax_world_publish_mirror(ctx->world);
        /* Every 50 frames, let the reader catch up (at most 1 s), so it sees many publications
         * however the threads are scheduled. */
        if (++frames % 50u == 0u) {
            uint32_t before = ax_atomic_load_u32(&ctx->reads, AX_MO_ACQUIRE);
            uint64_t give_up = ax_time_monotonic_ns() + 1000000000ull;
            while (ax_atomic_load_u32(&ctx->reads, AX_MO_ACQUIRE) == before &&
                   ax_time_monotonic_ns() < give_up) {
                ax_thread_yield();
            }
        }
    }
    (void)ax_world_apply_pending(ctx->world, UINT64_MAX);
    (void)ax_world_publish_mirror(ctx->world);
    return 0;
}

AX_TEST(a_reader_never_sees_part_of_a_transaction)
{
    static atom_ctx ctx;
    manual_clock clock = {0};
    ax_thread scene;
    ax_txn *t;
    uint32_t k;
    uint32_t i;
    uint32_t last_valid = 0;
    uint64_t seen = 0;
    uint32_t distinct = 0;
    uint32_t bad = 0;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    memset(&ctx, 0, sizeof ctx);
    ax_atomic_init_u32(&ctx.reads, 0);
    ctx.world = make_world(&clock);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(ctx.world));

    /* Transaction k (commit time k) sets every node's opacity to value_of(k), moves the mover
     * under group[k % 2], creates marker[k] with width k and removes the previous marker. One in
     * seven ends with a stale handle and is rejected whole. */
    for (k = 1; k <= ATOM_TXNS; ++k) {
        clock.now = k;
        t = ax_txn_begin(ctx.world);
        AX_REQUIRE(t != NULL);
        if (k == 1) {
            ctx.group[0] = ax_txn_create(t, AX_NODE_VIEW);
            ctx.group[1] = ax_txn_create(t, AX_NODE_VIEW);
            ctx.mover = ax_txn_create(t, AX_NODE_VIEW);
            for (i = 0; i < ATOM_NODES; ++i) {
                ctx.nodes[i] = ax_txn_create(t, AX_NODE_VIEW);
                ax_txn_append_child(t, ctx.group[i % 2], ctx.nodes[i]);
            }
        }
        for (i = 0; i < ATOM_NODES; ++i) {
            ax_txn_set_f32(t, ctx.nodes[i], AX_PROP_OPACITY, value_of(k));
        }
        ax_txn_append_child(t, ctx.group[k % 2], ctx.mover);
        ctx.marker[k] = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_dimension(t, ctx.marker[k], AX_PROP_WIDTH, points((float)k));
        ax_txn_append_child(t, ctx.group[0], ctx.marker[k]);
        if (last_valid != 0) {
            ax_txn_remove(t, ctx.marker[last_valid]);
        }
        ctx.valid[k] = k == 1 || k % 7u != 0u;
        if (!ctx.valid[k]) {
            ax_node stale;
            stale.h = ax_handle_make(0xFFFFFu, 1);
            ax_txn_set_f32(t, stale, AX_PROP_OPACITY, 0.5f);
        } else {
            last_valid = k;
        }
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    }

    AX_REQUIRE(ax_thread_create(&scene, atom_scene_main, &ctx) == AX_OK);
    while (seen < ATOM_TXNS && bad == 0) {
        uint64_t seq = ax_world_mirror_sync(ctx.world);
        uint32_t expect;
        ax_dimension width;
        ax_atomic_fetch_add_u32(&ctx.reads, 1, AX_MO_RELEASE);
        if (seq == 0) {
            ax_thread_yield();
            continue;
        }
        if (seq != seen) {
            distinct++;
            seen = seq;
        }
        /* The state the newest valid transaction up to seq left, and nothing else. */
        for (expect = (uint32_t)seq; !ctx.valid[expect]; --expect) {
        }
        for (i = 0; i < ATOM_NODES; ++i) {
            if (opacity_of(ctx.world, ctx.nodes[i]) != value_of(expect)) {
                bad++;
            }
        }
        if (ax_mirror_parent(ctx.world, ctx.mover).h != ctx.group[expect % 2].h) {
            bad++;
        }
        if (ax_mirror_get_dimension(ctx.world, ctx.marker[expect], AX_PROP_WIDTH, &width) !=
                AX_OK ||
            width.value != (float)expect) {
            bad++;
        }
        for (k = 1; k < expect; ++k) {
            if (ax_mirror_is_live(ctx.world, ctx.marker[k])) {
                bad++; /* removed earlier */
            }
        }
        if (seq < ATOM_TXNS && ax_mirror_is_live(ctx.world, ctx.marker[seq + 1u])) {
            bad++; /* from the future */
        }
        if (seq % 7u == 0u && ax_mirror_is_live(ctx.world, ctx.marker[seq])) {
            bad++; /* a rejected transaction's node */
        }
        if (bad != 0) {
            ax_test_fail(__FILE__, __LINE__, "inconsistent mirror at seq %llu (expected %u)",
                         (unsigned long long)seq, expect);
        }
    }
    ax_thread_join(&scene);
    AX_CHECK_EQ_U(bad, 0);
    AX_CHECK_EQ_U(seen, ATOM_TXNS);
    AX_CHECK(distinct > 10); /* the reader really ran alongside the scene */
    AX_CHECK_EQ_U(ax_world_last_applied_seq(ctx.world), ATOM_TXNS);
    ax_world_bind_scene_thread(ctx.world);
    AX_CHECK_RESULT(ax_world_destroy(ctx.world), AX_OK);
}
