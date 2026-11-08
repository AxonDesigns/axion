/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The headless host and its C testing API (decision 85): a tree drawn after settle() against its
 * golden, the manual clock and frame deadlines, advancing time, resizing, the IR dump, mirror
 * reads, surface settings, many instances (AX_MAX_WORLDS at once, eight on threads in parallel,
 * all identical), the real-threads mode giving the same pixels, and the GPU renderer offscreen
 * when there is one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_thread.h"
#include "axion/headless/ax_headless.h"
#include "axt_golden.h"
#if defined(AXT_HAVE_GPU)
#    include "gpu_test_util.h"
#endif

#define MS 1000000ull

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static ax_rgba c8(uint32_t argb)
{
    return ax_rgba_from_srgb8((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                              (uint8_t)(argb >> 24));
}

static ax_transform2d rotation(float radians)
{
    const ax_affine2 m = ax_affine2_rotate(radians);
    ax_transform2d t;
    t.a = m.a;
    t.b = m.b;
    t.c = m.c;
    t.d = m.d;
    t.tx = m.tx;
    t.ty = m.ty;
    return t;
}

static ax_node box(ax_txn *t, ax_node parent, float x, float y, float w, float h, uint32_t argb)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, n, AX_PROP_POSITION, AX_POSITION_ABSOLUTE); /* placed by its insets */
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
    ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR, c8(argb));
    ax_txn_append_child(t, parent, n);
    return n;
}

/* A small card UI: a background, a rounded card with a header bar, a turned badge, a translucent
 * layer and a hairline. Returns the card. */
static ax_node build(ax_world w, ax_node root)
{
    ax_txn *t = ax_txn_begin(w);
    ax_node card, badge, veil;
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, c8(0xFF1E2129u));
    card = box(t, root, 16.5f, 20.25f, 160, 96, 0xFFE8ECF4u);
    ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 12.0f);
    (void)box(t, card, 12, 12, 136, 18, 0xFF4F7CFFu);
    badge = box(t, root, 140, 100, 40, 24, 0xFFF7C948u);
    ax_txn_set_f32(t, badge, AX_PROP_CORNER_RADIUS, 6.0f);
    ax_txn_set_transform(t, badge, AX_PROP_TRANSFORM, rotation(0.3f));
    veil = box(t, root, 40, 60, 80, 70, 0xFFFF7A59u);
    ax_txn_set_f32(t, veil, AX_PROP_OPACITY, 0.5f);
    (void)box(t, root, 8, 140.3f, 180, 0.5f, 0xFFFFFFFFu);
    AX_CHECK_RESULT(ax_txn_commit(t, NULL), AX_OK);
    return card;
}

static ax_headless *make(uint32_t flags)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    d.flags = flags;
    if (ax_headless_create(&d, &h) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "ax_headless_create: %s", ax_last_error()->message.ptr);
    }
    return h;
}

static ax_headless_surface_desc surface(float w, float h, float scale)
{
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    s.width = w;
    s.height = h;
    s.scale = scale;
    return s;
}

/* Draws the card UI on a fresh instance and returns its pixels (caller frees). */
static uint8_t *draw_cards(uint32_t flags, uint32_t *out_w, uint32_t *out_h)
{
    ax_headless *h = make(flags);
    ax_headless_surface_desc s = surface(200, 160, 1.5f);
    ax_headless_image img;
    ax_node root;
    uint8_t *px = NULL;
    if (h == NULL) {
        return NULL;
    }
    if (ax_headless_add_surface(h, &s, &root) == AX_OK) {
        (void)build(ax_headless_world(h), root);
        if (ax_headless_settle(h, 100, NULL) == AX_OK &&
            ax_headless_capture(h, root, &img) == AX_OK) {
            px = (uint8_t *)malloc(img.stride * img.height);
            if (px != NULL) {
                memcpy(px, img.rgba, img.stride * img.height);
            }
            *out_w = img.width;
            *out_h = img.height;
            ax_headless_image_free(h, &img);
        }
    }
    ax_headless_destroy(h);
    return px;
}

