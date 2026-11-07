/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <float.h>
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_tracking_allocator.h"
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

static ax_node make(ax_scene *s)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (ax_scene_create(s, AX_NODE_VIEW, &n) != AX_OK) {
        n.h = 0;
    }
    return n;
}

static ax_value f32v(float f)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.f32 = f;
    return v;
}

static ax_value dimv(float value, ax_unit unit)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.dimension.value = value;
    v.dimension.unit = (uint32_t)unit;
    return v;
}

/* ---- The table ------------------------------------------------------------------------------- */

typedef struct expected_prop {
    ax_prop id;
    const char *name;
    ax_value_type type;
    ax_effect_class effect;
    uint32_t flags;
} expected_prop;

AX_TEST(builtin_properties_match_the_documented_table)
{
    static const expected_prop expected[] = {
        {AX_PROP_WIDTH, "width", AX_VALUE_DIMENSION, AX_EFFECT_LAYOUT, AX_PROP_ANIMATABLE},
        {AX_PROP_HEIGHT, "height", AX_VALUE_DIMENSION, AX_EFFECT_LAYOUT, AX_PROP_ANIMATABLE},
        {AX_PROP_LEFT, "left", AX_VALUE_DIMENSION, AX_EFFECT_LAYOUT, AX_PROP_ANIMATABLE},
        {AX_PROP_TOP, "top", AX_VALUE_DIMENSION, AX_EFFECT_LAYOUT, AX_PROP_ANIMATABLE},
        {AX_PROP_BACKGROUND_COLOR, "background-color", AX_VALUE_COLOR, AX_EFFECT_PAINT,
         AX_PROP_ANIMATABLE},
        {AX_PROP_CORNER_RADIUS_TOP_LEFT, "corner-radius-top-left", AX_VALUE_F32, AX_EFFECT_PAINT,
         AX_PROP_ANIMATABLE},
        {AX_PROP_CORNER_RADIUS_TOP_RIGHT, "corner-radius-top-right", AX_VALUE_F32, AX_EFFECT_PAINT,
         AX_PROP_ANIMATABLE},
        {AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, "corner-radius-bottom-right", AX_VALUE_F32,
         AX_EFFECT_PAINT, AX_PROP_ANIMATABLE},
        {AX_PROP_CORNER_RADIUS_BOTTOM_LEFT, "corner-radius-bottom-left", AX_VALUE_F32,
         AX_EFFECT_PAINT, AX_PROP_ANIMATABLE},
        {AX_PROP_CORNER_RADIUS, "corner-radius", AX_VALUE_F32, AX_EFFECT_PAINT,
         AX_PROP_ANIMATABLE | AX_PROP_SHORTHAND},
        {AX_PROP_OPACITY, "opacity", AX_VALUE_F32, AX_EFFECT_COMPOSITE, AX_PROP_ANIMATABLE},
        {AX_PROP_TRANSFORM, "transform", AX_VALUE_TRANSFORM2D, AX_EFFECT_COMPOSITE,
         AX_PROP_ANIMATABLE},
    };
    fixture f;
    uint32_t count = 0;
    uint32_t i;
    const ax_prop_desc *all = ax_prop_registry_builtins(&count);
    fixture_init(&f);
    AX_CHECK_EQ_U(count, AX_COUNTOF(expected));
    for (i = 0; i < AX_COUNTOF(expected); ++i) {
        const ax_prop_desc *d = ax_prop_registry_find(&f.scene.props, expected[i].id);
        ax_str name;
        AX_REQUIRE(d != NULL);
        AX_CHECK(d == &all[i]);
        AX_CHECK(strcmp(d->name, expected[i].name) == 0);
        AX_CHECK_EQ_U(d->type, expected[i].type);
        AX_CHECK_EQ_U(d->effect, expected[i].effect);
        AX_CHECK_EQ_U(d->flags, expected[i].flags);
        name.ptr = expected[i].name;
        name.len = strlen(expected[i].name);
        AX_CHECK(ax_prop_registry_find_by_name(&f.scene.props, name) == d);
    }
    AX_CHECK(ax_prop_registry_find(&f.scene.props, AX_PROP_INVALID) == NULL);
    AX_CHECK(ax_prop_registry_find(&f.scene.props, 0x0105) == NULL);
    AX_CHECK(ax_prop_registry_find(&f.scene.props, 0x12345) == NULL);
    AX_CHECK(ax_prop_registry_find_by_name(&f.scene.props, AX_STR("color")) == NULL);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- Setting and reading --------------------------------------------------------------------- */

AX_TEST(set_and_get_every_builtin)
{
    fixture f;
    ax_node n;
    ax_value v;
    ax_value out;
    uint32_t row;
    fixture_init(&f);
    n = make(&f.scene);

    v = dimv(120.0f, AX_UNIT_POINTS);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_WIDTH, AX_VALUE_DIMENSION, &v), AX_OK);
    v = dimv(48.0f, AX_UNIT_POINTS);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_HEIGHT, AX_VALUE_DIMENSION, &v), AX_OK);
    v = dimv(-10.0f, AX_UNIT_POINTS); /* insets may be negative */
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v), AX_OK);
    v = dimv(0.0f, AX_UNIT_AUTO);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_TOP, AX_VALUE_DIMENSION, &v), AX_OK);
    memset(&v, 0, sizeof v);
    v.color = ax_color_make(0.5f, 0.25f, 0.0f, 1.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_BACKGROUND_COLOR, AX_VALUE_COLOR, &v), AX_OK);
    v = f32v(3.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, AX_VALUE_F32, &v),
                    AX_OK);
    v = f32v(0.25f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_OPACITY, AX_VALUE_F32, &v), AX_OK);
    memset(&v, 0, sizeof v);
    v.transform2d = ax_affine2_translate(5.0f, 6.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_TRANSFORM, AX_VALUE_TRANSFORM2D, &v), AX_OK);

    /* Stored in the hot columns. */
    row = ax_node_store_row(&f.scene.store, n);
    AX_CHECK(f.scene.store.style[row].width.value == 120.0f);
    AX_CHECK_EQ_U(f.scene.store.style[row].width.unit, AX_UNIT_POINTS);
    AX_CHECK(f.scene.store.style[row].left.value == -10.0f);
    AX_CHECK(f.scene.store.paint[row].background.g == 0.25f);
    AX_CHECK(f.scene.store.paint[row].corner_radius[2] == 3.0f);
    AX_CHECK(f.scene.store.composite[row].opacity == 0.25f);
    AX_CHECK(f.scene.store.composite[row].transform.ty == 6.0f);

    /* And read back by id. */
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_HEIGHT, &out), AX_OK);
    AX_CHECK(out.dimension.value == 48.0f);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_TOP, &out), AX_OK);
    AX_CHECK_EQ_U(out.dimension.unit, AX_UNIT_AUTO);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_BACKGROUND_COLOR, &out), AX_OK);
    AX_CHECK(out.color.r == 0.5f && out.color.a == 1.0f);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_CORNER_RADIUS_TOP_LEFT, &out), AX_OK);
    AX_CHECK(out.f32 == 0.0f);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_TRANSFORM, &out), AX_OK);
    AX_CHECK(out.transform2d.tx == 5.0f && out.transform2d.a == 1.0f);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(invalid_values_are_rejected_and_change_nothing)
{
    fixture f;
    ax_node n;
    ax_node stale;
    ax_value v;
    ax_value out;
    fixture_init(&f);
    n = make(&f.scene);
    stale = make(&f.scene);
    AX_REQUIRE(ax_scene_destroy(&f.scene, stale) == AX_OK);
    ax_scene_clear_dirty(&f.scene);

    v = f32v(1.0f);
    /* Wrong type, unknown property, stale node. */
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_WIDTH, AX_VALUE_F32, &v),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(ax_last_error()->code == AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(strstr(ax_last_error()->message.ptr, "width") != NULL);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, 0x0999, AX_VALUE_F32, &v), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, stale, AX_PROP_OPACITY, AX_VALUE_F32, &v),
                    AX_ERR_INVALID_HANDLE);
    /* Out of range, not finite. */
    v = f32v(1.5f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_OPACITY, AX_VALUE_F32, &v),
                    AX_ERR_INVALID_ARGUMENT);
    v = f32v(-1.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_CORNER_RADIUS, AX_VALUE_F32, &v),
                    AX_ERR_INVALID_ARGUMENT);
    v = f32v(NAN);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_OPACITY, AX_VALUE_F32, &v),
                    AX_ERR_INVALID_ARGUMENT);
    v = dimv(-1.0f, AX_UNIT_POINTS);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_WIDTH, AX_VALUE_DIMENSION, &v),
                    AX_ERR_INVALID_ARGUMENT);
    v = dimv(INFINITY, AX_UNIT_POINTS);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v),
                    AX_ERR_INVALID_ARGUMENT);
    v = dimv(1.0f, (ax_unit)7);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_LEFT, AX_VALUE_DIMENSION, &v),
                    AX_ERR_INVALID_ARGUMENT);
    /* Percentages wait for layout. */
    v = dimv(50.0f, AX_UNIT_PERCENT);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_WIDTH, AX_VALUE_DIMENSION, &v),
                    AX_ERR_UNSUPPORTED);
    memset(&v, 0, sizeof v);
    v.color = ax_color_make(0.0f, NAN, 0.0f, 1.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_BACKGROUND_COLOR, AX_VALUE_COLOR, &v),
                    AX_ERR_INVALID_ARGUMENT);
    memset(&v, 0, sizeof v);
    v.transform2d = ax_affine2_identity();
    v.transform2d.c = INFINITY;
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_TRANSFORM, AX_VALUE_TRANSFORM2D, &v),
                    AX_ERR_INVALID_ARGUMENT);
    /* An extended-range color is valid (decision 50). */
    v.color = ax_color_make(-0.1f, 1.4f, 0.0f, 1.0f);
    AX_CHECK_RESULT(ax_scene_check_value(&f.scene, AX_PROP_BACKGROUND_COLOR, AX_VALUE_COLOR, &v),
                    AX_OK);

    /* Nothing changed and nothing was marked. */
    AX_CHECK_EQ_U(ax_scene_dirty_flags(&f.scene, n), 0);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_OPACITY, &out), AX_OK);
    AX_CHECK(out.f32 == 1.0f);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_WIDTH, &out), AX_OK);
    AX_CHECK_EQ_U(out.dimension.unit, AX_UNIT_AUTO);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(the_corner_radius_shorthand_sets_four_and_reads_their_common_value)
{
    fixture f;
    ax_node n;
    ax_value v;
    ax_value out;
    uint32_t row;
    int i;
    fixture_init(&f);
    n = make(&f.scene);
    v = f32v(12.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_CORNER_RADIUS, AX_VALUE_F32, &v), AX_OK);
    row = ax_node_store_row(&f.scene.store, n);
    for (i = 0; i < 4; ++i) {
        AX_CHECK(f.scene.store.paint[row].corner_radius[i] == 12.0f);
    }
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_CORNER_RADIUS, &out), AX_OK);
    AX_CHECK(out.f32 == 12.0f);

    v = f32v(4.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, AX_PROP_CORNER_RADIUS_TOP_RIGHT, AX_VALUE_F32, &v),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_CORNER_RADIUS, &out),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(strstr(ax_last_error()->message.ptr, "longhands differ") != NULL);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_CORNER_RADIUS_TOP_RIGHT, &out), AX_OK);
    AX_CHECK(out.f32 == 4.0f);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

