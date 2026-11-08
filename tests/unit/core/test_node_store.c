/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/core/ax_node_store.h"

/* ---- Helpers --------------------------------------------------------------------------------- */

typedef struct fixture {
    ax_tracking_allocator tracker;
    ax_allocator alloc;
    ax_node_store store;
} fixture;

static void fixture_init(fixture *f)
{
    ax_node_store_desc desc;
    ax_tracking_allocator_init(&f->tracker, ax_default_allocator());
    f->alloc = ax_tracking_allocator_get(&f->tracker);
    memset(&desc, 0, sizeof desc);
    desc.allocator = &f->alloc;
    ax_node_store_init(&f->store, &desc);
}

static size_t fixture_deinit(fixture *f)
{
    ax_node_store_deinit(&f->store);
    return ax_tracking_allocator_deinit(&f->tracker);
}

static ax_node make(ax_node_store *s)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (ax_node_store_create(s, AX_NODE_VIEW, &n) != AX_OK) {
        n.h = 0;
    }
    return n;
}

/* A per-node marker in a hot column, to check that rows move with their data. */
static void mark(ax_node_store *s, ax_node n, float v)
{
    uint32_t row = ax_node_store_row(s, n);
    s->paint[row].corner_radius[0] = v;
    s->composite[row].opacity = v;
}

static float marker(const ax_node_store *s, ax_node n)
{
    uint32_t row = ax_node_store_row(s, n);
    return row == AX_NODE_NONE ? -1.0f : s->paint[row].corner_radius[0];
}

/* Children of a node as handles, in order; returns the count. Also checks the back links. */
static uint32_t children_of(const ax_node_store *s, ax_node parent, ax_node *out, uint32_t cap)
{
    uint32_t n = 0;
    ax_node prev = AX_HANDLE_NULL_INIT;
    ax_node c = ax_node_store_first_child(s, parent);
    while (!AX_HANDLE_IS_NULL(c)) {
        AX_CHECK(AX_HANDLE_EQ(ax_node_store_parent(s, c), parent));
        AX_CHECK(AX_HANDLE_EQ(ax_node_store_prev_sibling(s, c), prev));
        if (n < cap) {
            out[n] = c;
        }
        n++;
        prev = c;
        c = ax_node_store_next_sibling(s, c);
    }
    AX_CHECK(AX_HANDLE_EQ(ax_node_store_last_child(s, parent), prev));
    return n;
}

#define CHECK_CHILDREN(s, parent, ...)                                                \
    do {                                                                              \
        ax_node ax__expected[] = {__VA_ARGS__};                                       \
        ax_node ax__got[16];                                                          \
        uint32_t ax__n = children_of((s), (parent), ax__got, 16);                     \
        uint32_t ax__i;                                                               \
        AX_CHECK_EQ_U(ax__n, AX_COUNTOF(ax__expected));                               \
        for (ax__i = 0; ax__i < ax__n && ax__i < AX_COUNTOF(ax__expected); ++ax__i) { \
            AX_CHECK_EQ_U(ax__got[ax__i].h, ax__expected[ax__i].h);                   \
        }                                                                             \
    } while (0)

/* ---- Creating and destroying ----------------------------------------------------------------- */

