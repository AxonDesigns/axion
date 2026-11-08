/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dirty propagation by effect class, with relayout boundaries.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/core/ax_scene.h"

static ax_node make(ax_scene *s)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (ax_scene_create(s, AX_NODE_VIEW, &n) != AX_OK) {
        n.h = 0;
    }
    return n;
}

static ax_result set_f32(ax_scene *s, ax_node n, ax_prop p, float f)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.f32 = f;
    return ax_scene_set(s, n, p, AX_VALUE_F32, &v);
}

static ax_result set_dim(ax_scene *s, ax_node n, ax_prop p, float value, ax_unit unit)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.dimension.value = value;
    v.dimension.unit = (uint32_t)unit;
    return ax_scene_set(s, n, p, AX_VALUE_DIMENSION, &v);
}

static void fix_size(ax_scene *s, ax_node n, float w, float h)
{
    AX_CHECK_RESULT(set_dim(s, n, AX_PROP_WIDTH, w, AX_UNIT_POINTS), AX_OK);
    AX_CHECK_RESULT(set_dim(s, n, AX_PROP_HEIGHT, h, AX_UNIT_POINTS), AX_OK);
}

static bool roots_are(const ax_scene *s, const ax_node *expected, uint32_t count)
{
    uint32_t i;
    if (s->dirty.layout_root_count != count) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!AX_HANDLE_EQ(s->dirty.layout_roots[i], expected[i])) {
            return false;
        }
    }
    return true;
}

#define FLAGS(n) ax_scene_dirty_flags(&scene, (n))

/*
 * window (root)
 *   panel (fixed 300x200: a boundary)
 *     list (auto)
 *       item (auto)
 *         label (auto)
 *   sidebar (auto)
 */
typedef struct tree {
    ax_node window, panel, list, item, label, sidebar;
} tree;

