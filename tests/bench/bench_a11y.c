/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmarks for the accessibility tree (decision 108). Not a test and not a CI gate:
 * build a Release preset and run `bench_a11y` by hand; decisions.md records the numbers.
 *
 * A tree of 10 101 nodes: a root, 100 lists of 100 list items, every tenth item a text leaf (1000
 * paragraphs, so text runs are built too). For each case, the median and worst of 50 frames:
 *   1. The frame (apply, layout, animate, record, flush) with one label changing, without a sink,
 *      with an inactive sink (no assistive technology), and with an active tree: the inactive tree
 *      must cost nothing (its counters show no node touched).
 *   2. Activation: the whole tree built and sent once.
 *   3. The flush alone, for a frame where 0, 1, 100 and 1000 items changed their label, and one
 *      where every frame changed (the window narrowed: 10 101 bounds).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_a11y.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_ir.h"

#define LISTS 100u
#define ITEMS 100u
#define RUNS  50u

typedef struct bench {
    ax_world w;
    uint64_t now;
    ax_node root;
    ax_node items[LISTS * ITEMS];
    float width;
    uint64_t sent, updates;
    ax_ir_builder b;
} bench;

static uint64_t read_clock(void *user)
{
    return ((bench *)user)->now;
}

static void on_update(void *user, const ax_a11y_update *u)
{
    bench *b = (bench *)user;
    b->sent += u->count;
    b->updates++;
}