AX_TEST(create_gives_live_nodes_with_default_values)
{
    fixture f;
    ax_node a;
    uint32_t row;
    fixture_init(&f);
    a = make(&f.store);
    AX_REQUIRE(!AX_HANDLE_IS_NULL(a));
    AX_CHECK(ax_node_store_is_live(&f.store, a));
    AX_CHECK_EQ_U(ax_node_store_count(&f.store), 1);
    row = ax_node_store_row(&f.store, a);
    AX_REQUIRE(row == 0);
    AX_CHECK_EQ_U(f.store.kind[row], AX_NODE_VIEW);
    AX_CHECK_EQ_U(f.store.style[row].width.unit, AX_UNIT_AUTO);
    AX_CHECK_EQ_U(f.store.style[row].inset[AX_EDGE_LEFT].unit, AX_UNIT_AUTO);
    AX_CHECK_EQ_U(f.store.style[row].margin[AX_EDGE_TOP].unit, AX_UNIT_POINTS);
    AX_CHECK_EQ_U(f.store.style[row].flex_direction, AX_FLEX_DIRECTION_COLUMN);
    AX_CHECK(f.store.paint[row].background.a == 0.0f);
    AX_CHECK(f.store.paint[row].corner_radius[2] == 0.0f);
    AX_CHECK(f.store.composite[row].opacity == 1.0f);
    AX_CHECK(f.store.composite[row].transform.a == 1.0f &&
             f.store.composite[row].transform.d == 1.0f);
    AX_CHECK(f.store.composite[row].transform.tx == 0.0f);
    AX_CHECK(f.store.layout[row].frame.w == 0.0f);
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_parent(&f.store, a)));
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_first_child(&f.store, a)));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(destroyed_handles_are_stale_and_a_recycled_slot_gets_a_new_generation)
{
    fixture f;
    ax_node a;
    ax_node b;
    ax_node null_node = AX_HANDLE_NULL_INIT;
    fixture_init(&f);
    a = make(&f.store);
    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, a), AX_OK);
    AX_CHECK(!ax_node_store_is_live(&f.store, a));
    AX_CHECK_EQ_U(ax_node_store_row(&f.store, a), AX_NODE_NONE);
    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, a), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, null_node), AX_ERR_INVALID_HANDLE);

    /* LIFO recycling: same slot, next generation; the old handle stays stale. */
    b = make(&f.store);
    AX_CHECK_EQ_U(ax_handle_slot(b.h), ax_handle_slot(a.h));
    AX_CHECK_EQ_U(ax_handle_generation(b.h), ax_handle_generation(a.h) + 1u);
    AX_CHECK(!ax_node_store_is_live(&f.store, a));
    AX_CHECK(ax_node_store_is_live(&f.store, b));
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_parent(&f.store, a)));
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, b, a), AX_ERR_INVALID_HANDLE);

    /* A handle from a slot the store never created. */
    {
        ax_node foreign;
        foreign.h = ax_handle_make(9999, 1);
        AX_CHECK(!ax_node_store_is_live(&f.store, foreign));
        AX_CHECK_RESULT(ax_node_store_destroy(&f.store, foreign), AX_ERR_INVALID_HANDLE);
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(an_exhausted_generation_retires_its_slot)
{
    fixture f;
    ax_node a;
    ax_node b;
    fixture_init(&f);
    a = make(&f.store);
    /* Skip ahead: the slot is at its last generation. */
    f.store.slots[ax_handle_slot(a.h)].generation = UINT32_MAX;
    a.h = ax_handle_make(ax_handle_slot(a.h), UINT32_MAX);
    AX_REQUIRE(ax_node_store_is_live(&f.store, a));
    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, a), AX_OK);
    b = make(&f.store);
    AX_CHECK(ax_handle_slot(b.h) != ax_handle_slot(a.h));
    AX_CHECK(!ax_node_store_is_live(&f.store, a));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(swap_remove_keeps_every_other_node_and_its_data)
{
    enum { N = 200 };
    fixture f;
    ax_node nodes[N];
    uint32_t i;
    fixture_init(&f);
    for (i = 0; i < N; ++i) {
        nodes[i] = make(&f.store);
        AX_REQUIRE(!AX_HANDLE_IS_NULL(nodes[i]));
        mark(&f.store, nodes[i], (float)i);
    }
    /* Destroy every third node, early rows first, so many rows move. */
    for (i = 0; i < N; i += 3) {
        AX_CHECK_RESULT(ax_node_store_destroy(&f.store, nodes[i]), AX_OK);
    }
    AX_CHECK_EQ_U(ax_node_store_count(&f.store), N - (N + 2) / 3);
    for (i = 0; i < N; ++i) {
        if (i % 3 == 0) {
            AX_CHECK(!ax_node_store_is_live(&f.store, nodes[i]));
        } else {
            uint32_t row = ax_node_store_row(&f.store, nodes[i]);
            AX_REQUIRE(row < ax_node_store_count(&f.store));
            AX_CHECK_EQ_U(f.store.slot_of_row[row], ax_handle_slot(nodes[i].h));
            AX_CHECK(marker(&f.store, nodes[i]) == (float)i);
            AX_CHECK(f.store.composite[row].opacity == (float)i);
        }
    }
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- Hierarchy ------------------------------------------------------------------------------- */

AX_TEST(append_insert_and_reorder_children)
{
    fixture f;
    ax_node p;
    ax_node a;
    ax_node b;
    ax_node c;
    ax_node d;
    ax_node none = AX_HANDLE_NULL_INIT;
    fixture_init(&f);
    p = make(&f.store);
    a = make(&f.store);
    b = make(&f.store);
    c = make(&f.store);
    d = make(&f.store);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p, a), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p, b), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p, c), AX_OK);
    CHECK_CHILDREN(&f.store, p, a, b, c);

    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, p, d, a), AX_OK); /* at the front */
    CHECK_CHILDREN(&f.store, p, d, a, b, c);
    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, p, c, a), AX_OK); /* last to middle */
    CHECK_CHILDREN(&f.store, p, d, c, a, b);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p, d), AX_OK); /* first to last */
    CHECK_CHILDREN(&f.store, p, c, a, b, d);
    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, p, a, b), AX_OK); /* already there */
    CHECK_CHILDREN(&f.store, p, c, a, b, d);
    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, p, b, none), AX_OK); /* null: append */
    CHECK_CHILDREN(&f.store, p, c, a, d, b);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p, b), AX_OK); /* last stays last */
    CHECK_CHILDREN(&f.store, p, c, a, d, b);

    AX_CHECK_RESULT(ax_node_store_detach(&f.store, a), AX_OK);
    CHECK_CHILDREN(&f.store, p, c, d, b);
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_parent(&f.store, a)));
    AX_CHECK_RESULT(ax_node_store_detach(&f.store, a), AX_OK); /* a root: no-op */
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(reparenting_moves_a_subtree)
{
    fixture f;
    ax_node p1;
    ax_node p2;
    ax_node x;
    ax_node y;
    ax_node z;
    fixture_init(&f);
    p1 = make(&f.store);
    p2 = make(&f.store);
    x = make(&f.store);
    y = make(&f.store);
    z = make(&f.store);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p1, x), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p1, z), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, x, y), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, p2, x), AX_OK);
    CHECK_CHILDREN(&f.store, p1, z);
    CHECK_CHILDREN(&f.store, p2, x);
    CHECK_CHILDREN(&f.store, x, y);
    AX_CHECK(ax_node_store_is_ancestor_or_self(&f.store, p2, y));
    AX_CHECK(!ax_node_store_is_ancestor_or_self(&f.store, p1, y));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(cycles_and_foreign_anchors_are_rejected_without_changes)
{
    fixture f;
    ax_node a;
    ax_node b;
    ax_node c;
    ax_node other;
    fixture_init(&f);
    a = make(&f.store);
    b = make(&f.store);
    c = make(&f.store);
    other = make(&f.store);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, a, b), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, b, c), AX_OK);

    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, a, a), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, c, a), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, c, b), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, a, other, c),
                    AX_ERR_INVALID_ARGUMENT); /* c is not a's child */
    AX_CHECK_RESULT(ax_node_store_insert_before(&f.store, a, b, b), AX_ERR_INVALID_ARGUMENT);

    CHECK_CHILDREN(&f.store, a, b);
    CHECK_CHILDREN(&f.store, b, c);
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_parent(&f.store, a)));
    AX_CHECK(AX_HANDLE_IS_NULL(ax_node_store_parent(&f.store, other)));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(destroy_removes_the_whole_subtree)
{
    fixture f;
    ax_node root;
    ax_node a;
    ax_node b;
    ax_node c;
    ax_node a1;
    ax_node a2;
    ax_node a11;
    ax_node keep;
    fixture_init(&f);
    root = make(&f.store);
    a = make(&f.store);
    b = make(&f.store);
    c = make(&f.store);
    a1 = make(&f.store);
    a2 = make(&f.store);
    a11 = make(&f.store);
    keep = make(&f.store);
    mark(&f.store, keep, 42.0f);
    mark(&f.store, c, 7.0f);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, root, a), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, root, b), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, root, c), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, a, a1), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, a, a2), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&f.store, a1, a11), AX_OK);

    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, a), AX_OK);
    AX_CHECK(!ax_node_store_is_live(&f.store, a));
    AX_CHECK(!ax_node_store_is_live(&f.store, a1));
    AX_CHECK(!ax_node_store_is_live(&f.store, a2));
    AX_CHECK(!ax_node_store_is_live(&f.store, a11));
    CHECK_CHILDREN(&f.store, root, b, c);
    AX_CHECK_EQ_U(ax_node_store_count(&f.store), 4);
    AX_CHECK(marker(&f.store, keep) == 42.0f);
    AX_CHECK(marker(&f.store, c) == 7.0f);

    AX_CHECK_RESULT(ax_node_store_destroy(&f.store, root), AX_OK);
    AX_CHECK_EQ_U(ax_node_store_count(&f.store), 1);
    AX_CHECK(ax_node_store_is_live(&f.store, keep));
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- Compaction ------------------------------------------------------------------------------ */