static void build(ax_scene *s, tree *t)
{
    t->window = make(s);
    t->panel = make(s);
    t->list = make(s);
    t->item = make(s);
    t->label = make(s);
    t->sidebar = make(s);
    AX_CHECK_RESULT(ax_scene_insert_before(s, t->window, t->panel, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_insert_before(s, t->window, t->sidebar, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_insert_before(s, t->panel, t->list, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_insert_before(s, t->list, t->item, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_insert_before(s, t->item, t->label, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    fix_size(s, t->panel, 300.0f, 200.0f);
    ax_scene_clear_dirty(s);
}

AX_TEST(new_nodes_are_fully_dirty_roots)
{
    ax_scene scene;
    ax_node a;
    ax_scene_init(&scene, NULL);
    a = make(&scene);
    AX_CHECK_EQ_U(FLAGS(a), AX_DIRTY_NEW_NODE | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK(roots_are(&scene, &a, 1));
    ax_scene_clear_dirty(&scene);
    AX_CHECK_EQ_U(FLAGS(a), 0);
    AX_CHECK_EQ_U(scene.dirty.layout_root_count, 0);
    ax_scene_deinit(&scene);
}

AX_TEST(boundaries_are_roots_and_fixed_size_nodes)
{
    ax_scene scene;
    tree t;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);
    AX_CHECK(ax_scene_is_relayout_boundary(&scene, t.window));
    AX_CHECK(ax_scene_is_relayout_boundary(&scene, t.panel));
    AX_CHECK(!ax_scene_is_relayout_boundary(&scene, t.list));
    AX_CHECK(!ax_scene_is_relayout_boundary(&scene, t.sidebar));
    /* Width alone is not enough. */
    AX_CHECK_RESULT(set_dim(&scene, t.list, AX_PROP_WIDTH, 10.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK(!ax_scene_is_relayout_boundary(&scene, t.list));
    AX_CHECK_RESULT(set_dim(&scene, t.panel, AX_PROP_HEIGHT, 0.0f, AX_UNIT_AUTO), AX_OK);
    AX_CHECK(!ax_scene_is_relayout_boundary(&scene, t.panel));
    ax_scene_deinit(&scene);
}

AX_TEST(paint_and_composite_changes_mark_only_the_node)
{
    ax_scene scene;
    tree t;
    ax_value v;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);
    AX_CHECK_RESULT(set_f32(&scene, t.item, AX_PROP_CORNER_RADIUS, 4.0f), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.item), AX_DIRTY_PAINT);
    AX_CHECK_RESULT(set_f32(&scene, t.label, AX_PROP_OPACITY, 0.5f), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.label), AX_DIRTY_COMPOSITE);
    /* A transform is local: the subtree is not touched. */
    memset(&v, 0, sizeof v);
    v.transform2d = ax_affine2_translate(3.0f, 0.0f);
    AX_CHECK_RESULT(ax_scene_set(&scene, t.list, AX_PROP_TRANSFORM, AX_VALUE_TRANSFORM2D, &v),
                    AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.list), AX_DIRTY_COMPOSITE);
    AX_CHECK_EQ_U(FLAGS(t.item), AX_DIRTY_PAINT);
    AX_CHECK_EQ_U(FLAGS(t.window), 0);
    AX_CHECK_EQ_U(FLAGS(t.panel), 0);
    AX_CHECK_EQ_U(scene.dirty.layout_root_count, 0);
    ax_scene_deinit(&scene);
}

AX_TEST(layout_changes_climb_to_the_nearest_boundary)
{
    ax_scene scene;
    tree t;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);

    /* Inside the fixed panel: stops there, the window is untouched. */
    AX_CHECK_RESULT(set_dim(&scene, t.label, AX_PROP_WIDTH, 80.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.label), AX_DIRTY_LAYOUT);
    AX_CHECK_EQ_U(FLAGS(t.item), AX_DIRTY_LAYOUT_CHILD);
    AX_CHECK_EQ_U(FLAGS(t.list), AX_DIRTY_LAYOUT_CHILD);
    AX_CHECK_EQ_U(FLAGS(t.panel), AX_DIRTY_LAYOUT_CHILD | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK_EQ_U(FLAGS(t.window), 0);
    AX_CHECK(roots_are(&scene, &t.panel, 1));

    /* A second change in the same subtree adds no root. */
    AX_CHECK_RESULT(set_dim(&scene, t.item, AX_PROP_TOP, 4.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.item), AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_CHILD);
    AX_CHECK(roots_are(&scene, &t.panel, 1));

    /* The panel's own size: its parent lays it out, so the window becomes a root. */
    AX_CHECK_RESULT(set_dim(&scene, t.panel, AX_PROP_WIDTH, 320.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK(FLAGS(t.panel) & AX_DIRTY_LAYOUT);
    AX_CHECK_EQ_U(FLAGS(t.window), AX_DIRTY_LAYOUT_CHILD | AX_DIRTY_LAYOUT_ROOT);
    {
        ax_node expected[2];
        expected[0] = t.panel;
        expected[1] = t.window;
        AX_CHECK(roots_are(&scene, expected, 2));
    }

    /* Outside any fixed node: up to the window. */
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(set_dim(&scene, t.sidebar, AX_PROP_HEIGHT, 40.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.sidebar), AX_DIRTY_LAYOUT);
    AX_CHECK_EQ_U(FLAGS(t.window), AX_DIRTY_LAYOUT_CHILD | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK_EQ_U(FLAGS(t.panel), 0);
    AX_CHECK(roots_are(&scene, &t.window, 1));

    /* A root's own layout change makes it its own root. */
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(set_dim(&scene, t.window, AX_PROP_WIDTH, 800.0f, AX_UNIT_POINTS), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.window), AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK(roots_are(&scene, &t.window, 1));
    ax_scene_deinit(&scene);
}

AX_TEST(setting_the_same_value_marks_nothing)
{
    ax_scene scene;
    tree t;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);
    /* build() fixed the panel at 300x200, then cleared. */
    fix_size(&scene, t.panel, 300.0f, 200.0f);
    AX_CHECK_RESULT(set_f32(&scene, t.item, AX_PROP_OPACITY, 1.0f), AX_OK);
    AX_CHECK_RESULT(set_f32(&scene, t.item, AX_PROP_CORNER_RADIUS, 0.0f), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.panel), 0);
    AX_CHECK_EQ_U(FLAGS(t.item), 0);
    AX_CHECK_EQ_U(scene.dirty.layout_root_count, 0);
    ax_scene_deinit(&scene);
}

AX_TEST(structural_changes_mark_both_parents)
{
    ax_scene scene;
    tree t;
    ax_node fresh;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);

    /* Move the item from the list (inside the fixed panel) to the sidebar. */
    AX_CHECK_RESULT(ax_scene_insert_before(&scene, t.sidebar, t.item, (ax_node)AX_HANDLE_NULL_INIT),
                    AX_OK);
    AX_CHECK(FLAGS(t.list) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.panel) & AX_DIRTY_LAYOUT_CHILD); /* old side stops at the panel */
    AX_CHECK(FLAGS(t.panel) & AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK(FLAGS(t.sidebar) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.window) & AX_DIRTY_LAYOUT_ROOT); /* new side climbs to the window */
    AX_CHECK(FLAGS(t.item) & AX_DIRTY_LAYOUT);
    /* The moved subtree's insides keep their layout; only STYLE (what they inherit may change,
     * decision 99). */
    AX_CHECK_EQ_U(FLAGS(t.label), AX_DIRTY_STYLE);

    /* Children of a fixed node: only that node. */
    ax_scene_clear_dirty(&scene);
    fresh = make(&scene);
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(ax_scene_insert_before(&scene, t.panel, fresh, t.list), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.panel), AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK_EQ_U(FLAGS(t.window), 0);
    AX_CHECK(roots_are(&scene, &t.panel, 1));

    /* Reinserting a node where it already is marks nothing. */
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(ax_scene_insert_before(&scene, t.panel, fresh, t.list), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.panel), 0);

    /* Destroying a node marks its parent. */
    AX_CHECK_RESULT(ax_scene_destroy(&scene, t.item), AX_OK);
    AX_CHECK(FLAGS(t.sidebar) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.window) & AX_DIRTY_LAYOUT_ROOT);

    /* Detaching makes the node a root of its own. */
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(ax_scene_detach(&scene, t.list), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.panel), AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK_EQ_U(FLAGS(t.list), AX_DIRTY_LAYOUT | AX_DIRTY_LAYOUT_ROOT);
    ax_scene_deinit(&scene);
}

AX_TEST(inherited_properties_mark_the_subtree)
{
    ax_scene scene;
    tree t;
    ax_prop_desc d;
    ax_prop font_size;
    ax_value v;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);
    /* A stand-in for text's inherited font size, registered for the test. */
    memset(&d, 0, sizeof d);
    d.name = "test-font-size";
    d.type = AX_VALUE_F32;
    d.flags = AX_PROP_INHERITED | AX_PROP_ANIMATABLE;
    d.effect = AX_EFFECT_LAYOUT;
    d.default_value.f32 = 16.0f;
    AX_REQUIRE(ax_prop_registry_register(&scene.props, &scene.store, &d, &font_size) == AX_OK);

    memset(&v, 0, sizeof v);
    v.f32 = 20.0f;
    AX_CHECK_RESULT(ax_scene_set(&scene, t.list, font_size, AX_VALUE_F32, &v), AX_OK);
    AX_CHECK(FLAGS(t.list) & AX_DIRTY_STYLE);
    AX_CHECK(FLAGS(t.list) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.item) & AX_DIRTY_STYLE);
    AX_CHECK(FLAGS(t.item) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.label) & AX_DIRTY_STYLE);
    AX_CHECK(FLAGS(t.label) & AX_DIRTY_LAYOUT);
    AX_CHECK(FLAGS(t.panel) & AX_DIRTY_LAYOUT_ROOT);
    AX_CHECK_EQ_U(FLAGS(t.sidebar), 0);
    AX_CHECK_EQ_U(FLAGS(t.window), 0);
    AX_CHECK(roots_are(&scene, &t.panel, 1));
    ax_scene_deinit(&scene);
}

AX_TEST(a_shorthand_marks_like_its_longhands)
{
    ax_scene scene;
    tree t;
    ax_scene_init(&scene, NULL);
    build(&scene, &t);
    AX_CHECK_RESULT(set_f32(&scene, t.label, AX_PROP_CORNER_RADIUS, 6.0f), AX_OK);
    AX_CHECK_EQ_U(FLAGS(t.label), AX_DIRTY_PAINT);
    AX_CHECK_EQ_U(scene.dirty.layout_root_count, 0);
    ax_scene_deinit(&scene);
}
