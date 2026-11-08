/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transactions on the app thread: recording, sticky errors, handle reservation, the queue to the
 * scene thread and the way objects and results come back.
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/core/ax_world_sdk.h"

/* ---- Helpers --------------------------------------------------------------------------------- */

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_manual_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

static ax_world make_world(manual_clock *clock, const ax_allocator *allocator)
{
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.allocator = allocator;
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

static ax_transition spring(void)
{
    ax_transition tr;
    memset(&tr, 0, sizeof tr);
    tr.struct_size = sizeof tr;
    tr.kind = AX_TRANSITION_SPRING;
    tr.duration = 0.4f;
    tr.bounce = 0.2f;
    return tr;
}

/* Takes the next committed transaction on the (same) scene thread and marks it applied. */
static ax_txn *take_ok(ax_world w)
{
    ax_txn *t = ax_world_take_txn(w);
    if (t != NULL) {
        ax_world_finish_txn(w, t, AX_OK, 0, NULL);
    }
    return t;
}

/* ---- The world ------------------------------------------------------------------------------- */

AX_TEST(worlds_are_handles_and_use_their_clock)
{
    manual_clock clock = {12345};
    ax_world w = make_world(&clock, NULL);
    ax_world_desc small;
    ax_world other;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK_EQ_U(ax_world_now(w), 12345);
    clock.now = 99;
    AX_CHECK_EQ_U(ax_world_now(w), 99);
    AX_CHECK(ax_world_scene(w) != NULL);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_ERR_INVALID_HANDLE);
    AX_CHECK(ax_txn_begin(w) == NULL);
    AX_CHECK(ax_last_error()->code == AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(ax_world_now(w), 0);

    memset(&small, 0, sizeof small);
    small.struct_size = 8;
    AX_CHECK_RESULT(ax_world_create(&small, &other), AX_ERR_ABI_MISMATCH);
    AX_CHECK(AX_HANDLE_IS_NULL(other));

    /* NULL desc: the defaults, and the monotonic clock. */
    AX_REQUIRE(ax_world_create(NULL, &other) == AX_OK);
    AX_CHECK(ax_world_now(other) > 0);
    AX_CHECK_RESULT(ax_world_destroy(other), AX_OK);
}

AX_TEST(srgb_colors_are_stored_linear_and_premultiplied)
{
    ax_rgba c = ax_rgba_from_srgb8(255, 128, 0, 128);
    float alpha = 128.0f / 255.0f;
    AX_CHECK_NEAR_F(c.a, alpha, 1e-6);
    AX_CHECK_NEAR_F(c.r, 1.0f * alpha, 1e-6);
    AX_CHECK_NEAR_F(c.g, 0.2158605f * alpha, 1e-5); /* sRGB 128 is 0.2159 in linear light */
    AX_CHECK_NEAR_F(c.b, 0.0f, 1e-7);
}

/* ---- Recording ------------------------------------------------------------------------------- */

AX_TEST(commands_reach_the_scene_in_order_with_their_fields)
{
    manual_clock clock = {1000};
    ax_world w = make_world(&clock, NULL);
    ax_txn *t;
    ax_node list;
    ax_node card;
    ax_transition tr = spring();
    ax_transform2d m = {1.0f, 0.0f, 0.0f, 1.0f, 8.0f, 9.0f};
    uint64_t seq = 0;
    ax_txn_info info;
    const ax_txn_cmd *cmd = NULL;
    const ax_txn *peeked;
    uint32_t n = 0;
    uint16_t ops[16];
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));

    t = ax_txn_begin(w);
    AX_REQUIRE(t != NULL);
    list = ax_txn_create(t, AX_NODE_VIEW);
    card = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(list.h, ax_handle_make(0, 1)); /* fresh slots, generation 1 */
    AX_CHECK_EQ_U(card.h, ax_handle_make(1, 1));
    ax_txn_set_dimension(t, card, AX_PROP_WIDTH, points(120.0f));
    ax_txn_set_color(t, card, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb8(32, 36, 48, 255));
    ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 16.0f);
    ax_txn_set_transform(t, card, AX_PROP_TRANSFORM, m);
    ax_txn_append_child(t, list, card);
    ax_txn_set_transition(t, card, AX_PROP_CORNER_RADIUS, &tr); /* expands to four */
    ax_txn_set_animated(t, false);
    ax_txn_remove(t, card);
    clock.now = 2000;
    AX_CHECK_RESULT(ax_txn_commit(t, &seq), AX_OK);
    AX_CHECK_EQ_U(seq, 1);

    peeked = ax_world_peek_txn(w);
    AX_REQUIRE(peeked != NULL);
    ax_txn_get_info(peeked, &info);
    AX_CHECK_EQ_U(info.seq, 1);
    AX_CHECK_EQ_U(info.commit_time, 2000);
    AX_CHECK_EQ_U(info.command_count, 12);
    AX_CHECK_EQ_U(info.create_count, 2);
    AX_CHECK(!info.animated);
    while ((cmd = ax_txn_next_cmd(&info, cmd)) != NULL) {
        AX_REQUIRE(ax_txn_cmd_valid(&info, cmd));
        AX_REQUIRE(n < 16);
        ops[n] = cmd->op;
        if (n == 2) {
            const ax_txn_cmd_set *s = (const ax_txn_cmd_set *)(const void *)cmd;
            AX_CHECK_EQ_U(s->prop, AX_PROP_WIDTH);
            AX_CHECK_EQ_U(s->type, AX_VALUE_DIMENSION);
            AX_CHECK_EQ_U(s->node.h, card.h);
            AX_CHECK(s->value.dimension.value == 120.0f);
        }
        if (n == 5) {
            const ax_txn_cmd_set *s = (const ax_txn_cmd_set *)(const void *)cmd;
            AX_CHECK(s->value.transform2d.ty == 9.0f);
        }
        if (n == 6) {
            const ax_txn_cmd_insert *ins = (const ax_txn_cmd_insert *)(const void *)cmd;
            AX_CHECK_EQ_U(ins->parent.h, list.h);
            AX_CHECK_EQ_U(ins->child.h, card.h);
            AX_CHECK(AX_HANDLE_IS_NULL(ins->before));
        }
        if (n >= 7 && n <= 10) {
            const ax_txn_cmd_transition *tc = (const ax_txn_cmd_transition *)(const void *)cmd;
            AX_CHECK_EQ_U(tc->prop, AX_PROP_CORNER_RADIUS_TOP_LEFT + (n - 7u));
            AX_CHECK_EQ_U(tc->kind, AX_TRANSITION_SPRING);
            AX_CHECK(tc->bounce == 0.2f);
        }
        n++;
    }
    AX_REQUIRE(n == 12);
    {
        static const uint16_t expected[] = {
            AX_TXN_OP_CREATE,     AX_TXN_OP_CREATE,     AX_TXN_OP_SET,        AX_TXN_OP_SET,
            AX_TXN_OP_SET,        AX_TXN_OP_SET,        AX_TXN_OP_INSERT,     AX_TXN_OP_TRANSITION,
            AX_TXN_OP_TRANSITION, AX_TXN_OP_TRANSITION, AX_TXN_OP_TRANSITION, AX_TXN_OP_REMOVE};
        uint32_t i;
        for (i = 0; i < 12; ++i) {
            AX_CHECK_EQ_U(ops[i], expected[i]);
        }
    }
    AX_CHECK(take_ok(w) == peeked);
    AX_CHECK(ax_world_peek_txn(w) == NULL);
    AX_CHECK(ax_world_take_txn(w) == NULL);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), 1);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(sequences_follow_commit_order)
{
    ax_world w = make_world(NULL, NULL);
    uint64_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK_EQ_U(ax_world_last_committed_seq(w), 0);
    for (i = 1; i <= 6; ++i) {
        ax_txn *t = ax_txn_begin(w);
        uint64_t seq = 0;
        AX_REQUIRE(t != NULL);
        if (i % 2 == 0) {
            (void)ax_txn_create(t, AX_NODE_VIEW);
        }
        AX_CHECK_RESULT(ax_txn_commit(t, &seq), AX_OK); /* empty ones too */
        AX_CHECK_EQ_U(seq, i);
        AX_CHECK_EQ_U(ax_world_last_committed_seq(w), i);
    }
    /* An aborted transaction and one that fails to record take no sequence. */
    ax_txn_abort(ax_txn_begin(w));
    {
        ax_txn *t = ax_txn_begin(w);
        const ax_node none = AX_HANDLE_NULL_INIT;
        ax_txn_remove(t, none);
        AX_CHECK(ax_txn_commit(t, NULL) != AX_OK);
    }
    AX_CHECK_EQ_U(ax_world_last_committed_seq(w), 6);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), 0);
    for (i = 1; i <= 6; ++i) {
        ax_txn *t = take_ok(w);
        ax_txn_info info;
        AX_REQUIRE(t != NULL);
        ax_txn_get_info(t, &info);
        AX_CHECK_EQ_U(info.seq, i);
    }
    AX_CHECK(ax_world_take_txn(w) == NULL);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(recording_errors_are_sticky_and_nothing_is_queued)
{
    ax_world w = make_world(NULL, NULL);
    ax_txn *t;
    ax_node a;
    ax_node again;
    ax_node null_node = AX_HANDLE_NULL_INIT;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));

    t = ax_txn_begin(w);
    a = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, a, AX_PROP_WIDTH, 3.0f);                   /* command 1: wrong type */
    ax_txn_set_f32(t, a, AX_PROP_OPACITY, 7.0f);                 /* ignored */
    AX_CHECK(AX_HANDLE_IS_NULL(ax_txn_create(t, AX_NODE_VIEW))); /* ignored too */
    AX_CHECK_RESULT(ax_txn_commit(t, NULL), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(strstr(ax_last_error()->message.ptr, "command 1") != NULL);
    AX_CHECK(strstr(ax_last_error()->message.ptr, "width") != NULL);
    AX_CHECK(ax_world_peek_txn(w) == NULL);

    /* The created node never existed: its slot comes back at the next generation. */
    t = ax_txn_begin(w);
    again = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(ax_handle_slot(again.h), ax_handle_slot(a.h));
    AX_CHECK_EQ_U(ax_handle_generation(again.h), ax_handle_generation(a.h) + 1u);
    ax_txn_abort(t);

    /* Each kind of recording error. */
    {
        ax_transition tr = spring();
        ax_transition small = spring();
        struct {
            const char *what;
            ax_result expected;
        } cases[8];
        int i;
        small.struct_size = 8;
        cases[0].what = "null node";
        cases[0].expected = AX_ERR_INVALID_HANDLE;
        cases[1].what = "NaN";
        cases[1].expected = AX_ERR_INVALID_ARGUMENT;
        cases[2].what = "unknown core property";
        cases[2].expected = AX_ERR_INVALID_ARGUMENT;
        cases[3].what = "percentage";
        cases[3].expected = AX_ERR_UNSUPPORTED;
        cases[4].what = "insert into itself";
        cases[4].expected = AX_ERR_INVALID_ARGUMENT;
        cases[5].what = "small transition";
        cases[5].expected = AX_ERR_ABI_MISMATCH;
        cases[6].what = "bad bounce";
        cases[6].expected = AX_ERR_INVALID_ARGUMENT;
        cases[7].what = "unknown kind";
        cases[7].expected = AX_ERR_INVALID_ARGUMENT;
        for (i = 0; i < 8; ++i) {
            ax_node n;
            t = ax_txn_begin(w);
            n = ax_txn_create(t, AX_NODE_VIEW);
            switch (i) {
            case 0:
                ax_txn_set_f32(t, null_node, AX_PROP_OPACITY, 1.0f);
                break;
            case 1:
                ax_txn_set_f32(t, n, AX_PROP_OPACITY, NAN);
                break;
            case 2:
                ax_txn_set_f32(t, n, 0x0777, 1.0f);
                break;
            case 3: {
                ax_dimension d;
                d.value = 50.0f;
                d.unit = AX_UNIT_PERCENT;
                ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, d);
                break;
            }
            case 4:
                ax_txn_append_child(t, n, n);
                break;
            case 5:
                ax_txn_set_transition(t, n, AX_PROP_OPACITY, &small);
                break;
            case 6:
                tr.bounce = 3.0f;
                ax_txn_set_transition(t, n, AX_PROP_OPACITY, &tr);
                break;
            case 7:
                tr = spring();
                tr.kind = 42;
                ax_txn_set_transition(t, n, AX_PROP_OPACITY, &tr);
                break;
            }
            if (ax_txn_commit(t, NULL) != cases[i].expected) {
                ax_test_fail(__FILE__, __LINE__, "case %s: got %s", cases[i].what,
                             ax_result_name(ax_last_error()->code));
            }
        }
    }
    AX_CHECK(ax_world_peek_txn(w) == NULL);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Handle reservation ---------------------------------------------------------------------- */

