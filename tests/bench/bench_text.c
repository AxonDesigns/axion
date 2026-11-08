/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reference benchmarks for text (decisions 99 to 101). Not a test and not a CI gate: build a
 * Release preset and run `bench_text` by hand; decisions.md records the numbers.
 *
 *   1. A long paragraph (about 10 000 characters, the bundled Noto Sans at 14): itemizing and
 *      finding its breaks, shaping it (cold cache, then warm), and laying it out at one width
 *      (line breaking and positioning, shaping cached).
 *   2. Re-wrapping it at 60 widths, as an animated width does: per layout, and the runs shaped
 *      again (0 expected) and fragments shaped at unsafe breaks.
 *   3. The frame's stages while a column of 40 paragraphs (11 to 48 px) animates its width with the
 *      default spring through the resize preset: per frame, layout + animation + recording
 *      (median and worst), against a 16.7 ms frame and the 3.8 ms of a 260 Hz refresh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/text/ax_text.h"

#define MS 1000000ull

static const char WORDS[] =
    "AXION lays text out with HarfBuzz and libunibreak, wraps it greedily at the break "
    "opportunities of UAX 14, and draws it from an atlas of coverage glyphs rasterized by "
    "FreeType on worker threads. Office fi fl ffi, AV To Wa, 0123456789. ";

