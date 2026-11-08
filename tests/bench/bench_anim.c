/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmarks for implicit animations (decision 94). Not a test and not a CI gate: build
 * a Release preset and run `bench_anim` by hand; decisions.md records the numbers.
 *
 * The tree is bench_layout's: 10 001 flex nodes, a wrapping row of 100 cards of 99 items each, in
 * a 2400 x 2000 window. Each case runs the frame's stages with a manual clock, one frame every
 * 16.7 ms, and reports the median per frame of each stage, over the frames that animate:
 *   - one card grows (the resize preset: its 99 items laid out again every frame);
 *   - 1000 items fade (opacity springs: the fast path, no item recorded again);
 *   - the cards reorder (every card's frame moves with the default spring);
 * and the mirror's publication while the resize animates (decision 70: the mirror reflects targets,
 * so an animation adds nothing to it). Frames that record items during the fade are reported:
 * they are demotions of layers neutral for a second (the cards the resize moved), each recording
 * its subtree once.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"

enum { CARDS = 100, ITEMS = 99, MAXF = 64 };
#define MS 1000000ull

typedef struct clock_state {
    uint64_t now;
} clock_state;

static uint64_t read_clock(void *u)
{
    return ((clock_state *)u)->now;
}

typedef struct stage_times {
    double apply, layout, animate, record, publish;
    double samples[5][MAXF];
    int n;
} stage_times;

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return n > 0 ? v[n / 2] : 0.0;
}

typedef struct bench {
    clock_state clock;
    ax_world w;
    ax_ir_builder b;
    ax_node root;
    ax_node cards[CARDS];
    ax_node items[CARDS][ITEMS];
} bench;

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static void frame(bench *b, uint64_t t, stage_times *st)
{
    uint64_t t0 = ax_time_monotonic_ns(), t1, t2, t3, t4, t5;
    const ax_render_ir *ir = NULL;
    b->clock.now = t;
    (void)ax_world_apply_pending(b->w, t - 16 * MS);
    t1 = ax_time_monotonic_ns();
    (void)ax_world_set_root_viewport(b->w, b->root, 2400, 2000, 1.0f);
    (void)ax_world_position(b->w);
    t2 = ax_time_monotonic_ns();
    (void)ax_world_animate(b->w, t);
    t3 = ax_time_monotonic_ns();
    ax_world_record_collect(b->w);
    ax_ir_builder_begin(&b->b, 2400, 2000, t, ax_color_make(0, 0, 0, 1));
    (void)ax_world_record_root(b->w, b->root, &b->b);
    (void)ax_ir_builder_end(&b->b, &ir);
    t4 = ax_time_monotonic_ns();
    (void)ax_world_publish_mirror(b->w);
    ax_scene_clear_dirty(ax_world_scene(b->w));
    t5 = ax_time_monotonic_ns();
    if (st != NULL && st->n < MAXF) {
        st->samples[0][st->n] = (double)(t1 - t0) / 1e6;
        st->samples[1][st->n] = (double)(t2 - t1) / 1e6;
        st->samples[2][st->n] = (double)(t3 - t2) / 1e6;
        st->samples[3][st->n] = (double)(t4 - t3) / 1e6;
        st->samples[4][st->n] = (double)(t5 - t4) / 1e6;
        st->n++;
    }
}

static void build(bench *b)
{
    ax_world_desc wd;
    ax_txn *t;
    int c, i;
    memset(b, 0, sizeof *b);
    b->clock.now = 10 * MS;
    memset(&wd, 0, sizeof wd);
    wd.struct_size = sizeof wd;
    wd.clock = read_clock;
    wd.clock_user = &b->clock;
    if (ax_world_create(&wd, &b->w) != AX_OK) {
        fprintf(stderr, "bench_anim: no world\n");
        exit(1);
    }
    ax_ir_builder_init(&b->b, ax_default_allocator());
    t = ax_txn_begin(b->w);
    b->root = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, b->root, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, b->root, AX_PROP_FLEX_WRAP, AX_FLEX_WRAP_WRAP);
    ax_txn_set_u32(t, b->root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_dimension(t, b->root, AX_PROP_GAP, pt(4));
    for (c = 0; c < CARDS; ++c) {
        ax_node card = ax_txn_create(t, AX_NODE_VIEW);
        b->cards[c] = card;
        ax_txn_set_dimension(t, card, AX_PROP_WIDTH, pt(220));
        ax_txn_set_dimension(t, card, AX_PROP_PADDING, pt(6));
        ax_txn_set_dimension(t, card, AX_PROP_GAP, pt(2));
        ax_txn_set_color(t, card, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(0.2f, 0.3f, 0.5f, 1));
        ax_txn_append_child(t, b->root, card);
        for (i = 0; i < ITEMS; ++i) {
            ax_node item = ax_txn_create(t, AX_NODE_VIEW);
            b->items[c][i] = item;
            ax_txn_set_dimension(t, item, AX_PROP_HEIGHT, pt(9.0f + (float)(i % 3)));
            if (i % 2 == 0) {
                ax_txn_set_f32(t, item, AX_PROP_FLEX_GROW, 1);
            } else {
                ax_txn_set_dimension(t, item, AX_PROP_WIDTH, pt(150.0f + (float)i * 0.5f));
            }
            ax_txn_set_color(t, item, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(1, 1, 1, 1));
            ax_txn_append_child(t, card, item);
        }
    }
    (void)ax_txn_commit(t, NULL);
    frame(b, 100 * MS, NULL); /* the first content: never animated */
    frame(b, 117 * MS, NULL);
}