AX_TEST(aborted_creates_skip_a_generation)
{
    ax_world w = make_world(NULL, NULL);
    ax_txn *t;
    ax_node made[3];
    ax_node next;
    int i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    for (i = 0; i < 3; ++i) {
        made[i] = ax_txn_create(t, AX_NODE_VIEW);
    }
    ax_txn_abort(t);
    t = ax_txn_begin(w);
    /* LIFO: the last slot recycled is handed out first. */
    for (i = 2; i >= 0; --i) {
        next = ax_txn_create(t, AX_NODE_VIEW);
        AX_CHECK_EQ_U(ax_handle_slot(next.h), ax_handle_slot(made[i].h));
        AX_CHECK_EQ_U(ax_handle_generation(next.h), 2);
    }
    next = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(next.h, ax_handle_make(3, 1)); /* then fresh slots again */
    ax_txn_abort(t);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(the_scene_recycles_slots_and_the_last_generation_retires)
{
    ax_world w = make_world(NULL, NULL);
    ax_txn *t;
    ax_node recycled;
    ax_node last;
    ax_node n;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    recycled.h = ax_handle_make(40, 7);
    last.h = ax_handle_make(41, UINT32_MAX);
    ax_world_recycle_handle(w, recycled); /* scene thread: here, the same thread */
    ax_world_recycle_handle(w, last);

    t = ax_txn_begin(w); /* takes them back */
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(n.h, last.h);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(n.h, recycled.h);
    ax_txn_abort(t);

    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(n.h, ax_handle_make(40, 8)); /* slot 41 was retired, not handed out */
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(n.h, ax_handle_make(0, 1));
    ax_txn_abort(t);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Objects and results coming back --------------------------------------------------------- */

AX_TEST(a_warm_transaction_allocates_nothing)
{
    ax_tracking_allocator tracker;
    ax_allocator a;
    ax_world w;
    size_t warm_allocs = 0;
    int round;
    ax_tracking_allocator_init(&tracker, ax_default_allocator());
    a = ax_tracking_allocator_get(&tracker);
    w = make_world(NULL, &a);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    for (round = 0; round < 40; ++round) {
        ax_txn *t = ax_txn_begin(w);
        ax_node n = ax_txn_create(t, AX_NODE_VIEW);
        int i;
        for (i = 0; i < 1000; ++i) {
            ax_txn_set_f32(t, n, AX_PROP_OPACITY, (float)(i % 100) / 100.0f);
        }
        ax_txn_remove(t, n);
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        AX_REQUIRE(take_ok(w) != NULL);
        /* Pretend the scene destroyed the node, so its slot comes back. */
        ax_world_recycle_handle(
            w, (ax_node){ax_handle_make(ax_handle_slot(n.h), ax_handle_generation(n.h) + 1u)});
        if (round == 4) {
            warm_allocs = tracker.alloc_count + tracker.resize_count;
        }
    }
    AX_CHECK_EQ_U(tracker.alloc_count + tracker.resize_count, warm_allocs);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&tracker), 0);
}

