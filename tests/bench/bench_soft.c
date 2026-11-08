/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmark for ax_render_soft (decision 87): m1_nodes' scene (examples/m1_nodes,
 * 800 x 500 logical units) recorded headless over 60 frames of its animation, then each snapshot
 * drawn at scale 1 (800 x 500 pixels) and 2 (1600 x 1000), with the scalar and the SIMD path, on
 * the calling thread and on a job system of N workers. Not a test and not a CI gate: build a
 * Release preset and run `bench_soft [workers]` by hand; decisions.md records the numbers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"
#include "axion/headless/ax_headless.h"
#include "axion/render/ax_render_soft.h"
#include "m1_scene.h"

enum { FRAMES = 60, REPS = 5 };

typedef struct snapshot {
    uint8_t *data;
    size_t size;
} snapshot;

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Median and 95th percentile of the frames' draw times, in ms. */
static void run(const char *name, const snapshot *snaps, float scale, uint32_t flags,
                ax_job_system *js)
{
    const uint32_t w = (uint32_t)(800.0f * scale), h = (uint32_t)(500.0f * scale);
    ax_render_soft_desc d;
    ax_render_soft *r = NULL;
    ax_soft_target t;
    static double samples[FRAMES * REPS];
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4u);
    ax_arena arena;
    uint32_t i, rep, n = 0;
    memset(&d, 0, sizeof d);
    d.flags = flags;
    d.jobs = js;
    if (px == NULL || ax_render_soft_create(NULL, &d, &r) != AX_OK) {
        fprintf(stderr, "bench_soft: setup failed\n");
        exit(1);
    }
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = px;
    t.width = w;
    t.height = h;
    t.stride = (size_t)w * 4u;
    t.format = AX_SOFT_BGRA8;
    t.scale = scale;
    ax_arena_init(&arena, ax_default_allocator(), 0);
    for (rep = 0; rep < REPS; ++rep) {
        for (i = 0; i < FRAMES; ++i) {
            const ax_render_ir *ir = NULL;
            uint64_t t0;
            ax_arena_reset(&arena);
            if (ax_ir_deserialize(snaps[i].data, snaps[i].size, &arena, &ir) != AX_OK) {
                fprintf(stderr, "bench_soft: bad snapshot\n");
                exit(1);
            }
            t0 = ax_time_monotonic_ns();
            (void)ax_render_soft_draw_ir(r, &t, ir, NULL);
            samples[n++] = (double)(ax_time_monotonic_ns() - t0) / 1e6;
        }
    }
    qsort(samples, n, sizeof(double), cmp_double);
    printf("  %-40s %5ux%-5u median %7.3f ms   p95 %7.3f ms\n", name, w, h, samples[n / 2],
           samples[n * 95 / 100]);
    ax_arena_deinit(&arena);
    ax_render_soft_destroy(r);
    free(px);
}

int main(int argc, char **argv)
{
    ax_headless_desc hd = ax_headless_default_desc();
    ax_headless_surface_desc sd = ax_headless_default_surface_desc();
    static m1_moving m;
    static snapshot snaps[FRAMES];
    ax_headless *h = NULL;
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    ax_node root;
    uint32_t i, workers;
    char name[64];
    float scale;

    /* Record: the scene and 60 frames of its animation. */
    hd.driver = m1_scene_slide;
    hd.driver_user = &m;
    sd.width = 800.0f;
    sd.height = 500.0f;
    sd.scale = 1.0f;
    if (ax_headless_create(&hd, &h) != AX_OK || ax_headless_add_surface(h, &sd, &root) != AX_OK) {
        fprintf(stderr, "bench_soft: headless setup failed\n");
        return 1;
    }
    ax_atomic_init_u32(&m.ready, 0);
    (void)m1_scene_build(ax_headless_world(h), root, &m, 800.0f, 500.0f, NULL);
    (void)ax_headless_settle(h, 10, NULL);
    for (i = 0; i < FRAMES; ++i) {
        if (ax_headless_step(h, 1) != AX_OK ||
            ax_headless_save_ir(h, root, &snaps[i].data, &snaps[i].size) != AX_OK) {
            fprintf(stderr, "bench_soft: recording failed\n");
            return 1;
        }
    }

    jd.worker_count = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
    if (ax_jobs_create(&jd, &js) != AX_OK) {
        fprintf(stderr, "bench_soft: no job system\n");
        return 1;
    }
    workers = ax_jobs_worker_count(js);
    printf("ax_render_soft, m1_nodes' scene, %d frames x %d, SIMD path: %s, %u CPUs\n", FRAMES,
           REPS, ax_render_soft_has_simd() ? "yes" : "no", ax_cpu_count());
    for (scale = 1.0f; scale <= 2.0f; scale += 1.0f) {
        run("scalar, 1 thread", snaps, scale, AX_RENDER_SOFT_SCALAR, NULL);
        run("SIMD, 1 thread", snaps, scale, 0, NULL);
        snprintf(name, sizeof name, "scalar, %u workers + caller", workers);
        run(name, snaps, scale, AX_RENDER_SOFT_SCALAR, js);
        snprintf(name, sizeof name, "SIMD, %u workers + caller", workers);
        run(name, snaps, scale, 0, js);
    }
    ax_jobs_destroy(js);
    for (i = 0; i < FRAMES; ++i) {
        ax_headless_free(h, snaps[i].data, snaps[i].size);
    }
    ax_headless_destroy(h);
    return 0;
}
