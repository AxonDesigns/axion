/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmarks for the layout engine (decisions 90 and 92). Not a test and not a CI gate:
 * build a Release preset and run `bench_layout` by hand; decisions.md records the numbers.
 *
 * The tree: 10 001 flex nodes, a row of 100 cards (columns with padding and gap) of 99 items each
 * (rows of a fixed height, alternating grow and fixed widths, margins). Two variants: the cards
 * with a fixed size (relayout boundaries) and with an automatic size (no boundary below the root).
 *
 *   - a full layout of a new tree (cold caches), and of the whole tree again after the root's width
 *     changed (every constraint changes);
 *   - an incremental layout after one item's height changed, with and without boundaries;
 *   - one item changed in each of 8 cards: the 8 boundaries in one thread and on 4 workers;
 *   - how many measurement-cache entries the nodes needed (the cache holds 8).
 *
 * Each case runs REPS times and reports the median and the minimum.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_layout.h"
#include "axion/core/ax_scene.h"

enum { REPS = 15, CARDS = 100, ITEMS = 99 };

typedef struct timing {
    double samples[REPS];
    int count;
} timing;

static void add(timing *t, uint64_t ns)
{
    t->samples[t->count++] = (double)ns / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void report(const char *name, timing *t)
{
    qsort(t->samples, (size_t)t->count, sizeof(double), cmp_double);
    printf("  %-52s median %8.3f ms   min %8.3f ms\n", name, t->samples[t->count / 2],
           t->samples[0]);
}

typedef struct tree {
    ax_scene scene;
    ax_node root;
    ax_node cards[CARDS];
    ax_node items[CARDS][ITEMS];
} tree;

static void set(ax_scene *s, ax_node n, ax_prop p, ax_value_type type, ax_value v)
{
    if (ax_scene_set(s, n, p, type, &v) != AX_OK) {
        fprintf(stderr, "set 0x%x failed\n", (unsigned)p);
        exit(1);
    }
}

static ax_value dim(float v)
{
    ax_value x;
    memset(&x, 0, sizeof x);
    x.dimension.value = v;
    x.dimension.unit = AX_UNIT_POINTS;
    return x;
}

static ax_value num(float v)
{
    ax_value x;
    memset(&x, 0, sizeof x);
    x.f32 = v;
    return x;
}

static ax_value enm(uint32_t v)
{
    ax_value x;
    memset(&x, 0, sizeof x);
    x.u32 = v;
    return x;
}

static ax_node make(ax_scene *s, ax_node parent)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (ax_scene_create(s, AX_NODE_VIEW, &n) != AX_OK) {
        fprintf(stderr, "create failed\n");
        exit(1);
    }
    if (!AX_HANDLE_IS_NULL(parent)) {
        (void)ax_scene_insert_before(s, parent, n, (ax_node)AX_HANDLE_NULL_INIT);
    }
    return n;
}

static void build(tree *t, bool boundaries)
{
    ax_node_store_desc desc;
    int c, i;
    memset(&desc, 0, sizeof desc);
    ax_scene_init(&t->scene, &desc);
    t->root = make(&t->scene, (ax_node)AX_HANDLE_NULL_INIT);
    set(&t->scene, t->root, AX_PROP_FLEX_DIRECTION, AX_VALUE_U32, enm(AX_FLEX_DIRECTION_ROW));
    set(&t->scene, t->root, AX_PROP_FLEX_WRAP, AX_VALUE_U32, enm(AX_FLEX_WRAP_WRAP));
    set(&t->scene, t->root, AX_PROP_WIDTH, AX_VALUE_DIMENSION, dim(2400));
    set(&t->scene, t->root, AX_PROP_GAP, AX_VALUE_DIMENSION, dim(4));
    for (c = 0; c < CARDS; ++c) {
        ax_node card = make(&t->scene, t->root);
        t->cards[c] = card;
        set(&t->scene, card, AX_PROP_PADDING, AX_VALUE_DIMENSION, dim(6));
        set(&t->scene, card, AX_PROP_GAP, AX_VALUE_DIMENSION, dim(2));
        if (boundaries) {
            set(&t->scene, card, AX_PROP_WIDTH, AX_VALUE_DIMENSION, dim(220));
            set(&t->scene, card, AX_PROP_HEIGHT, AX_VALUE_DIMENSION, dim(1200));
        } else {
            set(&t->scene, card, AX_PROP_FLEX_GROW, AX_VALUE_F32, num(1));
            set(&t->scene, card, AX_PROP_FLEX_BASIS, AX_VALUE_DIMENSION, dim(200));
        }
        for (i = 0; i < ITEMS; ++i) {
            ax_node item = make(&t->scene, card);
            t->items[c][i] = item;
            set(&t->scene, item, AX_PROP_HEIGHT, AX_VALUE_DIMENSION, dim(9.0f + (float)(i % 3)));
            set(&t->scene, item, AX_PROP_MARGIN_LEFT, AX_VALUE_DIMENSION, dim((float)(i % 4)));
            if (i % 2 == 0) {
                set(&t->scene, item, AX_PROP_FLEX_GROW, AX_VALUE_F32, num(1));
            } else {
                set(&t->scene, item, AX_PROP_WIDTH, AX_VALUE_DIMENSION, dim(150.0f + (float)i));
                set(&t->scene, item, AX_PROP_ALIGN_SELF, AX_VALUE_U32, enm(AX_ALIGN_CENTER));
            }
        }
    }
}

