/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The layout engine (decision 90): flex basics checked by hand, rounding to the pixel grid, leaves
 * with a measure function, and the pass over dirty layout roots. The conformance suite against
 * Yoga's expectations is test_layout_yoga.c (decision 91).
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/core/ax_layout.h"
#include "axion/core/ax_scene.h"

typedef struct fixture {
    ax_tracking_allocator tracker;
    ax_allocator alloc;
    ax_scene scene;
} fixture;

static void fixture_init(fixture *f)
{
    ax_node_store_desc desc;
    ax_tracking_allocator_init(&f->tracker, ax_default_allocator());
    f->alloc = ax_tracking_allocator_get(&f->tracker);
    memset(&desc, 0, sizeof desc);
    desc.allocator = &f->alloc;
    ax_scene_init(&f->scene, &desc);
}

static size_t fixture_deinit(fixture *f)
{
    ax_scene_deinit(&f->scene);
    return ax_tracking_allocator_deinit(&f->tracker);
}

static ax_node node(fixture *f, ax_node parent)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (ax_scene_create(&f->scene, AX_NODE_VIEW, &n) != AX_OK) {
        n.h = 0;
    }
    if (!AX_HANDLE_IS_NULL(parent)) {
        (void)ax_scene_insert_before(&f->scene, parent, n, (ax_node)AX_HANDLE_NULL_INIT);
    }
    return n;
}

static void set_dim(fixture *f, ax_node n, ax_prop p, float v, ax_unit unit)
{
    ax_value val;
    memset(&val, 0, sizeof val);
    val.dimension.value = v;
    val.dimension.unit = (uint32_t)unit;
    AX_CHECK_RESULT(ax_scene_set(&f->scene, n, p, AX_VALUE_DIMENSION, &val), AX_OK);
}

static void pts(fixture *f, ax_node n, ax_prop p, float v)
{
    set_dim(f, n, p, v, AX_UNIT_POINTS);
}

static void set_u32(fixture *f, ax_node n, ax_prop p, uint32_t v)
{
    ax_value val;
    memset(&val, 0, sizeof val);
    val.u32 = v;
    AX_CHECK_RESULT(ax_scene_set(&f->scene, n, p, AX_VALUE_U32, &val), AX_OK);
}

static void set_f32(fixture *f, ax_node n, ax_prop p, float v)
{
    ax_value val;
    memset(&val, 0, sizeof val);
    val.f32 = v;
    AX_CHECK_RESULT(ax_scene_set(&f->scene, n, p, AX_VALUE_F32, &val), AX_OK);
}

static ax_rect frame_of(fixture *f, ax_node n)
{
    return f->scene.store.layout[ax_node_store_row(&f->scene.store, n)].frame;
}

#define CHECK_FRAME(f_, n_, x_, y_, w_, h_) \
    do {                                    \
        ax_rect r_ = frame_of((f_), (n_));  \
        AX_CHECK_NEAR_F(r_.x, (x_), 1e-4);  \
        AX_CHECK_NEAR_F(r_.y, (y_), 1e-4);  \
        AX_CHECK_NEAR_F(r_.w, (w_), 1e-4);  \
        AX_CHECK_NEAR_F(r_.h, (h_), 1e-4);  \
    } while (0)

