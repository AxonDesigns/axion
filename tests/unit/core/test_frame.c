/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The frame stages after applying transactions: minimal positioning, the layer rule and the
 * incremental recording of the Render IR from the node store (decision 76).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_allocator.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/render/ax_render_ir.h"

/* ---- Helpers --------------------------------------------------------------------------------- */

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static void box(ax_txn *t, ax_node n, float x, float y, float w, float h)
{
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
}

static ax_rgba rgba(float r, float g, float b, float a)
{
    ax_rgba c;
    c.r = r * a; /* premultiplied; values chosen exact in linear light */
    c.g = g * a;
    c.b = b * a;
    c.a = a;
    return c;
}

static ax_transform2d identity(void)
{
    ax_transform2d m;
    memset(&m, 0, sizeof m);
    m.a = 1.0f;
    m.d = 1.0f;
    return m;
}

static ax_transform2d rotate90(void)
{
    ax_transform2d m;
    m.a = 0.0f;
    m.b = 1.0f;
    m.c = -1.0f;
    m.d = 0.0f;
    m.tx = 0.0f;
    m.ty = 0.0f;
    return m;
}

typedef struct scene_nodes {
    ax_node root, card, inner, spinner, dot;
} scene_nodes;

/* root 400x300 (viewport): card (10, 20, 100x50, rounded 8) with inner (5, 5, 10x10); spinner
 * (200, 100, 40x20) turned 90 degrees, with dot (0, 0, 4x4). */
static bool build(ax_world w, scene_nodes *n)
{
    ax_txn *t = ax_txn_begin(w);
    n->root = ax_txn_create(t, AX_NODE_VIEW);
    n->card = ax_txn_create(t, AX_NODE_VIEW);
    n->inner = ax_txn_create(t, AX_NODE_VIEW);
    n->spinner = ax_txn_create(t, AX_NODE_VIEW);
    n->dot = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_color(t, n->root, AX_PROP_BACKGROUND_COLOR, rgba(0, 0, 0.25f, 1));
    box(t, n->card, 10, 20, 100, 50);
    ax_txn_set_color(t, n->card, AX_PROP_BACKGROUND_COLOR, rgba(1, 1, 1, 1));
    ax_txn_set_f32(t, n->card, AX_PROP_CORNER_RADIUS, 8.0f);
    box(t, n->inner, 5, 5, 10, 10);
    ax_txn_set_color(t, n->inner, AX_PROP_BACKGROUND_COLOR, rgba(1, 0, 0, 1));
    box(t, n->spinner, 200, 100, 40, 20);
    ax_txn_set_color(t, n->spinner, AX_PROP_BACKGROUND_COLOR, rgba(0, 0, 1, 1));
    ax_txn_set_transform(t, n->spinner, AX_PROP_TRANSFORM, rotate90());
    box(t, n->dot, 0, 0, 4, 4);
    ax_txn_set_color(t, n->dot, AX_PROP_BACKGROUND_COLOR, rgba(0, 1, 0, 1));
    ax_txn_append_child(t, n->root, n->card);
    ax_txn_append_child(t, n->card, n->inner);
    ax_txn_append_child(t, n->root, n->spinner);
    ax_txn_append_child(t, n->spinner, n->dot);
    return ax_txn_commit(t, NULL) == AX_OK;
}

typedef struct text {
    char buf[8192];
    size_t len;
} text;

static void write_text(void *ctx, const char *s, size_t n)
{
    text *t = (text *)ctx;
    if (t->len + n < sizeof t->buf) {
        memcpy(t->buf + t->len, s, n);
        t->len += n;
        t->buf[t->len] = '\0';
    }
}

/* One frame, in the pipeline's order, recording `root` into `b`; returns the snapshot. */
static const ax_render_ir *frame(ax_world w, ax_node root, ax_ir_builder *b, float vw, float vh)
{
    const ax_render_ir *ir = NULL;
    (void)ax_world_apply_pending(w, UINT64_MAX);
    if (ax_world_set_root_viewport(w, root, vw, vh) != AX_OK) {
        return NULL;
    }
    (void)ax_world_position(w);
    ax_world_record_collect(w);
    ax_ir_builder_begin(b, vw, vh, 0, ax_color_make(0, 0, 0, 1));
    if (ax_world_record_root(w, root, b) != AX_OK || ax_ir_builder_end(b, &ir) != AX_OK) {
        return NULL;
    }
    (void)ax_world_publish_mirror(w);
    ax_scene_clear_dirty(ax_world_scene(w));
    return ir;
}

static ax_frame_counters counters(ax_world w)
{
    ax_frame_counters c;
    ax_world_get_frame_counters(w, &c, true);
    return c;
}

/* ---- Positioning ----------------------------------------------------------------------------- */

