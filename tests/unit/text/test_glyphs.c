/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Glyph rasterization and the glyph cache (decision 100): coverage bitmaps at the exact size, the
 * subpixel positions, background rasterization with its callback, and eviction that never takes a
 * glyph a live snapshot uses (the owner's test with a tiny cache).
 */
#include <axion/base/ax_atomic.h>
#include <axion/base/ax_jobs.h>
#include <axion/base/ax_thread.h>

#include "ax_test.h"
#include "test_text_helpers.h"

/* The glyph id of the first glyph of a text. */
static uint32_t glyph_of(ax_text_system *ts, const char *s)
{
    ax_text_paragraph *p = tt_paragraph(ts, s, 16.0f, NULL, 0);
    const ax_text_layout *l = ax_text_paragraph_layout(p, NAN, NAN);
    uint32_t id = l != NULL && l->glyph_count > 0 ? l->glyphs[0].id : 0;
    ax_text_layout_release(l);
    ax_text_paragraph_release(p);
    return id;
}

static uint32_t coverage_sum(const ax_glyph_bitmap *b)
{
    uint32_t x, y, sum = 0;
    for (y = 0; y < b->height; ++y) {
        for (x = 0; x < b->width; ++x) {
            sum += b->pixels[(size_t)y * b->stride + x];
        }
    }
    return sum;
}

AX_TEST(glyphs_rasterize_at_the_exact_size_with_four_subpixel_positions)
{
    ax_text_system *ts = tt_system();
    ax_font_instance in16, in64;
    ax_glyph_key k;
    ax_glyph_bitmap b[4], big, space;
    uint32_t i;
    AX_REQUIRE(ts != NULL);
    in16 = ax_text_font_instance(ts, tt_font(ts, 400.0f), 16.0f, NULL);
    in64 = ax_text_font_instance(ts, tt_font(ts, 400.0f), 64.0f, NULL);
    k.instance = in16;
    k.glyph = glyph_of(ts, "H");
    for (i = 0; i < 4; ++i) {
        k.bin = i;
        AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, &k, 1, 1, NULL, NULL), 0u);
        AX_REQUIRE(ax_text_glyph_lookup(ts, &k, &b[i]));
    }
    /* An H of Noto Sans at 16 px: about 8.4 px wide (cap height 0.714 em: 11-12 rows). */
    AX_CHECK(b[0].width >= 8u && b[0].width <= 12u);
    AX_CHECK(b[0].height >= 11u && b[0].height <= 13u);
    AX_CHECK(b[0].top >= 11 && b[0].top <= 13);
    /* Each subpixel position is another bitmap with about the same ink. */
    for (i = 1; i < 4; ++i) {
        uint32_t s0 = coverage_sum(&b[0]), si = coverage_sum(&b[i]);
        AX_CHECK(b[i].pixels != b[0].pixels);
        AX_CHECK(si > s0 * 9u / 10u && si < s0 * 11u / 10u);
    }
    AX_CHECK(b[2].width > b[0].width || memcmp(b[2].pixels, b[0].pixels, b[0].width) != 0);

    /* Four times the size: about sixteen times the ink. */
    k.instance = in64;
    k.bin = 0;
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, &k, 1, 1, NULL, NULL), 0u);
    AX_REQUIRE(ax_text_glyph_lookup(ts, &k, &big));
    AX_CHECK(coverage_sum(&big) > coverage_sum(&b[0]) * 12u);

    /* A space has no pixels but is ready. */
    k.instance = in16;
    k.glyph = glyph_of(ts, " ");
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, &k, 1, 1, NULL, NULL), 0u);
    AX_REQUIRE(ax_text_glyph_lookup(ts, &k, &space));
    AX_CHECK_EQ_U(space.width * space.height, 0u);
    ax_text_system_destroy(ts);
}

typedef struct ready_flag {
    ax_atomic_u32 calls;
} ready_flag;

static void on_ready(void *user)
{
    (void)ax_atomic_fetch_add_u32(&((ready_flag *)user)->calls, 1u, AX_MO_RELEASE);
}