/* Pre-order walk; checks that rows are consecutive from *next_row. */
static void check_preorder(const ax_node_store *s, ax_node n, uint32_t *next_row)
{
    ax_node c;
    AX_CHECK_EQ_U(ax_node_store_row(s, n), *next_row);
    (*next_row)++;
    for (c = ax_node_store_first_child(s, n); !AX_HANDLE_IS_NULL(c);
         c = ax_node_store_next_sibling(s, c)) {
        check_preorder(s, c, next_row);
    }
}

AX_TEST(compact_puts_rows_in_preorder_with_stable_handles)
{
    enum { N = 300 };
    fixture f;
    ax_node nodes[N];
    ax_node root1;
    ax_node root2;
    uint32_t i;
    uint32_t next_row = 0;
    uint32_t live = 0;
    fixture_init(&f);
    /* Two trees built in an order unrelated to tree order: node i's parent is chosen by a
     * scrambled earlier index. */
    for (i = 0; i < N; ++i) {
        nodes[i] = make(&f.store);
        AX_REQUIRE(!AX_HANDLE_IS_NULL(nodes[i]));
        mark(&f.store, nodes[i], (float)i + 0.5f);
        if (i >= 2) {
            uint32_t parent = (i * 7919u) % i;
            AX_REQUIRE(ax_node_store_insert_before(
                           &f.store, nodes[parent], nodes[i],
                           ax_node_store_first_child(&f.store, nodes[parent])) == AX_OK);
        }
    }
    root1 = nodes[0];
    root2 = nodes[1];
    /* Holes: destroy some subtrees. */
    for (i = 250; i < N; i += 17) {
        if (ax_node_store_is_live(&f.store, nodes[i])) {
            AX_CHECK_RESULT(ax_node_store_destroy(&f.store, nodes[i]), AX_OK);
        }
    }
    AX_CHECK_RESULT(ax_node_store_compact(&f.store), AX_OK);

    /* Root order follows their rows before compaction: node 0 then node 1. */
    check_preorder(&f.store, root1, &next_row);
    check_preorder(&f.store, root2, &next_row);
    AX_CHECK_EQ_U(next_row, ax_node_store_count(&f.store));
    for (i = 0; i < N; ++i) {
        if (ax_node_store_is_live(&f.store, nodes[i])) {
            live++;
            AX_CHECK(marker(&f.store, nodes[i]) == (float)i + 0.5f);
            AX_CHECK_EQ_U(f.store.slot_of_row[ax_node_store_row(&f.store, nodes[i])],
                          ax_handle_slot(nodes[i].h));
        }
    }
    AX_CHECK_EQ_U(live, ax_node_store_count(&f.store));
    AX_CHECK(live < N);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- External slots -------------------------------------------------------------------------- */

typedef struct freed_log {
    ax_node handles[16];
    uint32_t count;
} freed_log;

static void on_freed(void *user, ax_node next)
{
    freed_log *log = (freed_log *)user;
    if (log->count < AX_COUNTOF(log->handles)) {
        log->handles[log->count] = next;
    }
    log->count++;
}

AX_TEST(external_slots_create_at_reserved_handles_and_report_freed_slots)
{
    ax_node_store s;
    ax_node_store_desc desc;
    freed_log log;
    ax_node a;
    ax_node b;
    ax_node c;
    ax_node again;
    memset(&log, 0, sizeof log);
    memset(&desc, 0, sizeof desc);
    desc.on_slot_freed = on_freed;
    desc.user = &log;
    ax_node_store_init(&s, &desc);

    a.h = ax_handle_make(0, 1);
    b.h = ax_handle_make(5, 1); /* a gap: slots 1-4 unseen */
    c.h = ax_handle_make(2, 1);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, a, AX_NODE_VIEW), AX_OK);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, b, AX_NODE_VIEW), AX_OK);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, c, AX_NODE_VIEW), AX_OK);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, a, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE); /* live */
    again.h = 0;
    AX_CHECK_RESULT(ax_node_store_create_at(&s, again, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE);

    AX_CHECK_RESULT(ax_node_store_append_child(&s, a, b), AX_OK);
    AX_CHECK_RESULT(ax_node_store_append_child(&s, b, c), AX_OK);
    AX_CHECK_RESULT(ax_node_store_destroy(&s, b), AX_OK);
    /* Every destroyed node is reported with its slot's next handle; no free list is kept. */
    AX_REQUIRE(log.count == 2);
    AX_CHECK_EQ_U(log.handles[0].h, ax_handle_make(2, 2)); /* c first: post-order */
    AX_CHECK_EQ_U(log.handles[1].h, ax_handle_make(5, 2));
    AX_CHECK_EQ_U(s.free_head, AX_NODE_NONE);

    /* The old generation is refused; the reported handle works. */
    AX_CHECK_RESULT(ax_node_store_create_at(&s, b, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, log.handles[1], AX_NODE_VIEW), AX_OK);
    AX_CHECK(ax_node_store_is_live(&s, log.handles[1]));
    AX_CHECK(!ax_node_store_is_live(&s, b));
    ax_node_store_deinit(&s);
    AX_CHECK_EQ_U(log.count, 2); /* deinit reports nothing */
}