static bool frame_is(ax_world w, ax_node n, float x, float y, float fw, float fh)
{
    ax_rectf r;
    return ax_mirror_get_frame(w, n, &r) == AX_OK && r.x == x && r.y == y && r.w == fw && r.h == fh;
}

AX_TEST(positions_come_from_left_top_width_height_and_the_viewport)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    ax_txn *t;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    AX_CHECK_EQ_U(counters(w).frames_changed, 5);
    ax_world_mirror_sync(w);
    AX_CHECK(frame_is(w, n.root, 0, 0, 400, 300));
    AX_CHECK(frame_is(w, n.card, 10, 20, 100, 50));
    AX_CHECK(frame_is(w, n.inner, 5, 5, 10, 10));
    AX_CHECK(frame_is(w, n.spinner, 200, 100, 40, 20));

    /* Nothing changed: nothing is positioned again. */
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    AX_CHECK_EQ_U(counters(w).frames_changed, 0);

    /* One node moves: one frame changes. A new viewport changes only the root's. */
    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, n.inner, AX_PROP_LEFT, pt(7));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    AX_CHECK_EQ_U(counters(w).frames_changed, 1);
    AX_REQUIRE(frame(w, n.root, &b, 640, 480) != NULL);
    AX_CHECK_EQ_U(counters(w).frames_changed, 1);
    ax_world_mirror_sync(w);
    AX_CHECK(frame_is(w, n.inner, 7, 5, 10, 10));
    AX_CHECK(frame_is(w, n.root, 0, 0, 640, 480)); /* its own WIDTH/HEIGHT do not apply */

    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(stale_layout_roots_are_skipped_and_an_overflow_walks_everything)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    ax_txn *t;
    ax_node gone;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    (void)counters(w);

    /* A node created (a layout root) and destroyed before the frame: listed, then skipped. */
    t = ax_txn_begin(w);
    gone = ax_txn_create(t, AX_NODE_VIEW);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    t = ax_txn_begin(w);
    ax_txn_remove(t, gone);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(ax_world_apply_pending(w, UINT64_MAX) == 2);
    AX_CHECK(ax_world_scene(w)->dirty.layout_root_count >= 1);
    AX_CHECK_EQ_U(ax_world_position(w), 0);
    ax_scene_clear_dirty(ax_world_scene(w));

    /* The root list overflowed: every tree is walked, nothing marked is missed. */
    ax_world_scene(w)->store.layout[ax_node_store_row(&ax_world_scene(w)->store, n.dot)].frame.x =
        99.0f; /* as if a change had been lost */
    ax_world_scene(w)->dirty.roots_overflowed = true;
    AX_CHECK_EQ_U(ax_world_position(w), 1);
    ax_scene_clear_dirty(ax_world_scene(w));
    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

/* ---- Recording ------------------------------------------------------------------------------- */

static const char *const k_golden =
    "render_ir 1.0 size 400x300 time_ns 0 seq 0 background (0 0 0 1)\n"
    "damage full\n"
    "layer 0 root items 3\n"
    "  rect (0 0 400 300) color (0 0 0.25 1)\n"
    "  rounded_rect (10 20 100 50) radii (8 8 8 8) color (1 1 1 1)\n"
    "  rect (15 25 10 10) color (1 0 0 1)\n"
    "  layer 1 transform (0 1 -1 0 230 90) items 2\n"
    "    rect (0 0 40 20) color (0 0 1 1)\n"
    "    rect (0 0 4 4) color (0 1 0 1)\n";