static double ms(uint64_t ns)
{
    return (double)ns / 1e6;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* The frame's stages in the runtime's order; returns the flush's own time in *flush_ns. */
static uint64_t frame(bench *b, uint64_t *flush_ns)
{
    const uint64_t t0 = ax_time_monotonic_ns();
    const ax_render_ir *ir = NULL;
    uint64_t f0;
    b->now += 16666667u;
    (void)ax_world_apply_pending(b->w, UINT64_MAX);
    (void)ax_world_set_root_viewport(b->w, b->root, b->width, 800.0f, 1.0f);
    (void)ax_world_position(b->w);
    (void)ax_world_animate(b->w, b->now);
    ax_world_record_collect(b->w);
    if (ax_world_root_changed(b->w, b->root, NULL)) {
        ax_ir_builder_begin(&b->b, b->width, 800.0f, 0, ax_color_make(0, 0, 0, 1));
        (void)ax_world_record_root(b->w, b->root, &b->b);
        (void)ax_ir_builder_end(&b->b, &ir);
    }
    f0 = ax_time_monotonic_ns();
    ax_world_a11y_flush(b->w);
    if (flush_ns != NULL) {
        *flush_ns = ax_time_monotonic_ns() - f0;
    }
    (void)ax_world_publish_mirror(b->w);
    ax_scene_clear_dirty(ax_world_scene(b->w));
    return ax_time_monotonic_ns() - t0;
}

static void build(bench *b)
{
    ax_world_desc d;
    ax_txn *t;
    uint32_t l, i;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.clock = read_clock;
    d.clock_user = b;
    b->now = 1000000000u;
    b->width = 1200.0f;
    (void)ax_world_create(&d, &b->w);
    ax_ir_builder_init(&b->b, ax_default_allocator());
    t = ax_txn_begin(b->w);
    b->root = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_str(t, b->root, AX_PROP_LABEL, "bench", 5);
    ax_txn_set_u32(t, b->root, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, b->root, AX_PROP_FLEX_WRAP, AX_FLEX_WRAP_WRAP);
    for (l = 0; l < LISTS; ++l) {
        ax_node list = ax_txn_create(t, AX_NODE_VIEW);
        ax_dimension w;
        w.value = 10.0f;
        w.unit = AX_UNIT_PERCENT;
        ax_txn_set_dimension(t, list, AX_PROP_WIDTH, w);
        ax_txn_set_u32(t, list, AX_PROP_ROLE, AX_ROLE_LIST);
        ax_txn_append_child(t, b->root, list);
        for (i = 0; i < ITEMS; ++i) {
            ax_node item = ax_txn_create(t, AX_NODE_VIEW);
            char name[32];
            const int n = snprintf(name, sizeof name, "Item %u.%u", (unsigned)l, (unsigned)i);
            ax_dimension h;
            h.value = 8.0f;
            h.unit = AX_UNIT_POINTS;
            if (i % 10u == 0) {
                ax_txn_set_str(t, item, AX_PROP_TEXT, "A short paragraph that wraps in a list.",
                               39);
                ax_txn_set_f32(t, item, AX_PROP_FONT_SIZE, 11.0f);
            } else {
                ax_txn_set_dimension(t, item, AX_PROP_HEIGHT, h);
                ax_txn_set_u32(t, item, AX_PROP_ROLE, AX_ROLE_LIST_ITEM);
                ax_txn_set_str(t, item, AX_PROP_LABEL, name, (size_t)n);
            }
            ax_txn_append_child(t, list, item);
            b->items[l * ITEMS + i] = item;
        }
    }
    (void)ax_txn_commit(t, NULL);
    (void)frame(b, NULL);
}

static void destroy(bench *b)
{
    ax_ir_builder_deinit(&b->b);
    (void)ax_world_destroy(b->w);
}

/* Changes the label of `count` items (not text leaves), spread over the tree. */
static void relabel(bench *b, uint32_t count, uint32_t round)
{
    ax_txn *t = ax_txn_begin(b->w);
    uint32_t k;
    for (k = 0; k < count; ++k) {
        const uint32_t i = (k * 97u + round) % (LISTS * ITEMS);
        const uint32_t item = i % 10u == 0 ? i + 1u : i;
        char name[32];
        const int n = snprintf(name, sizeof name, "Item %u (%u)", (unsigned)item, (unsigned)round);
        ax_txn_set_str(t, b->items[item], AX_PROP_LABEL, name, (size_t)n);
    }
    (void)ax_txn_commit(t, NULL);
}

static void report(const char *what, uint64_t *v, uint32_t n)
{
    qsort(v, n, sizeof v[0], cmp_u64);
    printf("  %-58s median %7.3f ms  worst %7.3f ms\n", what, ms(v[n / 2]), ms(v[n - 1]));
}

static void frames_with_one_change(bench *b, const char *what)
{
    uint64_t t[RUNS];
    uint32_t r;
    for (r = 0; r < RUNS; ++r) {
        relabel(b, 1, r);
        t[r] = frame(b, NULL);
    }
    report(what, t, RUNS);
}

int main(void)
{
    static bench b;
    ax_a11y_sink sink;
    ax_a11y_counters c;
    uint64_t t[RUNS], flush;
    uint32_t r, k;
    static const uint32_t COUNTS[] = {0, 1, 100, 1000};

    build(&b);
    printf("bench_a11y: %u nodes (%u lists of %u, every tenth item a text leaf)\n",
           ax_node_store_count(&ax_world_scene(b.w)->store), LISTS, ITEMS);

    printf("1. A frame with one label changing\n");
    frames_with_one_change(&b, "no sink");
    sink.update = on_update;
    sink.user = &b;
    (void)ax_world_set_a11y_sink(b.w, b.root, &sink);
    (void)ax_world_get_a11y_counters(b.w, &c, true);
    frames_with_one_change(&b, "a sink, no assistive technology");
    ax_world_get_a11y_counters(b.w, &c, true);
    printf("  %-58s touched %llu, built %llu, sent %llu\n", "  (inactive tree's counters)",
           (unsigned long long)c.touched, (unsigned long long)c.built,
           (unsigned long long)c.sent);

    printf("2. Activation\n");
    ax_world_a11y_activate(b.w, b.root, true);
    {
        const uint64_t t0 = ax_time_monotonic_ns();
        (void)frame(&b, &flush);
        (void)t0;
    }
    ax_world_get_a11y_counters(b.w, &c, true);
    printf("  %-58s %7.3f ms  (%llu nodes and runs sent)\n", "the whole tree built and sent",
           ms(flush), (unsigned long long)c.sent);
    frames_with_one_change(&b, "a frame with one label changing, tree active");

    printf("3. The flush alone, tree active\n");
    for (k = 0; k < sizeof COUNTS / sizeof COUNTS[0]; ++k) {
        char what[64];
        b.sent = 0;
        for (r = 0; r < RUNS; ++r) {
            if (COUNTS[k] > 0) {
                relabel(&b, COUNTS[k], r + 100u * k);
            }
            (void)frame(&b, &t[r]);
        }
        snprintf(what, sizeof what, "%u labels changed (%llu nodes sent a frame)",
                 (unsigned)COUNTS[k], (unsigned long long)(b.sent / RUNS));
        report(what, t, RUNS);
    }
    b.sent = 0;
    for (r = 0; r < RUNS; ++r) {
        b.width = r % 2u ? 1200.0f : 1100.0f; /* every frame changes, the paragraphs re-wrap */
        (void)frame(&b, &t[r]);
    }
    {
        char what[64];
        snprintf(what, sizeof what, "the window narrowed (%llu nodes sent a frame)",
                 (unsigned long long)(b.sent / RUNS));
        report(what, t, RUNS);
    }
    destroy(&b);
    return 0;
}