AX_TEST(a_tree_is_drawn_after_settle_and_matches_its_golden)
{
    ax_headless *h = make(0);
    ax_headless_surface_desc s = surface(200, 160, 1.5f);
    ax_headless_image img;
    ax_headless_surface_info info;
    ax_node root;
    uint32_t frames = 99;
    AX_REQUIRE(h != NULL);
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    (void)build(ax_headless_world(h), root);
    AX_REQUIRE(ax_headless_settle(h, 100, &frames) == AX_OK);
    AX_CHECK(frames >= 1 && frames <= 2);
    AX_REQUIRE(ax_headless_capture(h, root, &img) == AX_OK);
    AX_CHECK_EQ_U(img.width, 300);
    AX_CHECK_EQ_U(img.height, 240);
    AXT_CHECK_GOLDEN("headless", "cards", img.rgba, img.width, img.height, img.stride);
    ax_headless_image_free(h, &img);
    /* Nothing pending: settling runs no frame. */
    AX_REQUIRE(ax_headless_settle(h, 100, &frames) == AX_OK);
    AX_CHECK_EQ_U(frames, 0);
    info.struct_size = sizeof info;
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
    AX_CHECK(info.shown_ir > 0);
    AX_CHECK_EQ_U(info.pixel_width, 300);
    ax_headless_destroy(h);
}

AX_TEST(the_manual_clock_sets_each_frame_s_deadline)
{
    ax_headless *h = make(0);
    ax_headless_surface_desc s = surface(100, 80, 1.0f);
    ax_headless_surface_info info;
    ax_world w;
    ax_node root, card;
    uint64_t t0;
    ax_txn *t;
    AX_REQUIRE(h != NULL);
    w = ax_headless_world(h);
    t0 = ax_headless_now(h);
    AX_CHECK_EQ_U(t0, 1000000000ull);
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    t = ax_txn_begin(w);
    card = box(t, root, 10, 10, 20, 20, 0xFFFFFFFFu);
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    /* Committed now: the next frame (presented one refresh later) applies it. */
    AX_REQUIRE(ax_headless_step(h, 1) == AX_OK);
    AX_CHECK_EQ_U(ax_headless_now(h), t0 + 16666667ull);
    AX_CHECK_EQ_U(ax_world_last_applied_seq(w), ax_world_last_committed_seq(w));
    /* The mirror shows the frame's layout without a round trip (decision 69). */
    AX_CHECK(ax_world_mirror_sync(w) == ax_world_last_committed_seq(w));
    {
        ax_rectf f;
        ax_rgba c;
        AX_REQUIRE(ax_mirror_get_frame(w, card, &f) == AX_OK);
        AX_CHECK(f.x == 10.0f && f.w == 20.0f);
        AX_REQUIRE(ax_mirror_get_color(w, card, AX_PROP_BACKGROUND_COLOR, &c) == AX_OK);
        AX_CHECK(c.r == 1.0f);
    }
    /* Advancing time runs a frame for each refresh crossed: 60 ms crosses three (16.67, 33.33,
     * 50.00 ms); then 10 ms crosses none, and 7 more cross the fourth. */
    info.struct_size = sizeof info;
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
    {
        const uint64_t before = info.frames;
        AX_REQUIRE(ax_headless_advance_time(h, 60 * MS) == AX_OK);
        AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
        AX_CHECK_EQ_U(info.frames - before, 3);
        AX_CHECK_EQ_U(ax_headless_now(h), t0 + 16666667ull + 60 * MS);
        AX_REQUIRE(ax_headless_advance_time(h, 6 * MS) == AX_OK);
        AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
        AX_CHECK_EQ_U(info.frames - before, 3);
        AX_REQUIRE(ax_headless_advance_time(h, 7 * MS) == AX_OK);
        AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
        AX_CHECK_EQ_U(info.frames - before, 4);
    }
    ax_headless_destroy(h);
}

AX_TEST(settle_reports_when_it_cannot)
{
    ax_headless *h = make(0);
    ax_headless_surface_desc s = surface(64, 64, 1.0f);
    ax_node root;
    uint32_t frames = 7;
    AX_REQUIRE(h != NULL);
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    AX_CHECK_RESULT(ax_headless_settle(h, 0, &frames), AX_ERR_TIMEOUT); /* the root's txn */
    AX_CHECK_EQ_U(frames, 0);
    AX_CHECK_RESULT(ax_headless_settle(h, 5, &frames), AX_OK);
    AX_CHECK_EQ_U(frames, 1);
    ax_headless_destroy(h);
}

