/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Text in the scene (decision 99): the string value type, ax_txn_set_str and its checks, undoing
 * a rejected transaction, the mirror's text, and the style pass that resolves inherited values.
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_scene.h"
#include "axion/core/ax_text_node.h"
#include "axion/core/ax_world_sdk.h"

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

static void frame(ax_world w)
{
    (void)ax_world_apply_pending(w, UINT64_MAX);
    (void)ax_world_publish_mirror(w);
}

static bool text_is(ax_world w, ax_node n, const char *expected)
{
    char buf[64];
    size_t len = 0;
    if (ax_mirror_get_str(w, n, AX_PROP_TEXT, buf, sizeof buf, &len) != AX_OK) {
        return false;
    }
    return len == strlen(expected) && strcmp(buf, expected) == 0;
}

AX_TEST(text_content_reaches_the_scene_and_the_mirror)
{
    ax_world w = make_world();
    ax_scene *scene;
    ax_txn *t;
    ax_node n;
    ax_value v;
    char small[4];
    size_t len = 0;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    scene = ax_world_scene(w);

    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_str(t, n, AX_PROP_TEXT, "Hola, mundo \xC3\xA9", 14);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);

    AX_CHECK_RESULT(ax_scene_get(scene, n, AX_PROP_TEXT, &v), AX_OK);
    AX_REQUIRE(v.str != NULL);
    AX_CHECK_EQ_U(v.str->len, 14u);
    AX_CHECK(memcmp(v.str->data, "Hola, mundo \xC3\xA9", 14) == 0);
    AX_CHECK(text_is(w, n, "Hola, mundo \xC3\xA9"));

    /* A short buffer gets a NUL-terminated prefix and the whole length. */
    AX_CHECK_RESULT(ax_mirror_get_str(w, n, AX_PROP_TEXT, small, sizeof small, &len), AX_OK);
    AX_CHECK_EQ_U(len, 14u);
    AX_CHECK(strcmp(small, "Hol") == 0);
    AX_CHECK_RESULT(ax_mirror_get_str(w, n, AX_PROP_TEXT, NULL, 0, &len), AX_OK);
    AX_CHECK_EQ_U(len, 14u);

    /* Changing it, and the empty string. */
    t = ax_txn_begin(w);
    ax_txn_set_str(t, n, AX_PROP_TEXT, "Adi\xC3\xB3s", 6);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);
    AX_CHECK(text_is(w, n, "Adi\xC3\xB3s"));
    t = ax_txn_begin(w);
    ax_txn_set_str(t, n, AX_PROP_TEXT, NULL, 0);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    (void)ax_world_mirror_sync(w);
    AX_CHECK(text_is(w, n, ""));

    /* Style properties are not mirrored yet; other getters reject strings. */
    AX_CHECK_RESULT(ax_mirror_get_str(w, n, AX_PROP_FONT_FAMILY, NULL, 0, &len),
                    AX_ERR_UNSUPPORTED);
    AX_CHECK_RESULT(ax_mirror_get_str(w, n, AX_PROP_OPACITY, NULL, 0, &len),
                    AX_ERR_INVALID_ARGUMENT);
    {
        ax_rgba c;
        AX_CHECK_RESULT(ax_mirror_get_color(w, n, AX_PROP_COLOR, &c), AX_ERR_UNSUPPORTED);
    }
    ax_world_destroy(w);
}

static ax_result commit_error(ax_world w, void (*record)(ax_txn *, ax_node))
{
    ax_txn *t = ax_txn_begin(w);
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    record(t, n);
    return ax_txn_commit(t, NULL);
}

static void bad_utf8(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_TEXT, "a\xC3", 2); /* truncated sequence */
}
static void overlong(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_TEXT, "\xC0\xAF", 2);
}
static void surrogate(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_TEXT, "\xED\xA0\x80", 3);
}
static void bad_features(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_FONT_FEATURES, "\"liga\" maybe", 12);
}
static void not_a_string(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_OPACITY, "1", 1);
}
static void string_as_f32(ax_txn *t, ax_node n)
{
    ax_txn_set_f32(t, n, AX_PROP_TEXT, 1.0f);
}
static void size_zero(ax_txn *t, ax_node n)
{
    ax_txn_set_f32(t, n, AX_PROP_FONT_SIZE, 0.0f);
}
static void good_features(ax_txn *t, ax_node n)
{
    ax_txn_set_str(t, n, AX_PROP_FONT_FEATURES, "\"tnum\", liga 0, 'smcp' on", 25);
}