/* ---- Run-time registration ------------------------------------------------------------------- */

AX_TEST(registered_properties_live_in_side_components)
{
    fixture f;
    ax_prop_desc d;
    ax_prop plugin_a;
    ax_prop plugin_b;
    ax_prop idl;
    ax_node n;
    ax_node other;
    ax_value v;
    ax_value out;
    const ax_prop_desc *found;
    fixture_init(&f);

    memset(&d, 0, sizeof d);
    d.name = "x-glow";
    d.type = AX_VALUE_F32;
    d.flags = AX_PROP_ANIMATABLE;
    d.effect = AX_EFFECT_PAINT;
    d.default_value.f32 = 2.0f;
    d.min_value = 0.0f;
    d.max_value = 10.0f;
    AX_REQUIRE(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, &plugin_a) == AX_OK);
    AX_CHECK_EQ_U(plugin_a, AX_PROP_FIRST_DYNAMIC);
    d.name = "x-tint";
    d.type = AX_VALUE_COLOR;
    AX_REQUIRE(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, &plugin_b) == AX_OK);
    AX_CHECK_EQ_U(plugin_b, AX_PROP_FIRST_DYNAMIC + 1u);
    d.name = "kit-switch-on";
    d.type = AX_VALUE_BOOL;
    d.id = AX_PROP_FIRST_IDL + 5u;
    AX_REQUIRE(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, &idl) == AX_OK);
    AX_CHECK_EQ_U(idl, AX_PROP_FIRST_IDL + 5u);

    /* Rejected: a duplicate name or id, an id outside the IDL range, a shorthand, no name. */
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);
    d.name = "kit-other";
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);
    d.id = 0x0500;
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);
    d.id = 0;
    d.name = "width";
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);
    d.name = "x-short";
    d.flags = AX_PROP_SHORTHAND;
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);
    d.flags = 0;
    d.name = NULL;
    AX_CHECK_RESULT(ax_prop_registry_register(&f.scene.props, &f.scene.store, &d, NULL),
                    AX_ERR_INVALID_ARGUMENT);

    found = ax_prop_registry_find(&f.scene.props, plugin_a);
    AX_REQUIRE(found != NULL);
    AX_CHECK_EQ_U(found->storage, AX_PROP_IN_COMPONENT);
    AX_CHECK(ax_prop_registry_find_by_name(&f.scene.props, AX_STR("x-tint")) ==
             ax_prop_registry_find(&f.scene.props, plugin_b));
    AX_CHECK(ax_prop_registry_find(&f.scene.props, idl) != NULL);

    n = make(&f.scene);
    other = make(&f.scene);
    /* The default until set; setting the default allocates nothing. */
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, plugin_a, &out), AX_OK);
    AX_CHECK(out.f32 == 2.0f);
    v = f32v(2.0f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, plugin_a, AX_VALUE_F32, &v), AX_OK);
    AX_CHECK(ax_node_store_get_component(&f.scene.store, n, found->component) == NULL);
    v = f32v(7.5f);
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, plugin_a, AX_VALUE_F32, &v), AX_OK);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, plugin_a, &out), AX_OK);
    AX_CHECK(out.f32 == 7.5f);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, other, plugin_a, &out), AX_OK);
    AX_CHECK(out.f32 == 2.0f);
    v = f32v(11.0f); /* its own range */
    AX_CHECK_RESULT(ax_scene_set(&f.scene, n, plugin_a, AX_VALUE_F32, &v), AX_ERR_INVALID_ARGUMENT);
    memset(&v, 0, sizeof v);
    v.boolean = true;
    AX_CHECK_RESULT(ax_scene_set(&f.scene, other, idl, AX_VALUE_BOOL, &v), AX_OK);
    AX_CHECK_RESULT(ax_scene_get(&f.scene, other, idl, &out), AX_OK);
    AX_CHECK(out.boolean);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}