static void dump_write(void *ctx, const char *s, size_t n)
{
    char *buf = (char *)ctx;
    size_t len = strlen(buf);
    if (len + n < 8192) {
        memcpy(buf + len, s, n);
        buf[len + n] = '\0';
    }
}

AX_TEST(resizing_redraws_at_the_new_size_and_the_ir_follows)
{
    ax_headless *h = make(0);
    ax_headless_surface_desc s = surface(120, 90, 1.0f);
    ax_headless_image img;
    ax_headless_surface_info info;
    static char dump[8192];
    uint8_t *blob;
    size_t blob_size;
    ax_node root;
    AX_REQUIRE(h != NULL);
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    (void)build(ax_headless_world(h), root);
    AX_REQUIRE(ax_headless_settle(h, 10, NULL) == AX_OK);
    AX_REQUIRE(ax_headless_resize(h, root, 240, 100, 1.25f) == AX_OK);
    AX_REQUIRE(ax_headless_step(h, 1) == AX_OK);
    AX_REQUIRE(ax_headless_capture(h, root, &img) == AX_OK);
    AX_CHECK_EQ_U(img.width, 300);
    AX_CHECK_EQ_U(img.height, 125);
    ax_headless_image_free(h, &img);
    dump[0] = '\0';
    AX_REQUIRE(ax_headless_dump_ir(h, root, dump_write, dump) == AX_OK);
    AX_CHECK(strstr(dump, "240x100") != NULL);
    AX_CHECK(strstr(dump, "rounded_rect") != NULL);
    AX_REQUIRE(ax_headless_save_ir(h, root, &blob, &blob_size) == AX_OK);
    AX_CHECK(blob_size > 16 && memcmp(blob, "AXIR", 4) == 0);
    ax_headless_free(h, blob, blob_size);
    info.struct_size = sizeof info;
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
    AX_CHECK(info.width == 240.0f && info.scale == 1.25f);
    ax_headless_destroy(h);
}