AX_TEST(string_values_are_checked_when_recorded)
{
    ax_world w = make_world();
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    AX_CHECK_RESULT(commit_error(w, bad_utf8), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, overlong), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, surrogate), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, bad_features), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, not_a_string), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, string_as_f32), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, size_zero), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(commit_error(w, good_features), AX_OK);
    ax_world_destroy(w);
}

/* A rejected transaction undoes its strings and leaves an inherited property unset again. */
AX_TEST(a_rejected_transaction_restores_text_values)
{
    ax_world w = make_world();
    ax_scene *scene;
    ax_txn *t;
    ax_node n, gone;
    ax_value v;
    bool set = true;
    ax_txn_error err;
    AX_REQUIRE(!AX_HANDLE_IS_NULL(w));
    scene = ax_world_scene(w);

    t = ax_txn_begin(w);
    n = ax_txn_create(t, AX_NODE_VIEW);
    gone = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_str(t, n, AX_PROP_TEXT, "first", 5);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    t = ax_txn_begin(w);
    ax_txn_remove(t, gone);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);

    t = ax_txn_begin(w);
    ax_txn_set_str(t, n, AX_PROP_TEXT, "second", 6);
    ax_txn_set_f32(t, n, AX_PROP_FONT_SIZE, 30.0f);
    ax_txn_set_str(t, n, AX_PROP_FONT_FAMILY, "Serif", 5);
    ax_txn_set_f32(t, gone, AX_PROP_OPACITY, 0.5f); /* stale: rejects the transaction */
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    frame(w);
    AX_CHECK(ax_world_poll_txn_error(w, &err, NULL));

    AX_CHECK_RESULT(ax_scene_get(scene, n, AX_PROP_TEXT, &v), AX_OK);
    AX_REQUIRE(v.str != NULL);
    AX_CHECK(v.str->len == 5 && memcmp(v.str->data, "first", 5) == 0);
    AX_CHECK_RESULT(ax_scene_get_own(scene, n, AX_PROP_FONT_SIZE, &v, &set), AX_OK);
    AX_CHECK(!set);
    AX_CHECK_NEAR_F(v.f32, 14.0f, 0.0f);
    AX_CHECK_RESULT(ax_scene_get_own(scene, n, AX_PROP_FONT_FAMILY, &v, &set), AX_OK);
    AX_CHECK(!set);
    AX_CHECK(v.str == NULL);
    ax_world_destroy(w); /* the tracking of strings: ASan reports a leak if a reference stays */
}

/* ---- The style pass -------------------------------------------------------------------------- */

static ax_node node(ax_scene *s)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    (void)ax_scene_create(s, AX_NODE_VIEW, &n);
    return n;
}

static void set_f32(ax_scene *s, ax_node n, ax_prop p, float f)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.f32 = f;
    (void)ax_scene_set(s, n, p, AX_VALUE_F32, &v);
}

static void set_color(ax_scene *s, ax_node n, float r)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.color = ax_color_make(r, 0.0f, 0.0f, 1.0f);
    (void)ax_scene_set(s, n, AX_PROP_COLOR, AX_VALUE_COLOR, &v);
}