AX_TEST(rejections_reach_the_app_thread_and_their_creates_are_recycled)
{
    ax_world w = make_world(NULL, NULL);
    ax_txn *t;
    ax_txn *taken;
    ax_txn_error err;
    uint32_t dropped = 0;
    ax_node created;
    ax_node n;
    uint64_t seq = 0;
    int i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK(!ax_world_poll_txn_error(w, &err, &dropped));

    t = ax_txn_begin(w);
    created = ax_txn_create(t, AX_NODE_VIEW);
    AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
    taken = ax_world_take_txn(w);
    AX_REQUIRE(taken != NULL);
    ax_world_finish_txn(w, taken, AX_ERR_INVALID_HANDLE, 3, "command 3: stale node");
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), seq);

    AX_REQUIRE(ax_world_poll_txn_error(w, &err, &dropped));
    AX_CHECK_EQ_U(err.seq, seq);
    AX_CHECK_RESULT(err.result, AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(err.command, 3);
    AX_CHECK(err.message.len == strlen("command 3: stale node") &&
             memcmp(err.message.ptr, "command 3: stale node", err.message.len) == 0);
    AX_CHECK_EQ_U(dropped, 0);
    AX_CHECK(!ax_world_poll_txn_error(w, &err, NULL));

    /* The rejected object comes back when the next one is taken; its create is recycled. */
    t = ax_txn_begin(w);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(take_ok(w) != NULL);
    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(n.h, ax_handle_make(ax_handle_slot(created.h), 2));
    ax_txn_abort(t);

    /* More rejections than the queue holds: the oldest are kept, the rest counted. */
    for (i = 0; i < (int)AX_TXN_ERROR_QUEUE + 10; ++i) {
        t = ax_txn_begin(w);
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        taken = ax_world_take_txn(w);
        AX_REQUIRE(taken != NULL);
        ax_world_finish_txn(w, taken, AX_ERR_INVALID_ARGUMENT, 0, "bad");
    }
    AX_REQUIRE(ax_world_poll_txn_error(w, &err, &dropped));
    AX_CHECK_EQ_U(dropped, 10);
    for (i = 1; i < (int)AX_TXN_ERROR_QUEUE; ++i) {
        AX_CHECK(ax_world_poll_txn_error(w, &err, &dropped));
        AX_CHECK_EQ_U(dropped, 0);
    }
    AX_CHECK(!ax_world_poll_txn_error(w, &err, &dropped));
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(destroying_a_world_frees_queued_and_open_transactions)
{
    ax_tracking_allocator tracker;
    ax_allocator a;
    ax_world w;
    ax_txn *open_txn;
    int i;
    ax_tracking_allocator_init(&tracker, ax_default_allocator());
    a = ax_tracking_allocator_get(&tracker);
    w = make_world(NULL, &a);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    for (i = 0; i < 5; ++i) {
        ax_txn *t = ax_txn_begin(w);
        ax_txn_set_f32(t, ax_txn_create(t, AX_NODE_VIEW), AX_PROP_OPACITY, 0.5f);
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    }
    AX_REQUIRE(take_ok(w) != NULL);
    open_txn = ax_txn_begin(w);
    AX_REQUIRE(open_txn != NULL);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&tracker), 0);
}

