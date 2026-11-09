/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Semantic properties (decision 108): stored in their own component, effect SEMANTICS (no dirty
 * flag), validated, and undone with a rejected transaction.
 */
#include <float.h>
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_tracking_allocator.h"
#include "axion/core/ax_a11y.h"
#include "axion/core/ax_scene.h"
#include "axion/core/ax_text_node.h"
#include "axion/core/ax_world_sdk.h"

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

static ax_result set_u32(ax_scene *s, ax_node n, ax_prop p, uint32_t value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.u32 = value;
    return ax_scene_set(s, n, p, AX_VALUE_U32, &v);
}

static ax_result set_f32(ax_scene *s, ax_node n, ax_prop p, float value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.f32 = value;
    return ax_scene_set(s, n, p, AX_VALUE_F32, &v);
}

static ax_result set_bool(ax_scene *s, ax_node n, ax_prop p, bool value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.boolean = value;
    return ax_scene_set(s, n, p, AX_VALUE_BOOL, &v);
}

static bool str_is(const ax_shared_str *s, const char *expected)
{
    ax_str v = ax_shared_str_view(s);
    return v.len == strlen(expected) && (v.len == 0 || memcmp(v.ptr, expected, v.len) == 0);
}

AX_TEST(semantic_properties_are_stored_in_their_component_and_mark_nothing)
{
    fixture f;
    ax_node n;
    ax_node_semantics *m;
    ax_value out;
    bool set = true;
    fixture_init(&f);
    n = make(&f.scene);
    ax_scene_clear_dirty(&f.scene);
    AX_CHECK(ax_scene_semantics(&f.scene, n) == NULL); /* nothing until a semantic property */

    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_ROLE, AX_ROLE_BUTTON), AX_OK);
    AX_CHECK_RESULT(ax_scene_set_str(&f.scene, n, AX_PROP_LABEL, AX_STR("Save")), AX_OK);
    AX_CHECK_RESULT(ax_scene_set_str(&f.scene, n, AX_PROP_DESCRIPTION, AX_STR("Saves the file")),
                    AX_OK);
    AX_CHECK_RESULT(ax_scene_set_str(&f.scene, n, AX_PROP_TEST_ID, AX_STR("save")), AX_OK);
    AX_CHECK_RESULT(ax_scene_set_str(&f.scene, n, AX_PROP_VALUE_TEXT, AX_STR("50%")), AX_OK);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE, 50.0f), AX_OK);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE_MIN, -1.0f), AX_OK);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE_MAX, 100.0f), AX_OK);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE_STEP, 5.0f), AX_OK);
    AX_CHECK_RESULT(set_bool(&f.scene, n, AX_PROP_DISABLED, true), AX_OK);
    AX_CHECK_RESULT(set_bool(&f.scene, n, AX_PROP_HIDDEN, true), AX_OK);
    AX_CHECK_RESULT(set_bool(&f.scene, n, AX_PROP_SELECTED, true), AX_OK);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_CHECKED, AX_TOGGLED_MIXED), AX_OK);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_EXPANDED, AX_EXPANDED_COLLAPSED), AX_OK);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_ACTIONS, AX_ACTION_FOCUS | AX_ACTION_DEFAULT),
                    AX_OK);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_LEVEL, 2), AX_OK);

    /* Effect SEMANTICS: no dirty flag, so nothing is laid out, recorded or composited. */
    AX_CHECK_EQ_U(ax_scene_dirty_flags(&f.scene, n), 0);
    AX_CHECK_EQ_U(f.scene.dirty.layout_root_count, 0);

    m = ax_scene_semantics(&f.scene, n);
    AX_REQUIRE(m != NULL);
    AX_CHECK_EQ_U(m->own, 0xFFFFu); /* sixteen properties, a bit each */
    AX_CHECK_EQ_U(m->role, AX_ROLE_BUTTON);
    AX_CHECK(str_is(m->label, "Save"));
    AX_CHECK(str_is(m->description, "Saves the file"));
    AX_CHECK(str_is(m->test_id, "save"));
    AX_CHECK(str_is(m->value_text, "50%"));
    AX_CHECK(m->value == 50.0f && m->value_min == -1.0f && m->value_max == 100.0f);
    AX_CHECK(m->value_step == 5.0f);
    AX_CHECK(m->disabled && m->hidden && m->selected);
    AX_CHECK_EQ_U(m->checked, AX_TOGGLED_MIXED);
    AX_CHECK_EQ_U(m->expanded, AX_EXPANDED_COLLAPSED);
    AX_CHECK_EQ_U(m->actions, AX_ACTION_FOCUS | AX_ACTION_DEFAULT);
    AX_CHECK_EQ_U(m->level, 2);

    /* Read back by id; an optional number can be unset. */
    AX_CHECK_RESULT(ax_scene_get(&f.scene, n, AX_PROP_LABEL, &out), AX_OK);
    AX_CHECK(str_is(out.str, "Save"));
    AX_CHECK_RESULT(ax_scene_get_own(&f.scene, n, AX_PROP_VALUE, &out, &set), AX_OK);
    AX_CHECK(set && out.f32 == 50.0f);
    AX_CHECK_RESULT(ax_scene_unset(&f.scene, n, AX_PROP_VALUE), AX_OK);
    AX_CHECK_RESULT(ax_scene_get_own(&f.scene, n, AX_PROP_VALUE, &out, &set), AX_OK);
    AX_CHECK(!set);
    AX_CHECK_RESULT(ax_scene_unset(&f.scene, n, AX_PROP_LABEL), AX_OK);
    AX_CHECK(m->label == NULL);
    AX_CHECK_EQ_U(ax_scene_dirty_flags(&f.scene, n), 0);

    /* A fresh node reads the defaults. */
    {
        ax_node other = make(&f.scene);
        AX_CHECK_RESULT(ax_scene_get(&f.scene, other, AX_PROP_ROLE, &out), AX_OK);
        AX_CHECK_EQ_U(out.u32, AX_ROLE_NONE);
        AX_CHECK_RESULT(ax_scene_get(&f.scene, other, AX_PROP_LABEL, &out), AX_OK);
        AX_CHECK(out.str == NULL);
    }
    /* The component's strings go with the node. */
    AX_CHECK_RESULT(ax_scene_destroy(&f.scene, n), AX_OK);
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