AX_TEST(external_slots_adopt_skipped_generations)
{
    ax_node_store s;
    ax_node_store_desc desc;
    freed_log log;
    ax_node n;
    ax_node skipped;
    ax_node old;
    memset(&log, 0, sizeof log);
    memset(&desc, 0, sizeof desc);
    desc.on_slot_freed = on_freed;
    desc.user = &log;
    ax_node_store_init(&s, &desc);

    /* An unseen slot accepts any generation from 1: the app skipped 1-3 (aborted creates). */
    n.h = ax_handle_make(7, 4);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, n, AX_NODE_VIEW), AX_OK);
    old.h = ax_handle_make(7, 3);
    AX_CHECK(!ax_node_store_is_live(&s, old));
    AX_CHECK_RESULT(ax_node_store_destroy(&s, n), AX_OK);
    AX_REQUIRE(log.count == 1);
    AX_CHECK_EQ_U(log.handles[0].h, ax_handle_make(7, 5));

    /* A freed slot accepts its reported generation or any later one, never an earlier one. */
    old.h = ax_handle_make(7, 4);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, old, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE);
    skipped.h = ax_handle_make(7, 9);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, skipped, AX_NODE_VIEW), AX_OK);
    AX_CHECK(ax_node_store_is_live(&s, skipped));
    old.h = ax_handle_make(7, 5); /* the reported handle, skipped by the app: stale */
    AX_CHECK(!ax_node_store_is_live(&s, old));
    AX_CHECK_RESULT(ax_node_store_destroy(&s, skipped), AX_OK);
    AX_REQUIRE(log.count == 2);
    AX_CHECK_EQ_U(log.handles[1].h, ax_handle_make(7, 10));
    AX_CHECK_RESULT(ax_node_store_create_at(&s, skipped, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE);

    /* The last generation: destroying it retires the slot, unreported, and nothing reuses it. */
    n.h = ax_handle_make(7, UINT32_MAX);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, n, AX_NODE_VIEW), AX_OK);
    AX_CHECK_RESULT(ax_node_store_destroy(&s, n), AX_OK);
    AX_CHECK_EQ_U(log.count, 2);
    AX_CHECK_RESULT(ax_node_store_create_at(&s, n, AX_NODE_VIEW), AX_ERR_INVALID_HANDLE);
    AX_CHECK(!ax_node_store_is_live(&s, n));
    ax_node_store_deinit(&s);
}