AX_TEST(glyphs_past_the_budget_rasterize_in_the_background)
{
    ax_text_system *ts = tt_system();
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    ready_flag flag;
    ax_glyph_key keys[40];
    uint32_t budget = 10, i, missing;
    uint64_t start;
    AX_REQUIRE(ts != NULL);
    if (!ax_threads_supported()) {
        ax_text_system_destroy(ts);
        AX_SKIP("no threads in this build (background jobs run inline)");
    }
    jd.worker_count = 2;
    AX_REQUIRE(ax_jobs_create(&jd, &js) == AX_OK);
    ax_atomic_init_u32(&flag.calls, 0);
    ax_text_set_glyphs_ready_callback(ts, on_ready, &flag);
    for (i = 0; i < 40; ++i) {
        keys[i].instance = ax_text_font_instance(ts, tt_font(ts, 400.0f), 20.0f, NULL);
        keys[i].glyph = 30u + i;
        keys[i].bin = 0;
    }
    missing = ax_text_glyphs_ensure(ts, keys, 40, 1, js, &budget);
    AX_CHECK_EQ_U(budget, 0u);
    AX_CHECK_EQ_U(missing, 30u); /* 10 now, 30 later */
    start = ax_time_monotonic_ns();
    while (ax_atomic_load_u32(&flag.calls, AX_MO_ACQUIRE) == 0 &&
           ax_time_monotonic_ns() - start < 5000000000ull) {
        ax_thread_yield();
    }
    AX_CHECK(ax_atomic_load_u32(&flag.calls, AX_MO_ACQUIRE) >= 1u);
    ax_text_system_destroy(ts); /* waits for the batches */
    ax_jobs_destroy(js);
}

/* The owner's rule: no glyph a snapshot that may still be drawn uses is evicted, whatever the
 * budget. A cache of one byte keeps everything used at the live epoch. */
AX_TEST(a_tiny_cache_keeps_the_glyphs_of_live_snapshots)
{
    ax_text_system *ts = NULL;
    ax_text_system_desc d;
    ax_glyph_key keys[50];
    ax_glyph_bitmap b;
    ax_text_counters c;
    uint32_t i;
    memset(&d, 0, sizeof d);
    d.glyph_cache_bytes = 1;
    AX_REQUIRE(ax_text_system_create(&d, &ts) == AX_OK);
    for (i = 0; i < 50; ++i) {
        keys[i].instance = ax_text_font_instance(ts, tt_font(ts, 400.0f), 18.0f, NULL);
        keys[i].glyph = 40u + i;
        keys[i].bin = i % 4u;
    }
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, keys, 25, 1, NULL, NULL), 0u);      /* snapshot 1 */
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, keys + 25, 25, 2, NULL, NULL), 0u); /* snapshot 2 */
    ax_text_get_counters(ts, &c, true);

    /* Snapshot 1 may still be drawn: nothing goes. */
    AX_CHECK_EQ_U(ax_text_glyphs_retire(ts, 1), 0u);
    ax_text_get_counters(ts, &c, false);
    AX_CHECK(c.glyph_pinned_skips >= 50u);
    for (i = 0; i < 50; ++i) {
        AX_CHECK(ax_text_glyph_lookup(ts, &keys[i], &b));
    }
    /* Snapshot 1 retired, snapshot 2 live: only the first 25 go. */
    AX_CHECK_EQ_U(ax_text_glyphs_retire(ts, 2), 25u);
    for (i = 0; i < 25; ++i) {
        AX_CHECK(!ax_text_glyph_lookup(ts, &keys[i], &b));
    }
    for (i = 25; i < 50; ++i) {
        AX_CHECK(ax_text_glyph_lookup(ts, &keys[i], &b));
    }
    /* A glyph used again by a newer snapshot moves to that epoch and stays. */
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, keys + 25, 1, 3, NULL, NULL), 0u);
    AX_CHECK_EQ_U(ax_text_glyphs_retire(ts, 3), 24u);
    AX_CHECK(ax_text_glyph_lookup(ts, &keys[25], &b));
    /* Rasterized again on demand. */
    AX_CHECK_EQ_U(ax_text_glyphs_ensure(ts, keys, 1, 4, NULL, NULL), 0u);
    AX_CHECK(ax_text_glyph_lookup(ts, &keys[0], &b));
    ax_text_get_counters(ts, &c, false);
    AX_CHECK_EQ_U(c.glyphs_evicted, 49u);
    ax_text_system_destroy(ts);
}
