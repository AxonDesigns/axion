/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Text leaves in layout (decisions 90 and 99): a node with TEXT is measured by its paragraph,
 * wraps in a narrow container, follows its content and inherited style, re-wraps without shaping
 * again, and measures on layout's workers within decision 90's contract.
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_layout.h"
#include "axion/core/ax_scene.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/text/ax_text.h"

static ax_world make_world(void)
{
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    if (ax_world_create(&desc, &w) != AX_OK) {
        w.h = 0;
    }
    return w;
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static void text(ax_txn *t, ax_node n, const char *s)
{
    ax_txn_set_str(t, n, AX_PROP_TEXT, s, strlen(s));
}

static void layout(ax_world w, ax_node root, float vw, float vh)
{
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_set_root_viewport(w, root, vw, vh, 1.0f);
    (void)ax_world_position(w);
    ax_scene_clear_dirty(ax_world_scene(w));
}

static ax_rect frame_of(ax_world w, ax_node n)
{
    const ax_scene *s = ax_world_scene(w);
    return s->store.layout[ax_node_store_row(&s->store, n)].frame;
}

static const char LONG_TEXT[] = "AXION lays text out in lines that wrap at word boundaries.";

AX_TEST(a_text_leaf_is_as_large_as_its_text)
{
    ax_world w = make_world();
    ax_txn *t;
    ax_node root, row, label;
    ax_rect f;
    float tw, th;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    row = ax_txn_create(t, AX_NODE_VIEW);
    label = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, row, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, row, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_f32(t, root, AX_PROP_FONT_SIZE, 20.0f); /* inherited through the row */
    text(t, label, "Hello, world");
    ax_txn_append_child(t, root, row);
    ax_txn_append_child(t, row, label);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    layout(w, root, 400.0f, 300.0f);

    {
        /* What the text system measures for the same text and style. */
        ax_text_system *ts = ax_world_text_system(w);
        ax_font_query q;
        ax_text_style st;
        ax_text_paragraph_desc d;
        ax_text_paragraph *p = NULL;
        memset(&q, 0, sizeof q);
        memset(&st, 0, sizeof st);
        AX_REQUIRE(ax_text_match_font(ts, &q, &st.font) == AX_OK);
        st.size = 20.0f;
        memset(&d, 0, sizeof d);
        d.text = "Hello, world";
        d.length = 12;
        d.styles = &st;
        d.style_count = 1;
        AX_REQUIRE(ax_text_paragraph_create(ts, &d, &p) == AX_OK);
        ax_text_paragraph_measure(p, NAN, &tw, &th);
        ax_text_paragraph_release(p);
    }
    f = frame_of(w, label);
    /* Rounded to the pixel grid (decision 90). */
    AX_CHECK(fabsf(f.w - tw) <= 1.0f);
    AX_CHECK(fabsf(f.h - th) <= 1.0f);
    AX_CHECK(th > 20.0f * 1.3f);

    /* A larger inherited size: the label grows. */
    t = ax_txn_begin(w);
    ax_txn_set_f32(t, root, AX_PROP_FONT_SIZE, 40.0f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    layout(w, root, 400.0f, 300.0f);
    AX_CHECK(fabsf(frame_of(w, label).w - 2.0f * tw) <= 2.0f);

    /* New content: new size. */
    t = ax_txn_begin(w);
    text(t, label, "Hi");
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    layout(w, root, 400.0f, 300.0f);
    AX_CHECK(frame_of(w, label).w < tw);
    ax_world_destroy(w);
}

AX_TEST(text_wraps_in_a_narrow_container_without_shaping_again)
{
    ax_world w = make_world();
    ax_txn *t;
    ax_node root, column, para;
    ax_text_counters c;
    ax_rect wide, narrow;
    uint32_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    column = ax_txn_create(t, AX_NODE_VIEW);
    para = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_dimension(t, column, AX_PROP_WIDTH, pt(600.0f));
    ax_txn_set_u32(t, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    text(t, para, LONG_TEXT);
    ax_txn_append_child(t, root, column);
    ax_txn_append_child(t, column, para);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    layout(w, root, 800.0f, 600.0f);
    wide = frame_of(w, para);
    AX_CHECK_NEAR_F(wide.w, 600.0f, 0.0f); /* stretched across the column */

    ax_text_get_counters(ax_world_text_system(w), &c, true);
    for (i = 0; i < 20; ++i) {
        t = ax_txn_begin(w);
        ax_txn_set_dimension(t, column, AX_PROP_WIDTH, pt(300.0f - 10.0f * (float)i));
        AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        layout(w, root, 800.0f, 600.0f);
    }
    narrow = frame_of(w, para);
    AX_CHECK_NEAR_F(narrow.w, 110.0f, 0.0f);
    AX_CHECK(narrow.h >= 3.0f * wide.h); /* at least three lines */
    ax_text_get_counters(ax_world_text_system(w), &c, false);
    AX_CHECK_EQ_U(c.runs_shaped, 0u); /* wrapped again 20 times, shaped once before */
    AX_CHECK(c.layouts >= 20u);
    ax_world_destroy(w);
}

/* Decision 90's contract, with text as the first real measure function: in a parallel pass the
 * paragraphs measure on the workers (debug builds assert if a measure touches the store). */
AX_TEST(text_measures_on_layout_workers)
{
    ax_world w = make_world();
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    ax_txn *t;
    ax_node root;
    ax_node cards[8];
    ax_node first[8];
    ax_layout_counters lc;
    uint32_t i;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    jd.worker_count = 4;
    AX_REQUIRE(ax_jobs_create(&jd, &js) == AX_OK);
    ax_world_set_jobs(w, js);
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    for (i = 0; i < 8; ++i) {
        uint32_t k;
        cards[i] = ax_txn_create(t, AX_NODE_VIEW);
        /* Fixed size: each card is a relayout boundary. */
        ax_txn_set_dimension(t, cards[i], AX_PROP_WIDTH, pt(180.0f));
        ax_txn_set_dimension(t, cards[i], AX_PROP_HEIGHT, pt(400.0f));
        ax_txn_append_child(t, root, cards[i]);
        /* Over decision 92's threshold of 100 nodes per boundary for jobs. */
        for (k = 0; k < 110; ++k) {
            ax_node p = ax_txn_create(t, AX_NODE_VIEW);
            text(t, p, LONG_TEXT);
            ax_txn_append_child(t, cards[i], p);
            if (k == 0) {
                first[i] = p;
            }
        }
    }
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    layout(w, root, 1600.0f, 600.0f);
    ax_layout_get_counters(ax_world_scene(w), &lc, true);

    /* A change inside every card: independent boundaries, laid out in parallel, whose new
     * paragraphs were shaped in frame jobs. */
    t = ax_txn_begin(w);
    for (i = 0; i < 8; ++i) {
        text(t, first[i], "A new first paragraph, shaped on a worker and measured on one too.");
    }
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_position(w);
    ax_layout_get_counters(ax_world_scene(w), &lc, false);
    AX_CHECK(lc.measure_calls > 0u);
    AX_CHECK(lc.parallel_roots >= 2u);
    ax_scene_clear_dirty(ax_world_scene(w));
    ax_world_set_jobs(w, NULL);
    ax_world_destroy(w);
    ax_jobs_destroy(js);
}

/* ---- Recording (decisions 76 and 101) --------------------------------------------------------- */

static ax_frame_counters record_frame(ax_world w, ax_node root, ax_ir_builder *b)
{
    const ax_render_ir *ir = NULL;
    ax_frame_counters c;
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_set_root_viewport(w, root, 400.0f, 300.0f, 1.0f);
    (void)ax_world_position(w);
    ax_world_record_collect(w);
    ax_world_get_frame_counters(w, &c, true);
    ax_ir_builder_begin(b, 400.0f, 300.0f, 0, ax_color_make(1, 1, 1, 1));
    (void)ax_world_record_root(w, root, b);
    (void)ax_ir_builder_end(b, &ir);
    ax_scene_clear_dirty(ax_world_scene(w));
    ax_world_get_frame_counters(w, &c, true);
    return c;
}

/* The owner's check: glyph runs live in decision 76's per-node item cache, so a change of composite
 * properties alone never records text again. */
AX_TEST(glyph_runs_are_cached_with_the_node_items)
{
    ax_world w = make_world();
    ax_ir_builder b;
    ax_txn *t;
    ax_node root, label;
    ax_frame_counters c;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    ax_ir_builder_init(&b, ax_default_allocator());
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    label = ax_txn_create(t, AX_NODE_VIEW);
    text(t, label, "Cached glyph runs");
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb8(250, 250, 250, 255));
    ax_txn_append_child(t, root, label);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    c = record_frame(w, root, &b);
    AX_CHECK_EQ_U(c.glyph_runs, 1u);
    AX_CHECK_EQ_U(c.glyph_runs_deferred, 0u);

    /* The first opacity promotes the label to a layer: its items are recorded once in it. */
    t = ax_txn_begin(w);
    ax_txn_set_animated(t, false);
    ax_txn_set_f32(t, label, AX_PROP_OPACITY, 0.5f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    (void)record_frame(w, root, &b);
    /* Later composite changes record nothing: the run is copied. */
    t = ax_txn_begin(w);
    ax_txn_set_animated(t, false);
    ax_txn_set_f32(t, label, AX_PROP_OPACITY, 0.75f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    c = record_frame(w, root, &b);
    AX_CHECK_EQ_U(c.items_recorded, 0u);
    AX_CHECK_EQ_U(c.glyph_runs, 0u);
    AX_CHECK(c.items_reused >= 2u); /* the background and the run */

    /* A new color records the run again (paint). */
    t = ax_txn_begin(w);
    ax_txn_set_animated(t, false);
    ax_txn_set_color(t, label, AX_PROP_COLOR, ax_rgba_from_srgb8(200, 0, 0, 255));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    c = record_frame(w, root, &b);
    AX_CHECK_EQ_U(c.glyph_runs, 1u);
    ax_ir_builder_deinit(&b);
    ax_world_destroy(w);
}

/* ---- Frame requests (decision 95's generic wake) ----------------------------------------------- */

typedef struct wake_log {
    ax_atomic_u32 calls;
    ax_atomic_u64 last_root;
} wake_log;

static void on_wake(void *user, ax_node root)
{
    wake_log *l = (wake_log *)user;
    ax_atomic_store_u64(&l->last_root, root.h, AX_MO_RELAXED);
    (void)ax_atomic_fetch_add_u32(&l->calls, 1u, AX_MO_RELEASE);
}

typedef struct request_arg {
    ax_world world;
    ax_node root;
} request_arg;

static int request_from_thread(void *arg)
{
    request_arg *a = (request_arg *)arg;
    ax_world_request_frame(a->world, a->root);
    return 0;
}

/* Glyphs rasterized in the background commit nothing, so they wake the window themselves; the
 * runs that waited are recorded whole at the next frame. A commit and any thread can ask too. */
AX_TEST(background_glyphs_ask_for_a_frame_and_record_whole_runs)
{
    ax_world w = make_world();
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    static wake_log log;
    ax_ir_builder b;
    ax_txn *t;
    ax_node root, label;
    ax_frame_counters c;
    uint64_t start;
    uint32_t before;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    if (!ax_threads_supported()) {
        ax_world_destroy(w);
        AX_SKIP("no threads in this build (glyphs are never deferred)");
    }
    jd.worker_count = 2;
    AX_REQUIRE(ax_jobs_create(&jd, &js) == AX_OK);
    ax_world_set_jobs(w, js);
    ax_world_set_glyph_budget(w, 0); /* every glyph in the background */
    ax_atomic_init_u32(&log.calls, 0);
    ax_atomic_init_u64(&log.last_root, 0);
    ax_world_set_wake_hook(w, on_wake, &log);
    ax_ir_builder_init(&b, ax_default_allocator());

    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    label = ax_txn_create(t, AX_NODE_VIEW);
    text(t, label, "Rasterized on a worker, drawn whole");
    ax_txn_append_child(t, root, label);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&log.calls, AX_MO_ACQUIRE), 1u); /* the commit */
    before = 1;

    c = record_frame(w, root, &b);
    AX_CHECK_EQ_U(c.glyph_runs, 0u);
    AX_CHECK_EQ_U(c.glyph_runs_deferred, 1u); /* left out whole */
    start = ax_time_monotonic_ns();
    while (ax_atomic_load_u32(&log.calls, AX_MO_ACQUIRE) == before &&
           ax_time_monotonic_ns() - start < 5000000000ull) {
        ax_thread_yield();
    }
    AX_CHECK(ax_atomic_load_u32(&log.calls, AX_MO_ACQUIRE) > before); /* no commit: the glyphs */
    AX_CHECK_EQ_U(ax_atomic_load_u64(&log.last_root, AX_MO_RELAXED), 0u); /* every window */
    c = record_frame(w, root, &b);
    AX_CHECK_EQ_U(c.glyph_runs, 1u);
    AX_CHECK_EQ_U(c.glyph_runs_deferred, 0u);

    /* Any thread may ask, for one root. */
    {
        ax_thread th;
        request_arg arg;
        before = ax_atomic_load_u32(&log.calls, AX_MO_ACQUIRE);
        arg.world = w;
        arg.root = root;
        AX_REQUIRE(ax_thread_create(&th, request_from_thread, &arg) == AX_OK);
        (void)ax_thread_join(&th);
        AX_CHECK_EQ_U(ax_atomic_load_u32(&log.calls, AX_MO_ACQUIRE), before + 1u);
        AX_CHECK_EQ_U(ax_atomic_load_u64(&log.last_root, AX_MO_RELAXED), root.h);
    }
    ax_world_set_wake_hook(w, NULL, NULL);
    ax_ir_builder_deinit(&b);
    ax_world_set_jobs(w, NULL);
    ax_world_destroy(w);
    ax_jobs_destroy(js);
}

/* ---- Text color animation and settling scales (decisions 94 and 101) -------------------------- */

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

/* The first glyph run of a snapshot (NULL when none). */
static const ax_ir_glyph_run_item *first_run(const ax_render_ir *ir, ax_ir_glyph_run_item *out)
{
    uint32_t i;
    for (i = 0; i < ir->layer_count; ++i) {
        ax_ir_item_iter it = ax_ir_items(ir, ax_ir_layer_at(ir, i));
        const ax_ir_item_header *h;
        while ((h = ax_ir_item_next(&it)) != NULL) {
            if (h->type == AX_IR_ITEM_GLYPH_RUN) {
                memcpy(out, h, sizeof *out);
                return out;
            }
        }
    }
    return NULL;
}

/* One frame at time t (ns): layout, animation, recording. */
static const ax_render_ir *animated_frame(ax_world w, manual_clock *clock, ax_node root,
                                          ax_ir_builder *b, uint64_t t, ax_frame_counters *c)
{
    const ax_render_ir *ir = NULL;
    clock->now = t;
    (void)ax_world_apply_pending(w, t);
    (void)ax_world_set_root_viewport(w, root, 400.0f, 300.0f, 1.0f);
    (void)ax_world_position(w);
    (void)ax_world_animate(w, t);
    ax_world_record_collect(w);
    ax_world_get_frame_counters(w, c, true);
    ax_ir_builder_begin(b, 400.0f, 300.0f, t, ax_color_make(1, 1, 1, 1));
    (void)ax_world_record_root(w, root, b);
    (void)ax_ir_builder_end(b, &ir);
    ax_scene_clear_dirty(ax_world_scene(w));
    ax_world_get_frame_counters(w, c, true);
    return ir;
}

static ax_world make_clocked_world(manual_clock *clock)
{
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.clock = read_clock;
    desc.clock_user = clock;
    if (ax_world_create(&desc, &w) != AX_OK) {
        w.h = 0;
    }
    return w;
}

#define MS 1000000ull

/* COLOR animates like the background (the default 0.2 s ease), on the node that sets it, and its
 * descendants' text follows the presentation value. */
AX_TEST(an_inherited_text_color_animates)
{
    static manual_clock clock = {1000 * MS};
    ax_world w = make_clocked_world(&clock);
    ax_ir_builder b;
    ax_txn *t;
    ax_node root, panel, label;
    ax_frame_counters c;
    ax_ir_glyph_run_item run;
    const ax_render_ir *ir;
    uint64_t now = 1000 * MS;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    ax_ir_builder_init(&b, ax_default_allocator());
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    panel = ax_txn_create(t, AX_NODE_VIEW);
    label = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, panel, AX_PROP_COLOR, ax_rgba_from_srgb8(0, 0, 0, 255));
    text(t, label, "Fading color");
    ax_txn_append_child(t, root, panel);
    ax_txn_append_child(t, panel, label);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.color.r, 0.0f, 0.0f);

    t = ax_txn_begin(w);
    ax_txn_set_color(t, panel, AX_PROP_COLOR, ax_rgba_from_srgb8(255, 255, 255, 255));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    now += 16 * MS;
    (void)animated_frame(w, &clock, root, &b, now, &c);
    now += 84 * MS; /* half-way through the 0.2 s ease */
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK(run.color.r > 0.1f && run.color.r < 0.9f);
    AX_CHECK_EQ_U(c.glyph_runs, 1u); /* the label's run, again with the new color */
    AX_CHECK(ax_world_root_animating(w, root));
    now += 300 * MS;
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.color.r, 1.0f, 0.0001f);
    ax_ir_builder_deinit(&b);
    ax_world_destroy(w);
}