/* Reads every pending rejection, adding the dropped ones each call reports. */
static uint32_t drain_errors(ax_world w)
{
    ax_txn_error err;
    uint32_t total = 0;
    for (;;) {
        uint32_t dropped = 0;
        bool got = ax_world_poll_txn_error(w, &err, &dropped);
        total += dropped;
        if (!got) {
            return total;
        }
        total++;
    }
}

/* ---- Two threads (TSan) ---------------------------------------------------------------------- */

enum { STRESS_TXNS = 20000 };

typedef struct stress_ctx {
    ax_world world;
    uint32_t bad;
    uint64_t last_seq;
} stress_ctx;

static int scene_main(void *arg)
{
    stress_ctx *ctx = (stress_ctx *)arg;
    ax_world_bind_scene_thread(ctx->world);
    while (ctx->last_seq < STRESS_TXNS) {
        ax_txn *t = ax_world_take_txn(ctx->world);
        ax_txn_info info;
        const ax_txn_cmd *cmd = NULL;
        uint32_t count = 0;
        if (t == NULL) {
            ax_thread_yield();
            continue;
        }
        ax_txn_get_info(t, &info);
        if (info.seq != ctx->last_seq + 1u) {
            ctx->bad++;
        }
        ctx->last_seq = info.seq;
        while ((cmd = ax_txn_next_cmd(&info, cmd)) != NULL) {
            if (!ax_txn_cmd_valid(&info, cmd)) {
                ctx->bad++;
                break;
            }
            if (cmd->op == AX_TXN_OP_SET) {
                const ax_txn_cmd_set *s = (const ax_txn_cmd_set *)(const void *)cmd;
                /* Every value is derived from the sequence: a torn or stale buffer shows. */
                if (s->value.f32 != (float)((info.seq * 7u + count) % 100u) / 100.0f) {
                    ctx->bad++;
                }
            }
            count++;
        }
        if (count != info.command_count) {
            ctx->bad++;
        }
        /* Reject one in ten; the app thread recycles their creates. */
        ax_world_finish_txn(ctx->world, t, info.seq % 10u == 0u ? AX_ERR_INVALID_HANDLE : AX_OK, 0,
                            "stress");
    }
    return 0;
}

