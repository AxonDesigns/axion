/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Applying transactions on the scene thread: atomicity, rejection of the whole transaction,
 * order, deadlines, removal and handle recycling, and an app-to-scene stress test.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"

/* ---- Helpers --------------------------------------------------------------------------------- */

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

static float get_f32(ax_world w, ax_node n, ax_prop p)
{
    ax_value v;
    if (ax_scene_get(ax_world_scene(w), n, p, &v) != AX_OK) {
        return -1.0f;
    }
    return v.f32;
}

static bool is_live(ax_world w, ax_node n)
{
    return ax_node_store_is_live(&ax_world_scene(w)->store, n);
}

static ax_node parent_of(ax_world w, ax_node n)
{
    return ax_node_store_parent(&ax_world_scene(w)->store, n);
}

static uint32_t children(ax_world w, ax_node parent, ax_node *out, uint32_t cap)
{
    const ax_node_store *s = &ax_world_scene(w)->store;
    uint32_t n = 0;
    ax_node c;
    for (c = ax_node_store_first_child(s, parent); !AX_HANDLE_IS_NULL(c);
         c = ax_node_store_next_sibling(s, c)) {
        if (n < cap) {
            out[n] = c;
        }
        n++;
    }
    return n;
}

/* Everything a test can observe about a set of nodes, except dirty flags. */
typedef struct node_state {
    bool live;
    uint64_t parent;
    uint64_t next;
    ax_node_style style;
    ax_node_paint paint;
    ax_node_composite composite;
} node_state;

static void snapshot(ax_world w, const ax_node *nodes, uint32_t n, node_state *out)
{
    const ax_node_store *s = &ax_world_scene(w)->store;
    uint32_t i;
    memset(out, 0, sizeof *out * n);
    for (i = 0; i < n; ++i) {
        uint32_t row = ax_node_store_row(s, nodes[i]);
        if (row == AX_NODE_NONE) {
            continue;
        }
        out[i].live = true;
        out[i].parent = ax_node_store_parent(s, nodes[i]).h;
        out[i].next = ax_node_store_next_sibling(s, nodes[i]).h;
        out[i].style = s->style[row];
        out[i].paint = s->paint[row];
        out[i].composite = s->composite[row];
    }
}

/* ---- Applying -------------------------------------------------------------------------------- */

AX_TEST(a_transaction_applies_and_later_ones_use_its_handles)
{
    ax_world w = make_world(NULL);
    ax_txn *t;
    ax_node window;
    ax_node card;
    ax_node got[4];
    uint64_t seq = 0;
    uint32_t row;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));

    t = ax_txn_begin(w);
    window = ax_txn_create(t, AX_NODE_VIEW);
    card = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_dimension(t, window, AX_PROP_WIDTH, points(800.0f));
    ax_txn_set_dimension(t, window, AX_PROP_HEIGHT, points(600.0f));
    ax_txn_set_color(t, card, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb8(255, 0, 0, 255));
    ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 8.0f);
    ax_txn_append_child(t, window, card);
    AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
    AX_CHECK(!is_live(w, card)); /* nothing happens before the frame boundary */

    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), seq);
    AX_REQUIRE(is_live(w, window) && is_live(w, card));
    AX_CHECK_EQ_U(parent_of(w, card).h, window.h);
    row = ax_node_store_row(&ax_world_scene(w)->store, card);
    AX_CHECK(ax_world_scene(w)->store.paint[row].background.r == 1.0f);
    AX_CHECK(get_f32(w, card, AX_PROP_CORNER_RADIUS_BOTTOM_LEFT) == 8.0f);

    /* A later transaction names the nodes the first one created. */
    t = ax_txn_begin(w);
    ax_txn_set_f32(t, card, AX_PROP_OPACITY, 0.5f);
    ax_txn_insert_before(t, window, ax_txn_create(t, AX_NODE_VIEW), card);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK(get_f32(w, card, AX_PROP_OPACITY) == 0.5f);
    AX_CHECK_EQ_U(children(w, window, got, 4), 2);
    AX_CHECK_EQ_U(got[1].h, card.h);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(transactions_apply_in_sequence_up_to_the_deadline)
{
    manual_clock clock = {100};
    ax_world w = make_world(&clock);
    ax_txn *t;
    ax_node n;
    int i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK); /* at 100 */
    for (i = 1; i <= 3; ++i) {
        clock.now = 100u + 100u * (uint64_t)i; /* 200, 300, 400 */
        t = ax_txn_begin(w);
        ax_txn_set_f32(t, n, AX_PROP_OPACITY, 0.1f * (float)i);
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    }
    AX_CHECK_EQ_U(ax_world_apply_pending(w, 50), 0);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, 250), 2); /* commits at 100 and 200 */
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), 2);
    AX_CHECK(get_f32(w, n, AX_PROP_OPACITY) == 0.1f);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, 300), 1); /* inclusive */
    AX_CHECK(get_f32(w, n, AX_PROP_OPACITY) == 0.2f);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, ax_world_now(w)), 1);
    AX_CHECK(get_f32(w, n, AX_PROP_OPACITY) == 0.3f); /* the later sequence wins */
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), 4);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 0);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Rejection ------------------------------------------------------------------------------- */