AX_TEST(the_tree_is_recorded_as_expected)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    const ax_render_ir *ir;
    text out;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    ir = frame(w, n.root, &b, 400, 300);
    AX_REQUIRE(ir != NULL);
    AX_CHECK_RESULT(ax_ir_validate(ir), AX_OK);
    memset(&out, 0, sizeof out);
    ax_ir_dump(ir, write_text, &out);
    if (strcmp(out.buf, k_golden) != 0) {
        printf("got:\n%s\nexpected:\n%s\n", out.buf, k_golden);
    }
    AX_CHECK(strcmp(out.buf, k_golden) == 0);
    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(only_paint_and_layout_changes_record_items_again)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    ax_frame_counters c;
    ax_txn *t;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 5);
    AX_CHECK_EQ_U(c.layers_promoted, 1); /* the spinner */
    AX_CHECK(!ax_world_root_changed(w, n.root, NULL));

    /* No change: every item copied. */
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 0);
    AX_CHECK_EQ_U(c.items_reused, 5);

    /* Composite only (a layer's transform): no item is recorded, the root has new content. */
    t = ax_txn_begin(w);
    ax_txn_set_transform(t, n.spinner, AX_PROP_TRANSFORM, identity());
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    (void)ax_world_apply_pending(w, UINT64_MAX);
    ax_world_record_collect(w);
    AX_CHECK(ax_world_root_changed(w, n.root, NULL));
    ax_scene_clear_dirty(ax_world_scene(w));
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 0);
    AX_CHECK_EQ_U(c.items_reused, 5);
    AX_CHECK_EQ_U(c.layers, 1); /* still a layer: promotion is sticky */

    /* Paint: that node only. */
    t = ax_txn_begin(w);
    ax_txn_set_color(t, n.card, AX_PROP_BACKGROUND_COLOR, rgba(0.5f, 0.5f, 0.5f, 1));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 1);
    AX_CHECK_EQ_U(c.items_reused, 4);

    /* Layout: the card moves, and its child in the same layer moves with it. The spinner's
     * subtree is in its own layer: untouched. */
    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, n.card, AX_PROP_LEFT, pt(30));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 2);
    AX_CHECK_EQ_U(c.items_reused, 3);

    /* A layer moving is a layer record change only: its items and its child's are in its own
     * space. */
    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, n.spinner, AX_PROP_TOP, pt(150));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 0);
    AX_CHECK_EQ_U(c.items_reused, 5);

    /* A layout change that resizes: that node only. */
    t = ax_txn_begin(w);
    ax_txn_set_dimension(t, n.spinner, AX_PROP_WIDTH, pt(50));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.items_recorded, 1);
    AX_CHECK_EQ_U(c.items_reused, 4);

    /* Promotion: the card's first opacity makes it a layer once; its subtree is recorded again
     * in the new space. Later opacity changes record nothing. */
    t = ax_txn_begin(w);
    ax_txn_set_f32(t, n.card, AX_PROP_OPACITY, 0.5f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.layers_promoted, 1);
    AX_CHECK_EQ_U(c.items_recorded, 2);
    t = ax_txn_begin(w);
    ax_txn_set_f32(t, n.card, AX_PROP_OPACITY, 1.0f);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    c = counters(w);
    AX_CHECK_EQ_U(c.layers_promoted, 0);
    AX_CHECK_EQ_U(c.items_recorded, 0);
    AX_CHECK_EQ_U(c.layers, 2);

    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(a_reused_slot_is_recorded_fresh)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    ax_txn *t;
    ax_node fresh;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    /* Remove the spinner's dot; its slot comes back to the app thread for the next create. */
    t = ax_txn_begin(w);
    ax_txn_remove(t, n.dot);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    t = ax_txn_begin(w);
    (void)ax_world_poll_txn_error(w, &(ax_txn_error){0}, NULL); /* takes recycled handles back */
    fresh = ax_txn_create(t, AX_NODE_VIEW);
    box(t, fresh, 1, 2, 3, 4);
    ax_txn_set_color(t, fresh, AX_PROP_BACKGROUND_COLOR, rgba(1, 1, 0, 1));
    ax_txn_append_child(t, n.root, fresh);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    (void)counters(w);
    AX_REQUIRE(frame(w, n.root, &b, 400, 300) != NULL);
    AX_CHECK_EQ_U(ax_handle_slot(fresh.h), ax_handle_slot(n.dot.h));
    {
        ax_frame_counters c = counters(w);
        /* Only the new node: the root, marked LAYOUT for its new child, keeps its frame. */
        AX_CHECK_EQ_U(c.items_recorded, 1);
        AX_CHECK_EQ_U(c.layers_promoted, 0); /* the old node's promotion is not inherited */
    }
    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}

AX_TEST(window_roots_are_checked)
{
    ax_world w;
    scene_nodes n;
    ax_ir_builder b;
    AX_REQUIRE(ax_world_create(NULL, &w) == AX_OK);
    ax_ir_builder_init(&b, ax_default_allocator());
    AX_REQUIRE(build(w, &n));
    (void)ax_world_apply_pending(w, UINT64_MAX);
    AX_CHECK_RESULT(ax_world_set_root_viewport(w, n.card, 10, 10), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_world_set_root_viewport(w, n.root, -1, 10), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_world_set_root_viewport(w, n.root, NAN, 10), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_world_set_root_viewport(w, (ax_node){0}, 10, 10), AX_ERR_INVALID_HANDLE);
    ax_ir_builder_begin(&b, 10, 10, 0, ax_color_make(0, 0, 0, 1));
    AX_CHECK_RESULT(ax_world_record_root(w, n.root, &b), AX_ERR_INVALID_HANDLE); /* not a root */
    AX_REQUIRE(ax_world_set_root_viewport(w, n.root, 10, 10) == AX_OK);
    AX_CHECK(ax_world_root_changed(w, n.root, NULL)); /* true before its first record */
    ax_world_remove_root(w, n.root);
    AX_CHECK(!ax_world_root_changed(w, n.root, NULL));
    ax_ir_builder_deinit(&b);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}
