/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmarks for the core module and the job system. Not a test and not a CI gate:
 * build a Release preset and run `bench_core` by hand; decisions.md records the numbers.
 *
 *   - creating 10 000 nodes in one transaction (recording, applying, publishing the mirror), and
 *     directly in the scene;
 *   - a transaction of 1000 changes on a tree of 10 000 nodes;
 *   - the job system: one tiny job submitted and waited for at a time (wake-up latency, including
 *     notify's broadcast), and 100 000 tiny jobs in one submit.
 *
 * Each case runs REPS times and reports the median and the minimum.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"

enum { REPS = 15, NODES = 10000, GROUPS = 100, CHANGES = 1000 };

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
    printf("  %-44s median %8.3f ms   min %8.3f ms\n", name, t->samples[t->count / 2],
           t->samples[0]);
}

static ax_world make_world(void)
{
    ax_world w = AX_HANDLE_NULL_INIT;
    if (ax_world_create(NULL, &w) != AX_OK) {
        fprintf(stderr, "ax_world_create failed\n");
        exit(1);
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

/* ---- Creating 10 000 nodes ------------------------------------------------------------------- */

static void bench_create(void)
{
    timing record = {{0}, 0};
    timing apply = {{0}, 0};
    timing publish = {{0}, 0};
    timing direct = {{0}, 0};
    int rep;
    for (rep = 0; rep < REPS; ++rep) {
        ax_world w = make_world();
        ax_txn *t;
        ax_node root;
        uint64_t t0;
        uint64_t t1;
        int i;

        t0 = ax_time_monotonic_ns();
        t = ax_txn_begin(w);
        root = ax_txn_create(t, AX_NODE_VIEW);
        for (i = 0; i < NODES; ++i) {
            ax_node n = ax_txn_create(t, AX_NODE_VIEW);
            ax_txn_append_child(t, root, n);
        }
        (void)ax_txn_commit(t, NULL);
        t1 = ax_time_monotonic_ns();
        add(&record, t1 - t0);

        t0 = ax_time_monotonic_ns();
        (void)ax_world_apply_pending(w, UINT64_MAX);
        t1 = ax_time_monotonic_ns();
        add(&apply, t1 - t0);

        t0 = ax_time_monotonic_ns();
        (void)ax_world_publish_mirror(w);
        t1 = ax_time_monotonic_ns();
        add(&publish, t1 - t0);
        (void)ax_world_destroy(w);

        /* The same in the scene directly, without a transaction. */
        {
            ax_scene scene;
            ax_node r;
            ax_scene_init(&scene, NULL);
            t0 = ax_time_monotonic_ns();
            (void)ax_scene_create(&scene, AX_NODE_VIEW, &r);
            for (i = 0; i < NODES; ++i) {
                ax_node n;
                (void)ax_scene_create(&scene, AX_NODE_VIEW, &n);
                (void)ax_scene_insert_before(&scene, r, n, (ax_node)AX_HANDLE_NULL_INIT);
            }
            t1 = ax_time_monotonic_ns();
            add(&direct, t1 - t0);
            ax_scene_deinit(&scene);
        }
    }
    printf("Create %d nodes, appended to one root (cold world each time):\n", NODES);
    report("record and commit the transaction", &record);
    report("apply it (scene thread)", &apply);
    report("publish the mirror", &publish);
    report("the same directly in the scene", &direct);
}

/* ---- A transaction of 1000 changes ----------------------------------------------------------- */

static void bench_changes(void)
{
    timing record = {{0}, 0};
    timing apply = {{0}, 0};
    timing publish = {{0}, 0};
    static ax_node nodes[NODES];
    ax_node groups[GROUPS];
    ax_world w = make_world();
    ax_txn *t;
    ax_node root;
    uint32_t rng = 12345u;
    int rep;
    int i;

    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    for (i = 0; i < GROUPS; ++i) {
        groups[i] = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_append_child(t, root, groups[i]);
        ax_txn_set_dimension(t, groups[i], AX_PROP_WIDTH, points(200.0f));
        ax_txn_set_dimension(t, groups[i], AX_PROP_HEIGHT, points(200.0f)); /* boundaries */
    }
    for (i = 0; i < NODES; ++i) {
        nodes[i] = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_append_child(t, groups[i % GROUPS], nodes[i]);
    }
    (void)ax_txn_commit(t, NULL);
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_publish_mirror(w);
    ax_scene_clear_dirty(ax_world_scene(w));

    for (rep = 0; rep < REPS + 2; ++rep) { /* two warm-up rounds */
        uint64_t t0;
        uint64_t t1;
        t0 = ax_time_monotonic_ns();
        t = ax_txn_begin(w);
        for (i = 0; i < CHANGES; ++i) {
            ax_node n;
            rng = rng * 1664525u + 1013904223u;
            n = nodes[(rng >> 8) % NODES];
            switch (i % 10) {
            case 0: /* 10%: a move to another group (layout, both parents) */
                ax_txn_append_child(t, groups[(rng >> 4) % GROUPS], n);
                break;
            case 1:
            case 2: /* 20%: a width (layout, climbs to the group) */
                ax_txn_set_dimension(t, n, AX_PROP_WIDTH, points((float)(rng % 300u)));
                break;
            case 3:
            case 4: /* 20%: a color (paint) */
                ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR,
                                 ax_rgba_from_srgb8((uint8_t)rng, (uint8_t)(rng >> 8),
                                                    (uint8_t)(rng >> 16), 255));
                break;
            case 5: /* 10%: the four corners (a shorthand) */
                ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS, (float)(rng % 16u));
                break;
            default: /* 40%: opacity (composite) */
                ax_txn_set_f32(t, n, AX_PROP_OPACITY, (float)(rng % 101u) / 100.0f);
                break;
            }
        }
        (void)ax_txn_commit(t, NULL);
        t1 = ax_time_monotonic_ns();
        if (rep >= 2) {
            add(&record, t1 - t0);
        }

        t0 = ax_time_monotonic_ns();
        (void)ax_world_apply_pending(w, UINT64_MAX);
        t1 = ax_time_monotonic_ns();
        if (rep >= 2) {
            add(&apply, t1 - t0);
        }

        t0 = ax_time_monotonic_ns();
        (void)ax_world_publish_mirror(w);
        t1 = ax_time_monotonic_ns();
        if (rep >= 2) {
            add(&publish, t1 - t0);
        }
        ax_scene_clear_dirty(ax_world_scene(w));
        (void)ax_world_mirror_sync(w);
    }
    (void)ax_world_destroy(w);
    printf("A transaction of %d changes on %d nodes in %d fixed-size groups (warm):\n", CHANGES,
           NODES, GROUPS);
    report("record and commit", &record);
    report("apply (scene thread)", &apply);
    report("publish the mirror (10 000-slot scan)", &publish);
}