AX_TEST(the_style_pass_resolves_inherited_values_top_down)
{
    ax_scene scene;
    ax_node root, panel, label, note, other;
    ax_node_text *tl, *tn;
    ax_scene_init(&scene, NULL);
    root = node(&scene);
    panel = node(&scene);
    label = node(&scene);
    note = node(&scene);
    other = node(&scene);
    (void)ax_scene_insert_before(&scene, root, panel, (ax_node)AX_HANDLE_NULL_INIT);
    (void)ax_scene_insert_before(&scene, panel, label, (ax_node)AX_HANDLE_NULL_INIT);
    (void)ax_scene_insert_before(&scene, label, note, (ax_node)AX_HANDLE_NULL_INIT);
    (void)ax_scene_insert_before(&scene, root, other, (ax_node)AX_HANDLE_NULL_INIT);

    set_f32(&scene, root, AX_PROP_FONT_SIZE, 20.0f);   /* root sets; panel has no component */
    (void)ax_scene_set_str(&scene, label, AX_PROP_TEXT, AX_STR("Label"));
    (void)ax_scene_set_str(&scene, note, AX_PROP_TEXT, AX_STR("Note"));
    set_f32(&scene, note, AX_PROP_FONT_SIZE, 12.0f);
    set_f32(&scene, other, AX_PROP_FONT_SIZE, 30.0f);
    set_color(&scene, other, 1.0f);

    AX_CHECK_EQ_U(ax_scene_resolve_styles(&scene), 4u);
    tl = ax_scene_text(&scene, label);
    tn = ax_scene_text(&scene, note);
    AX_REQUIRE(tl != NULL && tn != NULL);
    AX_CHECK_NEAR_F(tl->computed.size, 20.0f, 0.0f); /* through panel, which has no component */
    AX_CHECK_NEAR_F(tl->computed.weight, 400.0f, 0.0f); /* default */
    AX_CHECK_NEAR_F(tn->computed.size, 12.0f, 0.0f);    /* its own */
    AX_CHECK(tl->paragraph_stale);
    ax_scene_clear_dirty(&scene);
    tl->paragraph_stale = false;
    tn->paragraph_stale = false;

    /* Nothing marked: nothing resolved. */
    AX_CHECK_EQ_U(ax_scene_resolve_styles(&scene), 0u);

    /* Changing the root's size reaches the label (layout, paragraph) but not the note's own. */
    set_f32(&scene, root, AX_PROP_FONT_SIZE, 22.0f);
    AX_CHECK(ax_scene_dirty_flags(&scene, label) & AX_DIRTY_STYLE);
    (void)ax_scene_resolve_styles(&scene);
    AX_CHECK_NEAR_F(tl->computed.size, 22.0f, 0.0f);
    AX_CHECK_NEAR_F(tn->computed.size, 12.0f, 0.0f);
    AX_CHECK(tl->paragraph_stale);
    AX_CHECK(!tn->paragraph_stale);
    AX_CHECK(ax_scene_dirty_flags(&scene, label) & AX_DIRTY_LAYOUT);
    ax_scene_clear_dirty(&scene);
    tl->paragraph_stale = false;

    /* Moving the label under `other` gives it other's size and color: paint and layout. */
    (void)ax_scene_insert_before(&scene, other, label, (ax_node)AX_HANDLE_NULL_INIT);
    AX_CHECK(ax_scene_dirty_flags(&scene, note) & AX_DIRTY_STYLE);
    (void)ax_scene_resolve_styles(&scene);
    tl = ax_scene_text(&scene, label);
    tn = ax_scene_text(&scene, note);
    AX_CHECK_NEAR_F(tl->computed.size, 30.0f, 0.0f);
    AX_CHECK_NEAR_F(tl->computed.color.r, 1.0f, 0.0f);
    AX_CHECK_NEAR_F(tn->computed.color.r, 1.0f, 0.0f); /* through the label */
    AX_CHECK(ax_scene_dirty_flags(&scene, label) & AX_DIRTY_PAINT);
    ax_scene_clear_dirty(&scene);

    /* A color change alone repaints, without a new paragraph. */
    tl->paragraph_stale = false;
    set_color(&scene, other, 0.5f);
    (void)ax_scene_resolve_styles(&scene);
    AX_CHECK_NEAR_F(tl->computed.color.r, 0.5f, 0.0f);
    AX_CHECK(!tl->paragraph_stale);
    AX_CHECK(ax_scene_dirty_flags(&scene, label) & AX_DIRTY_PAINT);

    /* Unsetting the note's size: it inherits again. */
    ax_scene_clear_dirty(&scene);
    AX_CHECK_RESULT(ax_scene_unset(&scene, note, AX_PROP_FONT_SIZE), AX_OK);
    (void)ax_scene_resolve_styles(&scene);
    AX_CHECK_NEAR_F(tn->computed.size, 30.0f, 0.0f);
    ax_scene_deinit(&scene);
}