typedef struct base_tree {
    ax_node root, a, b, a1, a2, b1;
} base_tree;

static void build_base(ax_world w, base_tree *bt)
{
    ax_txn *t = ax_txn_begin(w);
    bt->root = ax_txn_create(t, AX_NODE_VIEW);
    bt->a = ax_txn_create(t, AX_NODE_VIEW);
    bt->b = ax_txn_create(t, AX_NODE_VIEW);
    bt->a1 = ax_txn_create(t, AX_NODE_VIEW);
    bt->a2 = ax_txn_create(t, AX_NODE_VIEW);
    bt->b1 = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_append_child(t, bt->root, bt->a);
    ax_txn_append_child(t, bt->root, bt->b);
    ax_txn_append_child(t, bt->a, bt->a1);
    ax_txn_append_child(t, bt->a, bt->a2);
    ax_txn_append_child(t, bt->b, bt->b1);
    ax_txn_set_f32(t, bt->a1, AX_PROP_OPACITY, 0.75f);
    ax_txn_set_dimension(t, bt->b, AX_PROP_WIDTH, points(40.0f));
    (void)ax_txn_commit(t, NULL);
    (void)ax_world_apply_pending(w, UINT64_MAX);
}

AX_TEST(a_rejected_transaction_changes_nothing)
{
    ax_world w = make_world(NULL);
    base_tree bt;
    ax_txn *t;
    ax_node created;
    ax_node stale;
    ax_node nodes[6];
    node_state before[6];
    node_state after[6];
    ax_txn_error err;
    uint64_t seq = 0;
    uint32_t count_before;
    ax_node recycled;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    build_base(w, &bt);
    nodes[0] = bt.root;
    nodes[1] = bt.a;
    nodes[2] = bt.b;
    nodes[3] = bt.a1;
    nodes[4] = bt.a2;
    nodes[5] = bt.b1;
    snapshot(w, nodes, 6, before);
    count_before = ax_node_store_count(&ax_world_scene(w)->store);
    stale.h = ax_handle_make(ax_handle_slot(bt.a2.h), ax_handle_generation(bt.a2.h) + 5u);

    /* Every kind of change, then a stale node at command 12. */
    t = ax_txn_begin(w);
    created = ax_txn_create(t, AX_NODE_VIEW);                    /* 0 */
    ax_txn_set_f32(t, created, AX_PROP_OPACITY, 0.2f);           /* 1 */
    ax_txn_append_child(t, bt.b, created);                       /* 2 */
    ax_txn_set_f32(t, bt.a1, AX_PROP_OPACITY, 0.1f);             /* 3 */
    ax_txn_set_f32(t, bt.root, AX_PROP_CORNER_RADIUS, 9.0f);     /* 4: a shorthand */
    ax_txn_set_dimension(t, bt.b, AX_PROP_WIDTH, points(41.0f)); /* 5 */
    ax_txn_insert_before(t, bt.a, bt.a2, bt.a1);                 /* 6: reorder */
    ax_txn_append_child(t, bt.b, bt.a1);                         /* 7: reparent */
    ax_txn_remove(t, bt.b1);                                     /* 8 */
    ax_txn_append_child(t, bt.a1, bt.a2);                        /* 9 */
    ax_txn_set_transform(t, bt.a, AX_PROP_TRANSFORM,
                         (ax_transform2d){2.0f, 0.0f, 0.0f, 2.0f, 1.0f, 1.0f}); /* 10 */
    ax_txn_set_f32(t, created, AX_PROP_CORNER_RADIUS_TOP_LEFT, 3.0f);           /* 11 */
    ax_txn_set_f32(t, stale, AX_PROP_OPACITY, 0.5f);                            /* 12: fails */
    ax_txn_set_f32(t, bt.root, AX_PROP_OPACITY, 0.5f);                          /* 13: never runs */
    AX_REQUIRE(ax_txn_commit(t, &seq) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), seq);

    snapshot(w, nodes, 6, after);
    AX_CHECK(memcmp(before, after, sizeof before) == 0);
    AX_CHECK_EQ_U(ax_node_store_count(&ax_world_scene(w)->store), count_before);
    AX_CHECK(!is_live(w, created));
    AX_CHECK(is_live(w, bt.b1)); /* the removal never happened */

    AX_REQUIRE(ax_world_poll_txn_error(w, &err, NULL));
    AX_CHECK_EQ_U(err.seq, seq);
    AX_CHECK_RESULT(err.result, AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(err.command, 12);
    AX_CHECK(err.message.len > 0 && strstr(err.message.ptr, "command 12") != NULL);

    /* The created handle stays stale; once the object comes back its slot returns one
     * generation later, and works. */
    t = ax_txn_begin(w);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    t = ax_txn_begin(w);
    recycled = ax_txn_create(t, AX_NODE_VIEW);
    AX_CHECK_EQ_U(recycled.h, ax_handle_make(ax_handle_slot(created.h), 2));
    ax_txn_append_child(t, bt.root, recycled);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK(is_live(w, recycled));
    AX_CHECK(!is_live(w, created));
    AX_CHECK(!ax_world_poll_txn_error(w, &err, NULL));
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

typedef enum bad_kind {
    BAD_CYCLE,
    BAD_REMOVED_EARLIER,
    BAD_UNKNOWN_DYNAMIC_PROP,
    BAD_TRANSITION_UNKNOWN_PROP,
    BAD_ANCHOR_NOT_A_CHILD,
    BAD_REMOVE_TWICE,
    BAD_MALFORMED,
    BAD_COUNT
} bad_kind;

AX_TEST(each_kind_of_scene_rejection)
{
    static const ax_result expected[BAD_COUNT] = {AX_ERR_INVALID_ARGUMENT, AX_ERR_INVALID_HANDLE,
                                                  AX_ERR_INVALID_ARGUMENT, AX_ERR_INVALID_ARGUMENT,
                                                  AX_ERR_INVALID_ARGUMENT, AX_ERR_INVALID_HANDLE,
                                                  AX_ERR_INVALID_ARGUMENT};
    static const uint32_t expected_command[BAD_COUNT] = {2, 2, 1, 1, 1, 2, 1};
    ax_world w = make_world(NULL);
    base_tree bt;
    ax_node nodes[6];
    node_state before[6];
    node_state after[6];
    int k;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    build_base(w, &bt);
    nodes[0] = bt.root;
    nodes[1] = bt.a;
    nodes[2] = bt.b;
    nodes[3] = bt.a1;
    nodes[4] = bt.a2;
    nodes[5] = bt.b1;
    snapshot(w, nodes, 6, before);

    for (k = 0; k < BAD_COUNT; ++k) {
        ax_txn *t = ax_txn_begin(w);
        ax_txn_error err;
        ax_transition tr;
        memset(&tr, 0, sizeof tr);
        tr.struct_size = sizeof tr;
        tr.kind = AX_TRANSITION_NONE;
        ax_txn_set_f32(t, bt.a, AX_PROP_OPACITY, 0.3f); /* 0: a valid change first */
        switch ((bad_kind)k) {
        case BAD_CYCLE:
            ax_txn_append_child(t, bt.a, bt.b);     /* 1 */
            ax_txn_append_child(t, bt.b1, bt.root); /* 2: the root into its own subtree */
            break;
        case BAD_REMOVED_EARLIER:
            ax_txn_remove(t, bt.a);                          /* 1 */
            ax_txn_set_f32(t, bt.a1, AX_PROP_OPACITY, 0.9f); /* 2: inside the removed one */
            break;
        case BAD_UNKNOWN_DYNAMIC_PROP:
            ax_txn_set_f32(t, bt.a, AX_PROP_FIRST_DYNAMIC + 77u, 1.0f); /* 1 */
            break;
        case BAD_TRANSITION_UNKNOWN_PROP:
            ax_txn_set_transition(t, bt.a, AX_PROP_FIRST_IDL + 3u, &tr); /* 1 */
            break;
        case BAD_ANCHOR_NOT_A_CHILD:
            ax_txn_insert_before(t, bt.a, bt.b, bt.b1); /* 1: b1 is b's child, not a's */
            break;
        case BAD_REMOVE_TWICE:
            ax_txn_remove(t, bt.b1); /* 1 */
            ax_txn_remove(t, bt.b1); /* 2 */
            break;
        case BAD_MALFORMED:
            ax_txn_set_f32(t, bt.b, AX_PROP_OPACITY, 0.4f); /* 1: corrupted below */
            break;
        case BAD_COUNT:
            break;
        }
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        if (k == BAD_MALFORMED) {
            /* Corrupt the second command's size, as a damaged buffer would be. */
            ax_txn_info info;
            ax_txn_cmd *second;
            ax_txn_get_info(ax_world_peek_txn(w), &info);
            second =
                (ax_txn_cmd *)(void *)((unsigned char *)(uintptr_t)info.commands +
                                       ((const ax_txn_cmd *)(const void *)info.commands)->size);
            second->size = 12;
        }
        AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
        if (!ax_world_poll_txn_error(w, &err, NULL)) {
            ax_test_fail(__FILE__, __LINE__, "case %d: not rejected", k);
            continue;
        }
        if (err.result != expected[k] || err.command != expected_command[k]) {
            ax_test_fail(__FILE__, __LINE__, "case %d: %s at command %u (%.*s)", k,
                         ax_result_name(err.result), (unsigned)err.command, (int)err.message.len,
                         err.message.ptr);
        }
        snapshot(w, nodes, 6, after);
        if (memcmp(before, after, sizeof before) != 0) {
            ax_test_fail(__FILE__, __LINE__, "case %d: the tree changed", k);
        }
    }
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(a_rejected_transaction_may_leave_dirty_flags_set)
{
    ax_world w = make_world(NULL);
    base_tree bt;
    ax_txn *t;
    ax_node created;
    ax_node stale;
    ax_scene *scene;
    uint32_t i;
    bool stale_root = false;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    build_base(w, &bt);
    scene = ax_world_scene(w);
    ax_scene_clear_dirty(scene);
    stale.h = ax_handle_make(999, 1);

    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, bt.a1, AX_PROP_WIDTH, points(10.0f));
    ax_txn_set_f32(t, bt.b, AX_PROP_OPACITY, 0.2f);
    created = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_f32(t, stale, AX_PROP_OPACITY, 0.5f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);

    /* Values are restored... */
    {
        ax_value v;
        AX_CHECK_RESULT(ax_scene_get(scene, bt.a1, AX_PROP_WIDTH, &v), AX_OK);
        AX_CHECK_EQ_U(v.dimension.unit, AX_UNIT_AUTO);
    }
    AX_CHECK(get_f32(w, bt.b, AX_PROP_OPACITY) == 1.0f);
    AX_CHECK(!is_live(w, created));
    /* ...but the flags set while applying and undoing stay: redundant work next frame, never
     * missing work. */
    AX_CHECK(ax_scene_dirty_flags(scene, bt.a1) & AX_DIRTY_LAYOUT);
    AX_CHECK(ax_scene_dirty_flags(scene, bt.a) & AX_DIRTY_LAYOUT_CHILD);
    AX_CHECK(ax_scene_dirty_flags(scene, bt.b) & AX_DIRTY_COMPOSITE);
    /* The rolled-back node was recorded as a layout root: a stale candidate layout skips. */
    for (i = 0; i < scene->dirty.layout_root_count; ++i) {
        if (scene->dirty.layout_roots[i].h == created.h) {
            stale_root = true;
        }
    }
    AX_CHECK(stale_root);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(removal_destroys_the_subtree_and_its_slots_come_back)
{
    ax_world w = make_world(NULL);
    base_tree bt;
    ax_txn *t;
    ax_node got[4];
    ax_node again[3];
    int i;
    uint32_t live_before;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    build_base(w, &bt);
    live_before = ax_node_store_count(&ax_world_scene(w)->store);

    t = ax_txn_begin(w);
    ax_txn_remove(t, bt.a);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    AX_CHECK(!is_live(w, bt.a));
    AX_CHECK(!is_live(w, bt.a1));
    AX_CHECK(!is_live(w, bt.a2));
    AX_CHECK_EQ_U(children(w, bt.root, got, 4), 1);
    AX_CHECK_EQ_U(got[0].h, bt.b.h);
    AX_CHECK_EQ_U(ax_node_store_count(&ax_world_scene(w)->store), live_before - 3u);

    /* The three slots come back at their next generation (in some order). */
    t = ax_txn_begin(w);
    for (i = 0; i < 3; ++i) {
        uint32_t slot;
        again[i] = ax_txn_create(t, AX_NODE_VIEW);
        slot = ax_handle_slot(again[i].h);
        AX_CHECK(slot == ax_handle_slot(bt.a.h) || slot == ax_handle_slot(bt.a1.h) ||
                 slot == ax_handle_slot(bt.a2.h));
        AX_CHECK_EQ_U(ax_handle_generation(again[i].h), 2);
        ax_txn_append_child(t, bt.b, again[i]);
    }
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);
    for (i = 0; i < 3; ++i) {
        AX_CHECK(is_live(w, again[i]));
    }
    AX_CHECK(!is_live(w, bt.a1));
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
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

/* ---- Stress: the app thread against a model, the scene thread applying (TSan) --------------- */

enum { GROUPS = 8, ITEMS = 64, STRESS_TXNS = 4000 };

typedef struct model {
    ax_node item[ITEMS]; /* null when the entry is free */
    float opacity[ITEMS];
    int order[GROUPS][ITEMS]; /* item indices in child order */
    int count[GROUPS];
    int group_of[ITEMS];
} model;

typedef struct stress {
    ax_world world;
    ax_node root;
    ax_node group[GROUPS];
    model m;
    ax_node stale[32]; /* nodes removed by applied transactions */
    uint32_t stale_count;
    ax_node removing[8]; /* removed by the transaction being recorded */
    uint32_t removing_count;
    uint64_t rng;
    ax_atomic_u32 stop;
} stress;

static uint32_t next_rand(stress *s, uint32_t n)
{
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 7;
    s->rng ^= s->rng << 17;
    return (uint32_t)(s->rng % n);
}

static void model_unlink(model *m, int item)
{
    int g = m->group_of[item];
    int i;
    for (i = 0; i < m->count[g]; ++i) {
        if (m->order[g][i] == item) {
            memmove(&m->order[g][i], &m->order[g][i + 1],
                    sizeof(int) * (size_t)(m->count[g] - i - 1));
            m->count[g]--;
            return;
        }
    }
}

static int pick_live(stress *s, const model *m)
{
    int tries;
    for (tries = 0; tries < 8; ++tries) {
        int i = (int)next_rand(s, ITEMS);
        if (!AX_HANDLE_IS_NULL(m->item[i])) {
            return i;
        }
    }
    return -1;
}

/* Records one random valid change into t and applies it to the model copy. */
static void random_change(stress *s, ax_txn *t, model *m)
{
    uint32_t op = next_rand(s, 4);
    int i = pick_live(s, m);
    int g = (int)next_rand(s, GROUPS);
    if (op == 0 || i < 0) {
        int slot;
        for (slot = 0; slot < ITEMS && !AX_HANDLE_IS_NULL(m->item[slot]); ++slot) {
        }
        if (slot == ITEMS) {
            return;
        }
        m->item[slot] = ax_txn_create(t, AX_NODE_VIEW);
        m->opacity[slot] = 1.0f;
        ax_txn_append_child(t, s->group[g], m->item[slot]);
        m->group_of[slot] = g;
        m->order[g][m->count[g]++] = slot;
    } else if (op == 1) {
        float v = (float)next_rand(s, 101) / 100.0f;
        ax_txn_set_f32(t, m->item[i], AX_PROP_OPACITY, v);
        m->opacity[i] = v;
    } else if (op == 2) {
        /* Move: to the end of a group, or before another item there. */
        int before = m->count[g] > 0 ? m->order[g][next_rand(s, (uint32_t)m->count[g])] : -1;
        model_unlink(m, i);
        if (before == i || before < 0) {
            ax_txn_append_child(t, s->group[g], m->item[i]);
            m->order[g][m->count[g]++] = i;
        } else {
            int k;
            ax_txn_insert_before(t, s->group[g], m->item[i], m->item[before]);
            for (k = 0; m->order[g][k] != before; ++k) {
            }
            memmove(&m->order[g][k + 1], &m->order[g][k], sizeof(int) * (size_t)(m->count[g] - k));
            m->order[g][k] = i;
            m->count[g]++;
        }
        m->group_of[i] = g;
    } else {
        ax_txn_remove(t, m->item[i]);
        model_unlink(m, i);
        s->removing[s->removing_count++] = m->item[i];
        m->item[i].h = 0;
    }
}

static int stress_scene_main(void *arg)
{
    stress *s = (stress *)arg;
    ax_world_bind_scene_thread(s->world);
    while (ax_atomic_load_u32(&s->stop, AX_MO_ACQUIRE) == 0) {
        if (ax_world_apply_pending(s->world, UINT64_MAX) == 0) {
            ax_thread_yield();
        }
    }
    (void)ax_world_apply_pending(s->world, UINT64_MAX);
    return 0;
}

AX_TEST(stress_the_app_thread_commits_while_the_scene_applies)
{
    static stress s;
    ax_thread scene;
    ax_txn *t;
    uint32_t invalid = 0;
    uint32_t rejected = 0;
    uint64_t n;
    int g;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    memset(&s, 0, sizeof s);
    s.rng = 0x2545F4914F6CDD1Dull;
    ax_atomic_init_u32(&s.stop, 0);
    s.world = make_world(NULL);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(s.world));

    t = ax_txn_begin(s.world);
    s.root = ax_txn_create(t, AX_NODE_VIEW);
    for (g = 0; g < GROUPS; ++g) {
        s.group[g] = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_append_child(t, s.root, s.group[g]);
    }
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(ax_thread_create(&scene, stress_scene_main, &s) == AX_OK);

    for (n = 2; n <= STRESS_TXNS; ++n) {
        model next = s.m;
        uint32_t changes = 1u + next_rand(&s, 6);
        bool bad = next_rand(&s, 8) == 0;
        uint32_t c;
        t = ax_txn_begin(s.world);
        AX_REQUIRE(t != NULL);
        s.removing_count = 0;
        for (c = 0; c < changes; ++c) {
            random_change(&s, t, &next);
        }
        if (bad) {
            /* A stale handle (a node removed earlier), or a cycle: the whole transaction, with
             * the valid changes before it, must be rejected. */
            if (s.stale_count > 0) {
                ax_txn_set_f32(t, s.stale[next_rand(&s, s.stale_count)], AX_PROP_OPACITY, 0.5f);
            } else {
                ax_txn_append_child(t, s.group[0], s.root);
            }
            invalid++;
        }
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        if (!bad) {
            /* Only a transaction that applies removes its nodes. */
            s.m = next;
            for (c = 0; c < s.removing_count; ++c) {
                s.stale[s.stale_count < 32 ? s.stale_count++ : next_rand(&s, 32)] = s.removing[c];
            }
        }
        rejected += drain_errors(s.world);
    }
    while (ax_world_last_applied_seq(s.world) < STRESS_TXNS) {
        rejected += drain_errors(s.world);
        ax_thread_yield();
    }
    ax_atomic_store_u32(&s.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&scene);
    rejected += drain_errors(s.world);
    AX_CHECK_EQ_U(rejected, invalid);
    AX_CHECK(invalid > 0);

    /* The scene matches the model of every valid transaction, in order. */
    ax_world_bind_scene_thread(s.world);
    {
        uint32_t live = 0;
        int i;
        for (g = 0; g < GROUPS; ++g) {
            ax_node got[ITEMS];
            uint32_t k;
            uint32_t count = children(s.world, s.group[g], got, ITEMS);
            AX_CHECK_EQ_U(count, (uint32_t)s.m.count[g]);
            for (k = 0; k < count && k < (uint32_t)s.m.count[g]; ++k) {
                AX_CHECK_EQ_U(got[k].h, s.m.item[s.m.order[g][k]].h);
            }
        }
        for (i = 0; i < ITEMS; ++i) {
            if (!AX_HANDLE_IS_NULL(s.m.item[i])) {
                live++;
                AX_CHECK(is_live(s.world, s.m.item[i]));
                AX_CHECK(get_f32(s.world, s.m.item[i], AX_PROP_OPACITY) == s.m.opacity[i]);
            }
        }
        for (i = 0; i < (int)s.stale_count; ++i) {
            AX_CHECK(!is_live(s.world, s.stale[i]));
        }
        AX_CHECK_EQ_U(ax_node_store_count(&ax_world_scene(s.world)->store), 1u + GROUPS + live);
    }
    AX_CHECK_RESULT(ax_world_destroy(s.world), AX_OK);
}