static double ms_since(uint64_t t0)
{
    return (double)(ax_time_monotonic_ns() - t0) / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void paragraph_cases(void)
{
    ax_text_system *ts = NULL;
    ax_text_system_desc d;
    ax_font_query q;
    ax_text_style st;
    ax_text_paragraph_desc pd;
    ax_text_paragraph *p = NULL;
    ax_text_counters c;
    char *text;
    size_t len = 0, wl = sizeof WORDS - 1u;
    uint64_t t0;
    double create_ms, cold_ms, warm_ms, layout_ms, rewrap_ms;
    uint32_t i, lines = 0;
    const ax_text_layout *l;

    memset(&d, 0, sizeof d);
    if (ax_text_system_create(&d, &ts) != AX_OK) {
        printf("text system: failed\n");
        return;
    }
    text = (char *)malloc(12000);
    while (len + wl < 10000) {
        memcpy(text + len, WORDS, wl);
        len += wl;
    }
    memset(&q, 0, sizeof q);
    memset(&st, 0, sizeof st);
    (void)ax_text_match_font(ts, &q, &st.font);
    st.size = 14.0f;
    memset(&pd, 0, sizeof pd);
    pd.text = text;
    pd.length = len;
    pd.styles = &st;
    pd.style_count = 1;

    t0 = ax_time_monotonic_ns();
    (void)ax_text_paragraph_create(ts, &pd, &p);
    create_ms = ms_since(t0);
    t0 = ax_time_monotonic_ns();
    (void)ax_text_paragraph_shape(p);
    cold_ms = ms_since(t0);
    ax_text_paragraph_release(p);
    (void)ax_text_paragraph_create(ts, &pd, &p);
    t0 = ax_time_monotonic_ns();
    (void)ax_text_paragraph_shape(p); /* the same runs: from the cache */
    warm_ms = ms_since(t0);
    t0 = ax_time_monotonic_ns();
    l = ax_text_paragraph_layout(p, 600.0f, 600.0f);
    layout_ms = ms_since(t0);
    lines = l->line_count;
    ax_text_layout_release(l);

    ax_text_get_counters(ts, &c, true);
    t0 = ax_time_monotonic_ns();
    for (i = 0; i < 60; ++i) {
        l = ax_text_paragraph_layout(p, 300.0f + 5.0f * (float)i, NAN);
        ax_text_layout_release(l);
    }
    rewrap_ms = ms_since(t0) / 60.0;
    ax_text_get_counters(ts, &c, false);
    printf("paragraph of %zu bytes at 14 px, %u lines at 600:\n", len, lines);
    printf("  itemize + breaks %.3f ms, shape cold %.3f ms, warm %.3f ms, layout %.3f ms\n",
           create_ms, cold_ms, warm_ms, layout_ms);
    printf("  re-wrap at 60 widths: %.3f ms per layout, runs shaped %llu, fragments %llu\n",
           rewrap_ms, (unsigned long long)c.runs_shaped, (unsigned long long)c.fragments_shaped);
    ax_text_paragraph_release(p);
    ax_text_system_destroy(ts);
    free(text);
}

typedef struct clock_state {
    uint64_t now;
} clock_state;

static uint64_t read_clock(void *u)
{
    return ((clock_state *)u)->now;
}

static void width_animation_case(bool parallel)
{
    static clock_state clock = {1000 * MS};
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    ax_job_system *js = NULL;
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_txn *t;
    ax_node root, column;
    double frame_ms[200];
    ax_dimension width = {900.0f, AX_UNIT_POINTS};
    uint32_t i, n = 0;
    uint64_t now = clock.now;
    static const float sizes[] = {11, 12, 13, 14, 16, 18, 20, 24, 32, 48};

    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.clock = read_clock;
    desc.clock_user = &clock;
    if (ax_world_create(&desc, &w) != AX_OK) {
        return;
    }
    if (parallel) {
        ax_jobs_desc jd = ax_jobs_default_desc();
        if (ax_jobs_create(&jd, &js) == AX_OK) {
            ax_world_set_jobs(w, js);
        }
    }
    ax_ir_builder_init(&b, ax_default_allocator());
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    column = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_dimension(t, column, AX_PROP_WIDTH, width);
    ax_txn_append_child(t, root, column);
    for (i = 0; i < 40; ++i) {
        ax_node p = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_str(t, p, AX_PROP_TEXT, WORDS, sizeof WORDS - 1u);
        ax_txn_set_f32(t, p, AX_PROP_FONT_SIZE, sizes[i % 10u]);
        ax_txn_append_child(t, column, p);
    }
    ax_txn_set_animated(t, false);
    (void)ax_txn_commit(t, NULL);

    /* Warm: the first frame shapes and rasterizes; then the width springs to 400 and back. */
    for (i = 0; i < 160; ++i) {
        uint64_t t0;
        if (i == 2 || i == 80) {
            t = ax_txn_begin(w);
            width.value = i == 2 ? 400.0f : 900.0f;
            ax_txn_set_dimension(t, column, AX_PROP_WIDTH, width);
            (void)ax_txn_commit(t, NULL);
        }
        now += 16666667ull;
        clock.now = now;
        t0 = ax_time_monotonic_ns();
        (void)ax_world_apply_pending(w, now);
        (void)ax_world_set_root_viewport(w, root, 1000.0f, 8000.0f, 1.0f);
        (void)ax_world_position(w);
        (void)ax_world_animate(w, now);
        ax_world_record_collect(w);
        ax_ir_builder_begin(&b, 1000.0f, 8000.0f, now, ax_color_make(1, 1, 1, 1));
        (void)ax_world_record_root(w, root, &b);
        (void)ax_ir_builder_end(&b, &ir);
        ax_scene_clear_dirty(ax_world_scene(w));
        if (i > 2 && ax_world_root_animating(w, root) && n < 200) {
            frame_ms[n++] = ms_since(t0);
        }
    }
    qsort(frame_ms, n, sizeof(double), cmp_double);
    printf("40 paragraphs, width animated 900 -> 400 -> 900 (%s): %u frames, median %.3f ms, "
           "worst %.3f ms per frame (layout + animation + recording)\n",
           parallel ? "jobs" : "one thread", n, n > 0 ? frame_ms[n / 2] : 0.0,
           n > 0 ? frame_ms[n - 1u] : 0.0);
    ax_ir_builder_deinit(&b);
    ax_world_set_jobs(w, NULL);
    ax_world_destroy(w);
    if (js != NULL) {
        ax_jobs_destroy(js);
    }
}

int main(void)
{
    paragraph_cases();
    width_animation_case(false);
    width_animation_case(true);
    return 0;
}