static void report(const char *name, stage_times *st)
{
    printf("%s (%d animating frames)\n", name, st->n);
    printf("  apply %.3f  layout %.3f  animate %.3f  record %.3f  mirror %.3f ms (median)\n",
           median(st->samples[0], st->n), median(st->samples[1], st->n),
           median(st->samples[2], st->n), median(st->samples[3], st->n),
           median(st->samples[4], st->n));
}

/* Runs frames from t until nothing animates (or MAXF frames), measuring them. */
static void run_until_rest(bench *b, uint64_t t, stage_times *st)
{
    int f;
    for (f = 0; f < MAXF; ++f) {
        frame(b, t + (uint64_t)f * 16666667u, st);
        if (ax_world_animate(b->w, t + (uint64_t)f * 16666667u) == 0) {
            break;
        }
    }
}

int main(void)
{
    static bench b;
    stage_times st;
    ax_frame_counters fc;
    ax_txn *t;
    int i;

    /* A card grows: the resize preset relays out its 99 items every frame. */
    build(&b);
    memset(&st, 0, sizeof st);
    b.clock.now = 120 * MS;
    t = ax_txn_begin(b.w);
    ax_txn_set_dimension(t, b.cards[37], AX_PROP_HEIGHT, pt(1800));
    (void)ax_txn_commit(t, NULL);
    ax_world_get_frame_counters(b.w, &fc, true);
    run_until_rest(&b, 150 * MS, &st);
    ax_world_get_frame_counters(b.w, &fc, true);
    report("One card of 100 grows (resize preset, 99 items laid out again each frame)", &st);
    printf("  presentation relayouts %llu, nodes recorded %llu over the animation\n",
           (unsigned long long)fc.present_relayouts, (unsigned long long)fc.nodes_recorded);

    /* 1000 items fade: opacity springs on the fast path. */
    memset(&st, 0, sizeof st);
    b.clock.now = 1300 * MS;
    t = ax_txn_begin(b.w);
    for (i = 0; i < 1000; ++i) {
        ax_txn_set_f32(t, b.items[i % CARDS][i / CARDS], AX_PROP_OPACITY, 0.4f);
    }
    (void)ax_txn_commit(t, NULL);
    ax_world_get_frame_counters(b.w, &fc, true);
    frame(&b, 1333 * MS, &st); /* the first frame promotes the 1000 items to layers */
    ax_world_get_frame_counters(b.w, &fc, true);
    printf("1000 items fade: the first frame records %llu items (the promotions)\n",
           (unsigned long long)fc.items_recorded);
    for (i = 0; i < 60; ++i) {
        frame(&b, 1350 * MS + (uint64_t)i * 16666667u, &st);
        ax_world_get_frame_counters(b.w, &fc, true);
        if (fc.items_recorded != 0) {
            printf("    frame %d (t %.0f ms): %llu items, %llu nodes, %llu demoted\n", i,
                   (1350.0 + i * 16.67), (unsigned long long)fc.items_recorded,
                   (unsigned long long)fc.nodes_recorded, (unsigned long long)fc.layers_demoted);
        }
    }
    report("1000 items fade (opacity springs)", &st);
    printf("  (any frame above that records items is a demotion: layers neutral for a second)\n");

    /* The cards reorder: 100 frames move with the default spring. */
    memset(&st, 0, sizeof st);
    b.clock.now = 2500 * MS;
    t = ax_txn_begin(b.w);
    ax_txn_insert_before(t, b.root, b.cards[99], b.cards[0]);
    (void)ax_txn_commit(t, NULL);
    run_until_rest(&b, 2533 * MS, &st);
    report("The last card moves to the front (every card's frame springs)", &st);
    ax_ir_builder_deinit(&b.b);
    (void)ax_world_destroy(b.w);
    return 0;
}