/* ---- Side components ------------------------------------------------------------------------- */

typedef struct test_component {
    uint32_t value;
    uint32_t pad[3];
} test_component;

static void count_removal(void *element, void *user)
{
    uint32_t *removed = (uint32_t *)user;
    removed[((test_component *)element)->value % 64u]++;
}

AX_TEST(side_components_follow_their_nodes)
{
    fixture f;
    ax_component_desc desc;
    ax_component_type t_a;
    ax_component_type t_b;
    ax_node nodes[40];
    uint32_t removed[64];
    test_component *tc;
    uint32_t i;
    fixture_init(&f);
    memset(removed, 0, sizeof removed);
    memset(&desc, 0, sizeof desc);
    desc.size = sizeof(test_component);
    desc.align = AX_ALIGNOF(test_component);
    desc.on_remove = count_removal;
    desc.user = removed;
    AX_REQUIRE(ax_node_store_register_component(&f.store, &desc, &t_a) == AX_OK);
    desc.on_remove = NULL;
    AX_REQUIRE(ax_node_store_register_component(&f.store, &desc, &t_b) == AX_OK);
    AX_CHECK(t_a != t_b);
    desc.align = 3;
    {
        ax_component_type bad;
        AX_CHECK_RESULT(ax_node_store_register_component(&f.store, &desc, &bad),
                        AX_ERR_INVALID_ARGUMENT);
    }
    desc.align = AX_ALIGNOF(test_component);

    /* Slots above 256 too, so more than one sparse page is used. */
    for (i = 0; i < 300; ++i) {
        ax_node n = make(&f.store);
        AX_REQUIRE(!AX_HANDLE_IS_NULL(n));
        if (i >= 260) {
            nodes[i - 260] = n;
        }
    }
    for (i = 0; i < 40; ++i) {
        AX_REQUIRE(ax_node_store_add_component(&f.store, nodes[i], t_a, (void **)&tc) == AX_OK);
        AX_CHECK(tc->value == 0); /* zeroed */
        tc->value = i;
        if (i % 2 == 0) {
            AX_REQUIRE(ax_node_store_add_component(&f.store, nodes[i], t_b, (void **)&tc) == AX_OK);
            tc->value = 1000 + i;
        }
    }
    /* Adding again returns the existing element. */
    AX_REQUIRE(ax_node_store_add_component(&f.store, nodes[3], t_a, (void **)&tc) == AX_OK);
    AX_CHECK_EQ_U(tc->value, 3);

    AX_CHECK_RESULT(ax_node_store_remove_component(&f.store, nodes[0], t_a), AX_OK);
    AX_CHECK_EQ_U(removed[0], 1);
    AX_CHECK(ax_node_store_get_component(&f.store, nodes[0], t_a) == NULL);
    AX_CHECK_RESULT(ax_node_store_remove_component(&f.store, nodes[0], t_a),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_node_store_remove_component(&f.store, nodes[1], t_b),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(ax_node_store_get_component(&f.store, nodes[1], 17) == NULL);

    /* Destroying nodes removes their components (on_remove), and the others stay intact. */
    for (i = 5; i < 40; i += 5) {
        AX_CHECK_RESULT(ax_node_store_destroy(&f.store, nodes[i]), AX_OK);
        AX_CHECK_EQ_U(removed[i], 1);
        AX_CHECK(ax_node_store_get_component(&f.store, nodes[i], t_a) == NULL);
    }
    for (i = 1; i < 40; ++i) {
        if (i % 5 == 0) {
            continue;
        }
        tc = (test_component *)ax_node_store_get_component(&f.store, nodes[i], t_a);
        AX_REQUIRE(tc != NULL);
        AX_CHECK_EQ_U(tc->value, i);
        tc = (test_component *)ax_node_store_get_component(&f.store, nodes[i], t_b);
        if (i % 2 == 0) {
            AX_REQUIRE(tc != NULL);
            AX_CHECK_EQ_U(tc->value, 1000 + i);
        } else {
            AX_CHECK(tc == NULL);
        }
    }
    /* A recycled slot does not inherit components. */
    {
        ax_node fresh = make(&f.store);
        AX_CHECK(ax_node_store_get_component(&f.store, fresh, t_a) == NULL);
    }
    /* Deinit removes the rest: 40 added, 1 removed explicitly, 7 with their nodes. */
    AX_CHECK_EQ_U(f.store.components[t_a].count, 32);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
    {
        uint32_t total = 0;
        for (i = 0; i < 64; ++i) {
            total += removed[i];
        }
        AX_CHECK_EQ_U(total, 40);
    }
}

AX_TEST(registering_more_component_types_than_the_limit_is_unsupported)
{
    ax_node_store s;
    ax_component_desc desc;
    ax_component_type t;
    uint32_t i;
    ax_node_store_init(&s, NULL);
    memset(&desc, 0, sizeof desc);
    desc.size = 4;
    desc.align = 4;
    for (i = 0; i < AX_NODE_MAX_COMPONENT_TYPES; ++i) {
        AX_REQUIRE(ax_node_store_register_component(&s, &desc, &t) == AX_OK);
        AX_CHECK_EQ_U(t, i);
    }
    AX_CHECK_RESULT(ax_node_store_register_component(&s, &desc, &t), AX_ERR_UNSUPPORTED);
    ax_node_store_deinit(&s);
}

/* ---- Out of memory --------------------------------------------------------------------------- */

typedef struct budget_allocator {
    ax_allocator parent;
    int allocations_left;
} budget_allocator;

static void *budget_alloc(void *ctx, size_t size, size_t align)
{
    budget_allocator *b = (budget_allocator *)ctx;
    if (b->allocations_left <= 0) {
        return NULL;
    }
    b->allocations_left--;
    return b->parent.alloc(b->parent.ctx, size, align);
}

static void *budget_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    budget_allocator *b = (budget_allocator *)ctx;
    if (b->allocations_left <= 0) {
        return NULL;
    }
    b->allocations_left--;
    return b->parent.resize(b->parent.ctx, ptr, old_size, new_size, align);
}