AX_TEST(surface_settings_and_argument_errors)
{
    ax_headless *h = make(0);
    ax_headless_surface_desc s = surface(64, 64, 2.0f);
    ax_headless_surface_info info;
    ax_headless_image img;
    ax_headless_desc bad = ax_headless_default_desc();
    ax_headless *none = NULL;
    const ax_node unknown = {12345};
    ax_node root;
    AX_REQUIRE(h != NULL);
    s.theme = AX_HEADLESS_THEME_DARK;
    s.reduce_motion = true;
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    info.struct_size = sizeof info;
    AX_REQUIRE(ax_headless_get_surface_info(h, root, &info) == AX_OK);
    AX_CHECK_EQ_U(info.theme, AX_HEADLESS_THEME_DARK);
    AX_CHECK(info.reduce_motion);
    AX_CHECK_EQ_U(info.pixel_width, 128);
    AX_CHECK_RESULT(ax_headless_capture(h, root, &img), AX_ERR_UNSUPPORTED); /* no frame yet */
    AX_CHECK_RESULT(ax_headless_capture(h, unknown, &img), AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(ax_headless_resize(h, root, 0, 10, 1), AX_ERR_INVALID_ARGUMENT);
    s.scale = 0.0f;
    AX_CHECK_RESULT(ax_headless_add_surface(h, &s, &root), AX_ERR_INVALID_ARGUMENT);
    s.struct_size = 4;
    AX_CHECK_RESULT(ax_headless_add_surface(h, &s, &root), AX_ERR_ABI_MISMATCH);
    bad.refresh_ns = 0;
    AX_CHECK_RESULT(ax_headless_create(&bad, &none), AX_ERR_INVALID_ARGUMENT);
    bad = ax_headless_default_desc();
    bad.renderer = 9;
    AX_CHECK_RESULT(ax_headless_create(&bad, &none), AX_ERR_INVALID_ARGUMENT);
    AX_CHECK_RESULT(ax_headless_remove_surface(h, root), AX_OK);
    AX_CHECK_RESULT(ax_headless_remove_surface(h, root), AX_ERR_INVALID_HANDLE);
    ax_headless_destroy(h);
}

/* Instances are independent: AX_MAX_WORLDS of them at once, each with its own world. */
AX_TEST(ax_max_worlds_instances_at_once)
{
    static ax_headless *hs[AX_MAX_WORLDS];
    uint32_t i, made = 0;
    for (i = 0; i < AX_MAX_WORLDS; ++i) {
        ax_headless_desc d = ax_headless_default_desc();
        if (ax_headless_create(&d, &hs[i]) != AX_OK) {
            break;
        }
        made++;
    }
    AX_CHECK_EQ_U(made, AX_MAX_WORLDS);
    for (i = 0; i < made; ++i) {
        ax_headless_destroy(hs[i]);
    }
}

typedef struct parallel_run {
    uint8_t *px;
    uint32_t w, h;
} parallel_run;

static int parallel_main(void *arg)
{
    parallel_run *r = (parallel_run *)arg;
    r->px = draw_cards(0, &r->w, &r->h);
    return 0;
}

AX_TEST(instances_on_threads_in_parallel_draw_the_same_pixels)
{
    enum { N = 8 };
    static parallel_run runs[N];
    ax_thread threads[N];
    uint32_t i;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    memset(runs, 0, sizeof runs);
    for (i = 0; i < N; ++i) {
        AX_REQUIRE(ax_thread_create(&threads[i], parallel_main, &runs[i]) == AX_OK);
    }
    for (i = 0; i < N; ++i) {
        ax_thread_join(&threads[i]);
    }
    for (i = 0; i < N; ++i) {
        AX_REQUIRE(runs[i].px != NULL);
        AX_CHECK(runs[i].w == runs[0].w && runs[i].h == runs[0].h);
        AX_CHECK(memcmp(runs[i].px, runs[0].px, (size_t)runs[0].w * runs[0].h * 4u) == 0);
    }
    AXT_CHECK_GOLDEN("headless", "cards", runs[0].px, runs[0].w, runs[0].h, (size_t)runs[0].w * 4u);
    for (i = 0; i < N; ++i) {
        free(runs[i].px);
    }
}

/* Real threads and the real clock: the static tree gives the deterministic mode's pixels. */
AX_TEST(the_threaded_mode_draws_the_same_pixels)
{
    uint32_t w = 0, h = 0, w2 = 0, h2 = 0;
    uint8_t *a, *b;
    if (!ax_threads_supported()) {
        AX_SKIP("needs threads");
    }
    a = draw_cards(0, &w, &h);
    b = draw_cards(AX_HEADLESS_THREADS, &w2, &h2);
    AX_REQUIRE(a != NULL && b != NULL);
    AX_CHECK(w == w2 && h == h2);
    AX_CHECK(memcmp(a, b, (size_t)w * h * 4u) == 0);
    free(a);
    free(b);
}

/* ax_render_wgpu offscreen, when this build and machine have a GPU: the same picture as the
 * software renderer within decision 83's measures (a channel step or two at edges). */
AX_TEST(the_gpu_renderer_draws_offscreen_when_there_is_one)
{
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = surface(200, 160, 1.5f);
    ax_headless *h = NULL;
    ax_headless_image img;
    ax_node root;
    uint32_t w = 0, ht = 0, i, off = 0, worst = 0;
    uint8_t *soft;
    ax_result r;
#if defined(AXT_HAVE_GPU)
    axt_gpu_pin_drivers();
#endif
    d.renderer = AX_HEADLESS_RENDER_WGPU;
    r = ax_headless_create(&d, &h);
    if (r == AX_ERR_UNSUPPORTED) {
        AX_SKIP("no GPU renderer in this build or on this machine");
    }
    AX_REQUIRE(r == AX_OK);
    AX_REQUIRE(ax_headless_add_surface(h, &s, &root) == AX_OK);
    (void)build(ax_headless_world(h), root);
    AX_REQUIRE(ax_headless_settle(h, 100, NULL) == AX_OK);
    AX_REQUIRE(ax_headless_capture(h, root, &img) == AX_OK);
    soft = draw_cards(0, &w, &ht);
    AX_REQUIRE(soft != NULL && w == img.width && ht == img.height);
    for (i = 0; i < w * ht * 4u; ++i) {
        const uint32_t e = (uint32_t)abs((int)img.rgba[i] - (int)soft[i]);
        worst = e > worst ? e : worst;
        off += e > 2u ? 1u : 0u;
    }
    printf("GPU against software: worst %u/255, %u channels beyond 2/255\n", worst, off);
    AX_CHECK(off * 1000u <= w * ht * 4u); /* at most 0.1% of the channels */
    free(soft);
    ax_headless_image_free(h, &img);
    ax_headless_destroy(h);
}