AX_TEST(a_row_grows_the_flexible_item_and_stretches_every_item)
{
    fixture f;
    ax_node root, a, b, c;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 300);
    pts(&f, root, AX_PROP_HEIGHT, 100);
    set_u32(&f, root, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    a = node(&f, root);
    pts(&f, a, AX_PROP_WIDTH, 50);
    b = node(&f, root);
    set_f32(&f, b, AX_PROP_FLEX_GROW, 1.0f);
    c = node(&f, root);
    pts(&f, c, AX_PROP_WIDTH, 50);
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
    CHECK_FRAME(&f, root, 0, 0, 300, 100);
    CHECK_FRAME(&f, a, 0, 0, 50, 100);
    CHECK_FRAME(&f, b, 50, 0, 200, 100);
    CHECK_FRAME(&f, c, 250, 0, 50, 100);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(a_column_with_padding_and_gap_sizes_itself_from_its_content)
{
    fixture f;
    ax_node root, a, b;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 200);
    pts(&f, root, AX_PROP_PADDING, 10);
    pts(&f, root, AX_PROP_GAP, 5);
    a = node(&f, root);
    pts(&f, a, AX_PROP_HEIGHT, 20);
    b = node(&f, root);
    pts(&f, b, AX_PROP_HEIGHT, 20);
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
    CHECK_FRAME(&f, root, 0, 0, 200, 65);
    CHECK_FRAME(&f, a, 10, 10, 180, 20);
    CHECK_FRAME(&f, b, 10, 35, 180, 20);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(center_alignment_absolute_insets_percentages_and_display_none)
{
    fixture f;
    ax_node root, centered, abs, half, hidden, hidden_child;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 100);
    pts(&f, root, AX_PROP_HEIGHT, 100);
    set_u32(&f, root, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    set_u32(&f, root, AX_PROP_JUSTIFY_CONTENT, AX_JUSTIFY_CENTER);
    set_u32(&f, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_CENTER);
    hidden = node(&f, root);
    set_u32(&f, hidden, AX_PROP_DISPLAY, AX_DISPLAY_NONE);
    pts(&f, hidden, AX_PROP_WIDTH, 30);
    hidden_child = node(&f, hidden);
    pts(&f, hidden_child, AX_PROP_WIDTH, 10);
    centered = node(&f, root);
    pts(&f, centered, AX_PROP_WIDTH, 20);
    pts(&f, centered, AX_PROP_HEIGHT, 20);
    abs = node(&f, root);
    set_u32(&f, abs, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    pts(&f, abs, AX_PROP_RIGHT, 10);
    pts(&f, abs, AX_PROP_BOTTOM, 10);
    pts(&f, abs, AX_PROP_WIDTH, 20);
    pts(&f, abs, AX_PROP_HEIGHT, 20);
    half = node(&f, abs);
    set_dim(&f, half, AX_PROP_WIDTH, 50, AX_UNIT_PERCENT);
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
    CHECK_FRAME(&f, centered, 40, 40, 20, 20);
    CHECK_FRAME(&f, abs, 70, 70, 20, 20);
    CHECK_FRAME(&f, half, 0, 0, 10, 0);
    CHECK_FRAME(&f, hidden, 0, 0, 0, 0);
    CHECK_FRAME(&f, hidden_child, 0, 0, 0, 0);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(frames_round_to_the_pixel_grid_of_the_scale)
{
    fixture f;
    ax_node root, k[3];
    int i;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 100);
    pts(&f, root, AX_PROP_HEIGHT, 10);
    set_u32(&f, root, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    for (i = 0; i < 3; ++i) {
        k[i] = node(&f, root);
        set_f32(&f, k[i], AX_PROP_FLEX_GROW, 1.0f);
    }
    /* 33.33 points each; at 2 pixels per point, edges land on half points and the widths add up. */
    ax_layout_tree(&f.scene, root, NAN, NAN, 2.0f);
    CHECK_FRAME(&f, k[0], 0, 0, 33.5f, 10);
    CHECK_FRAME(&f, k[1], 33.5f, 0, 33.0f, 10);
    CHECK_FRAME(&f, k[2], 66.5f, 0, 33.5f, 10);
    /* Without rounding: the exact thirds. */
    ax_layout_tree(&f.scene, root, NAN, NAN, 0.0f);
    AX_CHECK_NEAR_F(frame_of(&f, k[1]).x, 100.0f / 3.0f, 1e-4);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

static void measure_text(void *user, ax_node n, float width, ax_layout_mode wm, float height,
                         ax_layout_mode hm, float *out_w, float *out_h)
{
    int *calls = (int *)user;
    (void)n;
    (void)height;
    (void)hm;
    (*calls)++;
    /* 120 points of text, 10 points per line: wraps to the width it is given. */
    if (wm == AX_LAYOUT_UNDEFINED || width >= 120.0f) {
        *out_w = 120.0f;
        *out_h = 10.0f;
    } else {
        *out_w = width;
        *out_h = 10.0f * ceilf(120.0f / width);
    }
}

AX_TEST(a_leaf_with_a_measure_function_wraps_to_its_width_and_is_cached)
{
    fixture f;
    ax_node root, text;
    int calls = 0;
    ax_layout_counters c;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 50);
    pts(&f, root, AX_PROP_PADDING_LEFT, 10);
    text = node(&f, root);
    AX_REQUIRE(ax_layout_set_measure(&f.scene, text, measure_text, &calls) == AX_OK);
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
    /* The column stretches the text to 40 points: 3 lines. */
    CHECK_FRAME(&f, text, 10, 0, 40, 30);
    CHECK_FRAME(&f, root, 0, 0, 50, 30);
    AX_CHECK(calls >= 1);
    ax_layout_get_counters(&f.scene, &c, true);
    AX_CHECK_EQ_U(c.measure_calls, (uint64_t)calls);
    /* Nothing dirty: the cache answers, the function is not called again. */
    ax_scene_clear_dirty(&f.scene);
    calls = 0;
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
    AX_CHECK_EQ_I(calls, 0);
    CHECK_FRAME(&f, text, 10, 0, 40, 30);
    AX_CHECK_RESULT(
        ax_layout_set_measure(&f.scene, (ax_node)AX_HANDLE_NULL_INIT, measure_text, &calls),
        AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

static bool one_viewport(void *user, ax_node root, float *w, float *h, float *scale)
{
    const ax_node *window = (const ax_node *)user;
    if (root.h != window->h) {
        return false;
    }
    *w = 320.0f;
    *h = 240.0f;
    *scale = 1.0f;
    return true;
}

typedef struct changes {
    uint32_t slots[64];
    uint32_t count;
} changes;

static void on_changed(void *user, uint32_t slot)
{
    changes *c = (changes *)user;
    if (c->count < 64u) {
        c->slots[c->count] = slot;
    }
    c->count++;
}

AX_TEST(the_pass_lays_out_dirty_roots_and_reports_changed_frames)
{
    fixture f;
    ax_node root, panel, item;
    ax_layout_pass_desc d;
    changes ch;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 10); /* a window root ignores its own size and margins */
    pts(&f, root, AX_PROP_MARGIN, 7);
    panel = node(&f, root);
    pts(&f, panel, AX_PROP_HEIGHT, 50);
    item = node(&f, panel);
    pts(&f, item, AX_PROP_HEIGHT, 10);
    memset(&d, 0, sizeof d);
    memset(&ch, 0, sizeof ch);
    d.viewport = one_viewport;
    d.viewport_user = &root;
    d.frame_changed = on_changed;
    d.frame_changed_user = &ch;
    AX_CHECK_EQ_U(ax_layout_run(&f.scene, &d), 3);
    AX_CHECK_EQ_U(ch.count, 3);
    CHECK_FRAME(&f, root, 0, 0, 320, 240);
    CHECK_FRAME(&f, panel, 0, 0, 320, 50);
    CHECK_FRAME(&f, item, 0, 0, 320, 10);
    ax_scene_clear_dirty(&f.scene);

    /* Nothing dirty, nothing laid out. */
    ch.count = 0;
    AX_CHECK_EQ_U(ax_layout_run(&f.scene, &d), 0);
    AX_CHECK_EQ_U(ch.count, 0);

    /* A change of the item: one frame changes, and only it is reported. */
    pts(&f, item, AX_PROP_HEIGHT, 20);
    AX_CHECK_EQ_U(ax_layout_run(&f.scene, &d), 1);
    AX_CHECK_EQ_U(ch.count, 1);
    AX_CHECK_EQ_U(ch.slots[0], ax_handle_slot(item.h));
    CHECK_FRAME(&f, item, 0, 0, 320, 20);
    ax_scene_clear_dirty(&f.scene);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- Relayout boundaries (decision 92)
 * ----------------------------------------------------------- */

typedef struct app_scene {
    ax_node window, sidebar, main, list;
    ax_node side_items[40];
    ax_node list_items[60];
} app_scene;

/* A window with a fixed-size sidebar and a fixed-size list, both relayout boundaries. */
static void build_app(fixture *f, app_scene *a)
{
    int i;
    a->window = node(f, (ax_node)AX_HANDLE_NULL_INIT);
    set_u32(f, a->window, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    a->sidebar = node(f, a->window);
    pts(f, a->sidebar, AX_PROP_WIDTH, 200);
    pts(f, a->sidebar, AX_PROP_HEIGHT, 600);
    a->main = node(f, a->window);
    set_f32(f, a->main, AX_PROP_FLEX_GROW, 1);
    pts(f, a->main, AX_PROP_PADDING, 8);
    a->list = node(f, a->main);
    pts(f, a->list, AX_PROP_WIDTH, 300);
    pts(f, a->list, AX_PROP_HEIGHT, 580);
    pts(f, a->list, AX_PROP_GAP, 2);
    for (i = 0; i < 40; ++i) {
        a->side_items[i] = node(f, a->sidebar);
        pts(f, a->side_items[i], AX_PROP_HEIGHT, 12);
    }
    for (i = 0; i < 60; ++i) {
        a->list_items[i] = node(f, a->list);
        pts(f, a->list_items[i], AX_PROP_HEIGHT, 8);
        pts(f, a->list_items[i], AX_PROP_MARGIN_LEFT, (float)(i % 5));
    }
}

static bool app_viewport(void *user, ax_node root, float *w, float *h, float *scale)
{
    const app_scene *a = (const app_scene *)user;
    if (root.h != a->window.h) {
        return false;
    }
    *w = 800.0f;
    *h = 600.0f;
    *scale = 1.0f;
    return true;
}

static uint32_t pass_of(fixture *f, ax_node n)
{
    return f->scene.store.layout[ax_node_store_row(&f->scene.store, n)].pass;
}

AX_TEST(editing_a_child_lays_out_nothing_outside_its_boundary)
{
    fixture f;
    app_scene a;
    ax_layout_pass_desc d;
    ax_layout_counters c;
    fixture_init(&f);
    build_app(&f, &a);
    memset(&d, 0, sizeof d);
    d.viewport = app_viewport;
    d.viewport_user = &a;
    (void)ax_layout_run(&f.scene, &d);
    ax_scene_clear_dirty(&f.scene);
    CHECK_FRAME(&f, a.list, 8, 8, 300, 580);
    CHECK_FRAME(&f, a.list_items[1], 1, 10, 299, 8);
    ax_layout_get_counters(&f.scene, &c, true);

    /* An item of the list grows: the list is the only layout root. */
    pts(&f, a.list_items[10], AX_PROP_HEIGHT, 30);
    AX_CHECK(ax_scene_is_relayout_boundary(&f.scene, a.list));
    AX_CHECK_EQ_U(f.scene.dirty.layout_root_count, 1);
    (void)ax_layout_run(&f.scene, &d);
    ax_layout_get_counters(&f.scene, &c, true);
    AX_CHECK_EQ_U(c.roots, 1);
    /* Laid out: the list and the item that changed (measured, then laid out stretched, as in
     * Yoga). The other 59 items keep their size: the cache answers them. Nothing outside the
     * list is visited. */
    AX_CHECK_EQ_U(c.layouts, 2);
    AX_CHECK_EQ_U(c.measures, 1);
    AX_CHECK(c.cached >= 59);
    AX_CHECK(pass_of(&f, a.window) != f.scene.layout_pass);
    AX_CHECK(pass_of(&f, a.main) != f.scene.layout_pass);
    AX_CHECK(pass_of(&f, a.sidebar) != f.scene.layout_pass);
    AX_CHECK(pass_of(&f, a.side_items[0]) != f.scene.layout_pass);
    /* 1 resized + 49 moved down. */
    AX_CHECK_EQ_U(c.frames_changed, 50);
    CHECK_FRAME(&f, a.list_items[10], 0, 100, 300, 30);
    CHECK_FRAME(&f, a.list_items[11], 1, 132, 299, 8);
    CHECK_FRAME(&f, a.list, 8, 8, 300, 580);
    ax_scene_clear_dirty(&f.scene);

    /* Without the boundary (the list's height is auto), the edit climbs to the window. */
    set_dim(&f, a.list, AX_PROP_HEIGHT, 0, AX_UNIT_AUTO);
    (void)ax_layout_run(&f.scene, &d);
    ax_scene_clear_dirty(&f.scene);
    ax_layout_get_counters(&f.scene, &c, true);
    pts(&f, a.list_items[20], AX_PROP_HEIGHT, 9);
    AX_CHECK(!ax_scene_is_relayout_boundary(&f.scene, a.list));
    (void)ax_layout_run(&f.scene, &d);
    ax_layout_get_counters(&f.scene, &c, true);
    AX_CHECK(pass_of(&f, a.window) == f.scene.layout_pass);
    AX_CHECK(pass_of(&f, a.side_items[0]) != f.scene.layout_pass); /* still not visited */
    ax_scene_clear_dirty(&f.scene);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* Two panels, each a relayout boundary with many items. */
typedef struct two_panels {
    ax_node window;
    ax_node panel[2];
    ax_node items[2][150];
} two_panels;

static void build_panels(fixture *f, two_panels *p)
{
    int k, i;
    p->window = node(f, (ax_node)AX_HANDLE_NULL_INIT);
    set_u32(f, p->window, AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
    pts(f, p->window, AX_PROP_WIDTH, 900);
    for (k = 0; k < 2; ++k) {
        p->panel[k] = node(f, p->window);
        pts(f, p->panel[k], AX_PROP_WIDTH, 400);
        pts(f, p->panel[k], AX_PROP_HEIGHT, 700);
        set_u32(f, p->panel[k], AX_PROP_FLEX_DIRECTION, AX_FLEX_DIRECTION_ROW);
        set_u32(f, p->panel[k], AX_PROP_FLEX_WRAP, AX_FLEX_WRAP_WRAP);
        for (i = 0; i < 150; ++i) {
            ax_node n = node(f, p->panel[k]);
            pts(f, n, AX_PROP_WIDTH, 30.0f + (float)(i % 7));
            pts(f, n, AX_PROP_HEIGHT, 10.0f + (float)(i % 3));
            set_f32(f, n, AX_PROP_FLEX_GROW, (float)(i % 2));
            p->items[k][i] = n;
        }
    }
}

static void edit_panels(fixture *f, two_panels *p, int round)
{
    int k;
    for (k = 0; k < 2; ++k) {
        pts(f, p->items[k][(round * 13 + k * 7) % 150], AX_PROP_WIDTH, 50.0f + (float)round);
        pts(f, p->items[k][(round * 29 + 3) % 150], AX_PROP_HEIGHT, 5.0f + (float)(round % 4));
    }
}

AX_TEST(independent_boundaries_run_in_parallel_with_the_same_results)
{
    fixture fs, fp;
    two_panels ps, pp;
    ax_job_system *js = NULL;
    ax_jobs_desc jd = ax_jobs_default_desc();
    ax_layout_pass_desc serial, parallel;
    ax_layout_counters cs, cp;
    int round;
    uint32_t i;
    jd.worker_count = 4;
    AX_REQUIRE(ax_jobs_create(&jd, &js) == AX_OK);
    fixture_init(&fs);
    fixture_init(&fp);
    build_panels(&fs, &ps);
    build_panels(&fp, &pp);
    memset(&serial, 0, sizeof serial);
    parallel = serial;
    parallel.jobs = js;
    parallel.parallel_min_nodes = 1;
    ax_layout_tree(&fs.scene, ps.window, NAN, NAN, 1.0f);
    ax_layout_tree(&fp.scene, pp.window, NAN, NAN, 1.0f);
    ax_scene_clear_dirty(&fs.scene);
    ax_scene_clear_dirty(&fp.scene);
    ax_layout_get_counters(&fs.scene, &cs, true);
    ax_layout_get_counters(&fp.scene, &cp, true);
    for (round = 0; round < 20; ++round) {
        edit_panels(&fs, &ps, round);
        edit_panels(&fp, &pp, round);
        AX_CHECK_EQ_U(fp.scene.dirty.layout_root_count, 2);
        (void)ax_layout_run(&fs.scene, &serial);
        (void)ax_layout_run(&fp.scene, &parallel);
        ax_scene_clear_dirty(&fs.scene);
        ax_scene_clear_dirty(&fp.scene);
    }
    ax_layout_get_counters(&fs.scene, &cs, true);
    ax_layout_get_counters(&fp.scene, &cp, true);
    AX_CHECK_EQ_U(cs.parallel_roots, 0);
    AX_CHECK_EQ_U(cp.parallel_roots, 40); /* both panels, every round */
    AX_CHECK_EQ_U(cp.roots, cs.roots);
    AX_CHECK_EQ_U(cp.layouts, cs.layouts);
    AX_CHECK_EQ_U(cp.frames_changed, cs.frames_changed);
    /* Same rows in both scenes (built the same way): the same bits. */
    AX_REQUIRE(fs.scene.store.row_count == fp.scene.store.row_count);
    for (i = 0; i < fs.scene.store.row_count; ++i) {
        AX_CHECK(memcmp(&fs.scene.store.layout[i].frame, &fp.scene.store.layout[i].frame,
                        sizeof(ax_rect)) == 0);
    }
    AX_CHECK_EQ_U(fixture_deinit(&fs), 0);
    AX_CHECK_EQ_U(fixture_deinit(&fp), 0);
    ax_jobs_destroy(js);
}

/* ---- The measure functions' contract (decision 90) --------------------------------------------
 */

static void measure_reading_the_store(void *user, ax_node n, float width, ax_layout_mode wm,
                                      float height, ax_layout_mode hm, float *out_w, float *out_h)
{
    ax_scene *scene = (ax_scene *)user;
    (void)width;
    (void)wm;
    (void)height;
    (void)hm;
    *out_w = ax_node_store_is_live(&scene->store, n) ? 10.0f : 0.0f; /* breaks the contract */
    *out_h = 10.0f;
}

/* Even on the scene thread, which owns the store, a measure function may not touch it. */
AX_DEATH_TEST(a_measure_function_that_touches_the_store_aborts)
{
    fixture f;
    ax_node root, leaf;
    fixture_init(&f);
    root = node(&f, (ax_node)AX_HANDLE_NULL_INIT);
    pts(&f, root, AX_PROP_WIDTH, 100);
    leaf = node(&f, root);
    set_u32(&f, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    (void)ax_layout_set_measure(&f.scene, leaf, measure_reading_the_store, &f.scene);
    ax_layout_tree(&f.scene, root, NAN, NAN, 1.0f);
}

AX_TEST(the_measure_guard_is_counted_and_per_thread)
{
    AX_CHECK(!ax_node_store_in_measure());
    ax_node_store_enter_measure();
    ax_node_store_enter_measure();
    AX_CHECK(ax_node_store_in_measure());
    ax_node_store_leave_measure();
    AX_CHECK(ax_node_store_in_measure());
    ax_node_store_leave_measure();
    AX_CHECK(!ax_node_store_in_measure());
}