static void budget_free(void *ctx, void *ptr, size_t size)
{
    budget_allocator *b = (budget_allocator *)ctx;
    b->parent.free(b->parent.ctx, ptr, size);
}

AX_TEST(out_of_memory_leaves_the_store_unchanged)
{
    ax_tracking_allocator tracker;
    budget_allocator budget;
    ax_allocator a;
    ax_node_store s;
    ax_node_store_desc desc;
    ax_node nodes[100];
    uint32_t created = 0;
    int limit;
    ax_tracking_allocator_init(&tracker, ax_default_allocator());
    budget.parent = ax_tracking_allocator_get(&tracker);
    a.alloc = budget_alloc;
    a.resize = budget_resize;
    a.free = budget_free;
    a.ctx = &budget;
    memset(&desc, 0, sizeof desc);
    desc.allocator = &a;

    /* Each budget fails at a different point while creating, compacting and adding components. */
    for (limit = 0; limit < 40; ++limit) {
        ax_component_desc cd;
        ax_component_type t;
        uint32_t i;
        ax_result r = AX_OK;
        budget.allocations_left = 1 << 30;
        ax_node_store_init(&s, &desc);
        memset(&cd, 0, sizeof cd);
        cd.size = 8;
        cd.align = 8;
        AX_REQUIRE(ax_node_store_register_component(&s, &cd, &t) == AX_OK);
        budget.allocations_left = limit;
        created = 0;
        for (i = 0; i < 100 && r == AX_OK; ++i) {
            r = ax_node_store_create(&s, AX_NODE_VIEW, &nodes[i]);
            if (r == AX_OK) {
                created++;
                if (i > 0) {
                    r = ax_node_store_append_child(&s, nodes[0], nodes[i]);
                    AX_CHECK_RESULT(r, AX_OK);
                }
                r = ax_node_store_add_component(&s, nodes[i], t, NULL);
                if (r != AX_OK) {
                    AX_CHECK_RESULT(r, AX_ERR_OUT_OF_MEMORY);
                    AX_CHECK(ax_node_store_get_component(&s, nodes[i], t) == NULL);
                }
            } else {
                AX_CHECK_RESULT(r, AX_ERR_OUT_OF_MEMORY);
                AX_CHECK(AX_HANDLE_IS_NULL(nodes[i]));
            }
        }
        AX_CHECK_EQ_U(ax_node_store_count(&s), created);
        r = ax_node_store_compact(&s);
        AX_CHECK(r == AX_OK || r == AX_ERR_OUT_OF_MEMORY);
        for (i = 0; i < created; ++i) {
            AX_CHECK(ax_node_store_is_live(&s, nodes[i]));
        }
        ax_node_store_deinit(&s);
    }
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&tracker), 0);
}