AX_TEST(invalid_semantic_values_are_rejected)
{
    fixture f;
    ax_node n;
    fixture_init(&f);
    n = make(&f.scene);
    /* TEXT_RUN is the tree's own role; unknown roles, states and action bits. */
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_ROLE, AX_ROLE_TEXT_RUN), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_ROLE, 999), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_CHECKED, 4), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_EXPANDED, 3), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_ACTIONS, 0x20), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_u32(&f.scene, n, AX_PROP_LEVEL, 10), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE, NAN), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(set_f32(&f.scene, n, AX_PROP_VALUE_STEP, -1.0f), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_scene_set_str(&f.scene, n, AX_PROP_LABEL, AX_STR("\xC0\x80")),
                    AX_ERR_INVALID_ARGUMENT);
    AX_CHECK(ax_scene_semantics(&f.scene, n) == NULL); /* nothing was stored */
    AX_CHECK_EQ_U(fixture_deinit(&f), 0);
}

static uint64_t read_clock(void *user)
{
    return *(const uint64_t *)user;
}

AX_TEST(a_rejected_transaction_undoes_semantic_values)
{
    uint64_t now = 1000;
    ax_world w = AX_HANDLE_NULL_INIT;
    ax_world_desc desc;
    ax_txn *t;
    ax_node n, stale;
    ax_value out;
    bool set = true;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.clock = read_clock;
    desc.clock_user = &now;
    AX_REQUIRE(ax_world_create(&desc, &w) == AX_OK);

    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_str(t, n, AX_PROP_LABEL, "Before", 6);
    ax_txn_set_u32(t, n, AX_PROP_ROLE, AX_ROLE_BUTTON);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);

    stale.h = ax_handle_make(ax_handle_slot(n.h), ax_handle_generation(n.h) + 3u);
    t = ax_txn_begin(w);
    ax_txn_set_str(t, n, AX_PROP_LABEL, "After", 5);
    ax_txn_set_u32(t, n, AX_PROP_ROLE, AX_ROLE_LINK);
    ax_txn_set_f32(t, n, AX_PROP_VALUE, 3.0f);       /* was unset: unset again */
    ax_txn_set_bool(t, stale, AX_PROP_HIDDEN, true); /* fails */
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    AX_CHECK_EQ_U(ax_world_apply_pending(w, UINT64_MAX), 1);

    AX_CHECK_RESULT(ax_scene_get(ax_world_scene(w), n, AX_PROP_LABEL, &out), AX_OK);
    AX_CHECK(str_is(out.str, "Before"));
    AX_CHECK_RESULT(ax_scene_get(ax_world_scene(w), n, AX_PROP_ROLE, &out), AX_OK);
    AX_CHECK_EQ_U(out.u32, AX_ROLE_BUTTON);
    AX_CHECK_RESULT(ax_scene_get_own(ax_world_scene(w), n, AX_PROP_VALUE, &out, &set), AX_OK);
    AX_CHECK(!set);
    AX_CHECK_RESULT(ax_world_destroy(w), AX_OK);
}