AX_TEST(the_queue_carries_transactions_between_two_threads)
{
    stress_ctx ctx;
    ax_thread scene;
    uint64_t seq;
    uint32_t errors = 0;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.world = make_world(NULL, NULL);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(ctx.world));
    AX_REQUIRE(ax_thread_create(&scene, scene_main, &ctx) == AX_OK);
    for (seq = 1; seq <= STRESS_TXNS; ++seq) {
        ax_txn *t = ax_txn_begin(ctx.world);
        ax_node n;
        uint32_t k;
        uint32_t sets = (uint32_t)(seq % 13u);
        uint64_t got = 0;
        AX_REQUIRE(t != NULL);
        n = ax_txn_create(t, AX_NODE_VIEW);
        for (k = 1; k <= sets; ++k) {
            ax_txn_set_f32(t, n, AX_PROP_OPACITY, (float)((seq * 7u + k) % 100u) / 100.0f);
        }
        AX_REQUIRE(ax_txn_commit(t, &got) == AX_OK);
        AX_CHECK_EQ_U(got, seq);
        errors += drain_errors(ctx.world);
    }
    /* Keep reading rejections until the scene has caught up, so none overflows the queue. */
    while (ax_world_last_applied_seq(ctx.world) < STRESS_TXNS) {
        errors += drain_errors(ctx.world);
        ax_thread_yield();
    }
    ax_thread_join(&scene);
    errors += drain_errors(ctx.world);
    AX_CHECK_EQ_U(ctx.bad, 0);
    AX_CHECK_EQ_U(ctx.last_seq, STRESS_TXNS);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(ctx.world), STRESS_TXNS);
    AX_CHECK_EQ_U(errors, STRESS_TXNS / 10u);
    ax_world_bind_scene_thread(ctx.world);
    AX_CHECK_RESULT(ax_world_destroy(ctx.world), AX_OK);
}

/* AX_MAX_WORLDS is a guaranteed minimum: that many worlds can exist at once (the headless host
 * runs one per instance). */
AX_TEST(at_least_ax_max_worlds_worlds_can_exist_at_once)
{
    static ax_world worlds[AX_MAX_WORLDS];
    uint32_t i, made = 0;
    for (i = 0; i < AX_MAX_WORLDS; ++i) {
        if (ax_world_create(NULL, &worlds[i]) != AX_OK) {
            break;
        }
        made++;
    }
    AX_CHECK_EQ_U(made, AX_MAX_WORLDS);
    for (i = 0; i < made; ++i) {
        AX_CHECK_RESULT(ax_world_destroy(worlds[i]), AX_OK);
    }
}