/* ---- Thread ownership ------------------------------------------------------------------------ */

typedef struct thread_ctx {
    ax_node_store *store;
    ax_node created;
    ax_result result;
} thread_ctx;

static int scene_thread_main(void *arg)
{
    thread_ctx *ctx = (thread_ctx *)arg;
    ax_node_store_bind_thread(ctx->store);
    ctx->result = ax_node_store_create(ctx->store, AX_NODE_VIEW, &ctx->created);
    return 0;
}

AX_TEST(another_thread_takes_ownership_by_binding)
{
    ax_node_store s;
    ax_thread t;
    thread_ctx ctx;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    ax_node_store_init(&s, NULL);
    AX_CHECK(ax_node_store_on_owner_thread(&s));
    ctx.store = &s;
    AX_REQUIRE(ax_thread_create(&t, scene_thread_main, &ctx) == AX_OK);
    ax_thread_join(&t);
    AX_CHECK_RESULT(ctx.result, AX_OK);
    AX_CHECK(!ax_node_store_on_owner_thread(&s));
    ax_node_store_bind_thread(&s); /* back, to tear down */
    AX_CHECK(ax_node_store_is_live(&s, ctx.created));
    ax_node_store_deinit(&s);
}

static int use_store_main(void *arg)
{
    ax_node n;
    (void)ax_node_store_create((ax_node_store *)arg, AX_NODE_VIEW, &n);
    return 0;
}

AX_DEATH_TEST(use_from_a_thread_that_does_not_own_it_aborts)
{
    ax_node_store s;
    ax_thread t;
    if (!ax_threads_supported()) {
        abort();
    }
    ax_node_store_init(&s, NULL);
    AX_REQUIRE(ax_thread_create(&t, use_store_main, &s) == AX_OK);
    ax_thread_join(&t);
}