/* A scaled layer's text keeps its raster while the scale moves and records again at the exact
 * size once the scale has held still for 150 ms; the window gets frames until then. */
AX_TEST(text_under_a_changing_scale_rasterizes_again_once_it_settles)
{
    static manual_clock clock = {1000 * MS};
    ax_world w = make_clocked_world(&clock);
    ax_ir_builder b;
    ax_txn *t;
    ax_node root, layer, label;
    ax_frame_counters c;
    ax_ir_glyph_run_item run;
    const ax_render_ir *ir;
    ax_transform2d m2 = {2, 0, 0, 2, 0, 0}, m3 = {3, 0, 0, 3, 0, 0};
    uint64_t now = 1000 * MS;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    ax_ir_builder_init(&b, ax_default_allocator());
    t = ax_txn_begin(w);
    ax_txn_set_animated(t, false);
    root = ax_txn_create(t, AX_NODE_VIEW);
    layer = ax_txn_create(t, AX_NODE_VIEW);
    label = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_transform(t, layer, AX_PROP_TRANSFORM, m2);
    text(t, label, "Scaled");
    ax_txn_append_child(t, root, layer);
    ax_txn_append_child(t, layer, label);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.raster_scale, 2.0f, 0.0f); /* first seen: settled at once */

    t = ax_txn_begin(w);
    ax_txn_set_animated(t, false);
    ax_txn_set_transform(t, layer, AX_PROP_TRANSFORM, m3);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    now += 16 * MS;
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.raster_scale, 2.0f, 0.0f); /* the old raster, drawn scaled */
    AX_CHECK_EQ_U(c.glyph_runs, 0u);
    AX_CHECK(ax_world_root_animating(w, root)); /* frames go on until it settles */
    now += 100 * MS;
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.raster_scale, 2.0f, 0.0f);
    now += 100 * MS; /* held still 200 ms */
    ir = animated_frame(w, &clock, root, &b, now, &c);
    AX_REQUIRE(first_run(ir, &run) != NULL);
    AX_CHECK_NEAR_F(run.raster_scale, 3.0f, 0.0f);
    AX_CHECK_EQ_U(c.text_rescaled, 1u);
    AX_CHECK_EQ_U(c.glyph_runs, 1u);
    now += 16 * MS;
    (void)animated_frame(w, &clock, root, &b, now, &c);
    AX_CHECK(!ax_world_root_animating(w, root)); /* settled: at rest */
    ax_ir_builder_deinit(&b);
    ax_world_destroy(w);
}