static uint32_t run(tree *t, const ax_layout_pass_desc *d)
{
    uint32_t n = ax_layout_run(&t->scene, d);
    ax_scene_clear_dirty(&t->scene);
    return n;
}

static void bench_full(bool boundaries)
{
    timing cold = {{0}, 0};
    timing again = {{0}, 0};
    ax_layout_pass_desc d;
    int rep;
    memset(&d, 0, sizeof d);
    for (rep = 0; rep < REPS; ++rep) {
        tree *t = (tree *)calloc(1, sizeof *t);
        uint64_t t0, t1;
        build(t, boundaries);
        t0 = ax_time_monotonic_ns();
        (void)run(t, &d);
        t1 = ax_time_monotonic_ns();
        add(&cold, t1 - t0);
        set(&t->scene, t->root, AX_PROP_WIDTH, AX_VALUE_DIMENSION, dim(2300.0f + (float)rep));
        t0 = ax_time_monotonic_ns();
        (void)run(t, &d);
        t1 = ax_time_monotonic_ns();
        add(&again, t1 - t0);
        ax_scene_deinit(&t->scene);
        free(t);
    }
    printf("%s\n", boundaries ? "Cards of a fixed size (100 relayout boundaries)"
                              : "Cards of an automatic size (no boundary)");
    report("full layout, new tree (10 001 nodes)", &cold);
    report("full layout again, the root's width changed", &again);
}

static void bench_incremental(bool boundaries)
{
    timing one = {{0}, 0};
    ax_layout_pass_desc d;
    ax_layout_counters c;
    tree *t = (tree *)calloc(1, sizeof *t);
    int rep;
    memset(&d, 0, sizeof d);
    build(t, boundaries);
    (void)run(t, &d);
    ax_layout_get_counters(&t->scene, &c, true);
    for (rep = 0; rep < REPS; ++rep) {
        uint64_t t0, t1;
        set(&t->scene, t->items[37][rep * 3 + 1], AX_PROP_HEIGHT, AX_VALUE_DIMENSION,
            dim(12.0f + (float)(rep % 2)));
        t0 = ax_time_monotonic_ns();
        (void)run(t, &d);
        t1 = ax_time_monotonic_ns();
        add(&one, t1 - t0);
    }
    ax_layout_get_counters(&t->scene, &c, true);
    report(boundaries ? "one item changed, inside its card's boundary"
                      : "one item changed, no boundary (up to the root)",
           &one);
    printf("    per pass: %.0f laid out, %.0f measured, %.0f from the cache, %.0f frames changed\n",
           (double)c.layouts / REPS, (double)c.measures / REPS, (double)c.cached / REPS,
           (double)c.frames_changed / REPS);
    ax_scene_deinit(&t->scene);
    free(t);
}

static void bench_parallel(void)
{
    timing serial = {{0}, 0};
    timing parallel = {{0}, 0};
    ax_layout_pass_desc ds, dp;
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    tree *t = (tree *)calloc(1, sizeof *t);
    int rep, k;
    jd.worker_count = 4;
    if (ax_jobs_create(&jd, &js) != AX_OK) {
        fprintf(stderr, "no job system\n");
        exit(1);
    }
    memset(&ds, 0, sizeof ds);
    dp = ds;
    dp.jobs = js;
    build(t, true);
    (void)run(t, &ds);
    for (rep = 0; rep < 2 * REPS; ++rep) {
        uint64_t t0, t1;
        /* Items of 8 cards change: each card is a relayout root of the pass. */
        for (k = 0; k < 8; ++k) {
            int i;
            for (i = 0; i < ITEMS; i += 2) {
                set(&t->scene, t->items[k * 12][i], AX_PROP_MARGIN_RIGHT, AX_VALUE_DIMENSION,
                    dim(1.0f + (float)rep)); /* a new value every time */
            }
        }
        t0 = ax_time_monotonic_ns();
        (void)run(t, rep < REPS ? &ds : &dp);
        t1 = ax_time_monotonic_ns();
        add(rep < REPS ? &serial : &parallel, t1 - t0);
    }
    printf("8 cards relaid out together (each ~100 nodes)\n");
    report("one thread", &serial);
    report("4 workers (frame jobs)", &parallel);
    ax_scene_deinit(&t->scene);
    free(t);
    ax_jobs_destroy(js);
}

static void cache_use(void)
{
    tree *t = (tree *)calloc(1, sizeof *t);
    ax_layout_pass_desc d;
    uint32_t hist[AX_LAYOUT_MEASURE_CACHE + 1];
    uint32_t r;
    memset(&d, 0, sizeof d);
    memset(hist, 0, sizeof hist);
    build(t, false);
    (void)run(t, &d);
    for (r = 0; r < t->scene.store.row_count; ++r) {
        uint32_t used = 0, i;
        for (i = 0; i < AX_LAYOUT_MEASURE_CACHE; ++i) {
            used += t->scene.store.layout[r].measures[i].valid != 0;
        }
        hist[used]++;
    }
    printf("Measurement cache entries a node used (a full layout, no boundary)\n  ");
    for (r = 0; r <= AX_LAYOUT_MEASURE_CACHE; ++r) {
        printf("%u: %u   ", (unsigned)r, (unsigned)hist[r]);
    }
    printf("\n");
    ax_scene_deinit(&t->scene);
    free(t);
}

int main(void)
{
    bench_full(true);
    bench_incremental(true);
    bench_full(false);
    bench_incremental(false);
    bench_parallel();
    cache_use();
    return 0;
}