/* ---- The job system -------------------------------------------------------------------------- */

static void report_us(const char *name, timing *t)
{
    qsort(t->samples, (size_t)t->count, sizeof(double), cmp_double);
    printf("  %-44s median %8.2f us   min %8.2f us\n", name, t->samples[t->count / 2] * 1000.0,
           t->samples[0] * 1000.0);
}

static void empty_job(void *arg)
{
    (void)arg;
}

enum { BATCH = 100000 };
static ax_job g_batch[BATCH];

typedef struct fan_arg {
    ax_job_system *js;
} fan_arg;

/* Runs on a worker: its jobs go to that worker's deque, and the others steal. */
static void fan_out(void *arg)
{
    fan_arg *f = (fan_arg *)arg;
    ax_job_counter c = AX_JOB_COUNTER_INIT;
    ax_jobs_submit(f->js, g_batch, BATCH, &c);
    ax_jobs_wait(f->js, &c);
}

static void spin_ms(uint64_t ms)
{
    uint64_t until = ax_time_monotonic_ns() + ms * 1000000u;
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
}

static void bench_jobs(ax_jobs_mode mode, uint32_t workers)
{
    enum { ONE_AT_A_TIME = 2000, ASLEEP_SAMPLES = 9 };
    timing single = {{0}, 0};
    timing asleep = {{0}, 0};
    timing injected = {{0}, 0};
    timing fanned = {{0}, 0};
    ax_jobs_desc desc = ax_jobs_default_desc();
    ax_job_system *js = NULL;
    fan_arg fa;
    int rep;
    int i;
    desc.mode = mode;
    desc.worker_count = workers;
    desc.deque_capacity = BATCH;
    desc.inject_capacity = BATCH;
    if (ax_jobs_create(&desc, &js) != AX_OK) {
        fprintf(stderr, "ax_jobs_create failed\n");
        exit(1);
    }
    for (i = 0; i < BATCH; ++i) {
        g_batch[i].fn = empty_job;
        g_batch[i].arg = NULL;
    }
    fa.js = js;
    for (rep = 0; rep < REPS; ++rep) {
        uint64_t t0 = ax_time_monotonic_ns();
        ax_job_counter c = AX_JOB_COUNTER_INIT;
        ax_job fan = {fan_out, &fa};
        for (i = 0; i < ONE_AT_A_TIME; ++i) {
            ax_job_counter one = AX_JOB_COUNTER_INIT;
            ax_jobs_submit(js, g_batch, 1, &one);
            ax_jobs_wait(js, &one);
        }
        add(&single, (ax_time_monotonic_ns() - t0) / ONE_AT_A_TIME);

        if (mode == AX_JOBS_THREADED && rep < ASLEEP_SAMPLES) {
            /* The submit alone, once every worker has gone to sleep: notify's broadcast. */
            ax_job_counter one = AX_JOB_COUNTER_INIT;
            spin_ms(5);
            t0 = ax_time_monotonic_ns();
            ax_jobs_submit(js, g_batch, 1, &one);
            add(&asleep, ax_time_monotonic_ns() - t0);
            ax_jobs_wait(js, &one);
        }

        t0 = ax_time_monotonic_ns();
        ax_jobs_submit(js, g_batch, BATCH, &c);
        ax_jobs_wait(js, &c);
        add(&injected, ax_time_monotonic_ns() - t0);

        t0 = ax_time_monotonic_ns();
        ax_jobs_submit(js, &fan, 1, &c);
        ax_jobs_wait(js, &c);
        add(&fanned, ax_time_monotonic_ns() - t0);
    }
    printf("Job system, %s, %u workers (empty jobs):\n",
           mode == AX_JOBS_INLINE ? "inline" : "threaded", ax_jobs_worker_count(js));
    report_us("submit 1 job and wait, per job", &single);
    if (asleep.count > 0) {
        report_us("submit 1 job, every worker asleep", &asleep);
    }
    report("100 000 jobs from a non-worker thread", &injected);
    report("100 000 jobs fanned out from one worker", &fanned);
    ax_jobs_destroy(js);
}

int main(void)
{
    printf("bench_core (%u logical processors)\n\n", ax_cpu_count());
    bench_create();
    printf("\n");
    bench_changes();
    printf("\n");
    bench_jobs(AX_JOBS_THREADED, 4);
    bench_jobs(AX_JOBS_INLINE, 0);
    return 0;
}