/* The x of the first rect item whose color has red r (in the root's layer space when no layer is
 * between), from a snapshot. */
static bool rect_x(const ax_render_ir *ir, float red, float *out)
{
    uint32_t i;
    for (i = 0; i < ir->layer_count; ++i) {
        const ax_ir_layer *l = ax_ir_layer_at(ir, i);
        ax_ir_item_iter it = ax_ir_items(ir, l);
        const ax_ir_item_header *h;
        while ((h = ax_ir_item_next(&it)) != NULL) {
            if (h->type == AX_IR_ITEM_RECT) {
                ax_ir_rect_item r;
                memcpy(&r, h, sizeof r);
                if (r.color.r == red) {
                    *out = r.rect.x + l->transform.tx; /* the child is in its parent's layer */
                    return true;
                }
            }
        }
    }
    return false;
}

/* The user's rule: an animation ends where pixel snapping puts the node at rest, so nothing jumps
 * when it stops. A parent's resize (the resize preset) lays its child out unrounded each frame;
 * the child carries the target's rounding offset, so the last animated frame and the frame at rest
 * agree, here at the 1.325 scale where rounding offsets are not zero. */
AX_TEST(a_resize_ends_on_the_pixel_grid_without_a_jump)
{
    static manual_clock clock = {1000 * MS};
    ax_world w = make_clocked_world(&clock);
    ax_ir_builder b;
    ax_txn *t;
    ax_node root, parent, child;
    const ax_render_ir *ir;
    uint64_t now = 1000 * MS;
    float x = 0.0f, last = 0.0f, rest = 0.0f;
    uint32_t frames = 0;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    ax_ir_builder_init(&b, ax_default_allocator());
    t = ax_txn_begin(w);
    root = ax_txn_create(t, AX_NODE_VIEW);
    parent = ax_txn_create(t, AX_NODE_VIEW);
    child = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    ax_txn_set_dimension(t, parent, AX_PROP_WIDTH, pt(200.0f));
    ax_txn_set_dimension(t, parent, AX_PROP_HEIGHT, pt(40.0f));
    ax_txn_set_u32(t, parent, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    ax_txn_set_u32(t, parent, AX_PROP_JUSTIFY_CONTENT, AX_JUSTIFY_CENTER);
    ax_txn_set_color(t, parent, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(0.0f, 0.0f, 1.0f, 1.0f));
    ax_txn_set_dimension(t, child, AX_PROP_WIDTH, pt(50.3f));
    ax_txn_set_dimension(t, child, AX_PROP_HEIGHT, pt(20.0f));
    ax_txn_set_color(t, child, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(1.0f, 0.0f, 0.0f, 1.0f));
    ax_txn_append_child(t, root, parent);
    ax_txn_append_child(t, parent, child);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    clock.now = now;
    (void)ax_world_apply_pending(w, now);
    (void)ax_world_set_root_viewport(w, root, 400.0f, 300.0f, 1.325f);
    (void)ax_world_position(w);
    (void)ax_world_animate(w, now);
    ax_world_record_collect(w);
    ax_ir_builder_begin(&b, 400.0f, 300.0f, now, ax_color_make(1, 1, 1, 1));
    (void)ax_world_record_root(w, root, &b);
    (void)ax_ir_builder_end(&b, &ir);
    ax_scene_clear_dirty(ax_world_scene(w));

    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, parent, AX_PROP_WIDTH, pt(261.0f)); /* the default spring */
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    for (;;) {
        now += 16 * MS;
        clock.now = now;
        (void)ax_world_apply_pending(w, now);
        (void)ax_world_set_root_viewport(w, root, 400.0f, 300.0f, 1.325f);
        (void)ax_world_position(w);
        (void)ax_world_animate(w, now);
        ax_world_record_collect(w);
        ax_ir_builder_begin(&b, 400.0f, 300.0f, now, ax_color_make(1, 1, 1, 1));
        (void)ax_world_record_root(w, root, &b);
        (void)ax_ir_builder_end(&b, &ir);
        ax_scene_clear_dirty(ax_world_scene(w));
        AX_REQUIRE(rect_x(ir, 1.0f, &x));
        if (!ax_world_root_animating(w, root)) {
            rest = x;
            break;
        }
        last = x;
        frames++;
        AX_REQUIRE(frames < 200);
    }
    {
        /* At rest: the child's rounded frame, on the 1.325 grid. */
        const ax_scene *s = ax_world_scene(w);
        const ax_rect pf = s->store.layout[ax_node_store_row(&s->store, parent)].frame;
        const ax_rect cf = s->store.layout[ax_node_store_row(&s->store, child)].frame;
        const float device = (pf.x + cf.x) * 1.325f;
        AX_CHECK_NEAR_F(rest, pf.x + cf.x, 0.0001f);
        AX_CHECK_NEAR_F(device, (float)(int32_t)(device + 0.5f), 0.001f);
    }
    /* The last animated frame is where the rest frame is: the spring's last step (under a tenth of
     * a device pixel), not the rounding offset (up to half a pixel). */
    AX_CHECK(frames > 5);
    if (!(fabsf(last - rest) * 1.325f < 0.1f)) {
        ax_test_fail(__FILE__, __LINE__, "last %.4f rest %.4f frames %u", (double)last, (double)rest, frames);
    }
    ax_ir_builder_deinit(&b);
    ax_world_destroy(w);
}
