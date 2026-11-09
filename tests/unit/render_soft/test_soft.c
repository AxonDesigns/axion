/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft: the Render IR's rules, clips, group opacity, blending spaces, never stretching,
 * pixel formats and errors (decision 81).
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axt_scenes.h"
#include "soft_test_util.h"

typedef struct env {
    ax_render_soft *r;
    ax_ir_builder b;
} env;

static void env_init(env *e, uint32_t diag)
{
    ax_render_soft_desc d;
    memset(&d, 0, sizeof d);
    d.diagnostic_flags = diag;
    memset(e, 0, sizeof *e);
    AX_REQUIRE(ax_render_soft_create(NULL, &d, &e->r) == AX_OK);
    ax_ir_builder_init(&e->b, ax_default_allocator());
}

static void env_deinit(env *e)
{
    ax_ir_builder_deinit(&e->b);
    ax_render_soft_destroy(e->r);
}

static const ax_render_ir *finish(env *e)
{
    const ax_render_ir *ir = NULL;
    if (ax_ir_builder_end(&e->b, &ir) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "recording failed");
    }
    return ir;
}

static const uint8_t *px(const uint8_t *img, uint32_t w, uint32_t x, uint32_t y)
{
    return img + ((size_t)y * w + x) * 4u;
}

static ax_color white(void)
{
    return ax_color_make(1, 1, 1, 1);
}

/* A value that is exactly half way (127.5 or 187.5) may round either way. */
#define CHECK_NEAR_U8(actual, expected) \
    AX_CHECK((int)(actual) >= (int)(expected) - 1 && (int)(actual) <= (int)(expected))

#define CHECK_PX(img, w, x, y, r_, g_, b_)        \
    do {                                          \
        const uint8_t *axt__p = px(img, w, x, y); \
        AX_CHECK_EQ_U(axt__p[0], r_);             \
        AX_CHECK_EQ_U(axt__p[1], g_);             \
        AX_CHECK_EQ_U(axt__p[2], b_);             \
    } while (0)

AX_TEST(blending_is_in_srgb_space_with_premultiplied_alpha)
{
    env e;
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 32, 16, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
    got = axt_soft_draw(e.r, finish(&e), 32, 16, 1.0f, NULL);
    AX_REQUIRE(got != NULL);
    CHECK_PX(got, 32, 8, 8, 128, 128, 128); /* 50% white over black: 128, not 188 */
    CHECK_PX(got, 32, 24, 8, 127, 0, 128);  /* 50% blue over red */
    AX_CHECK_EQ_U(px(got, 32, 8, 8)[3], 255);
    free(got);
    env_deinit(&e);
}

/* The same content in a linear layer, and with the diagnostic that makes every layer linear
 * (decision 58's comparison): 50% of linear 1 is 0.5, encoded 188; half a pixel too. */
static void check_linear(uint8_t *got, uint32_t w, uint32_t ox)
{
    const uint8_t *a = px(got, w, ox + 8, 8);
    const uint8_t *b = px(got, w, ox + 24, 8);
    const uint8_t *c = px(got, w, ox + 16, 20);
    CHECK_NEAR_U8(a[0], 188);
    CHECK_NEAR_U8(b[0], 188);
    AX_CHECK_EQ_U(b[1], 0);
    CHECK_NEAR_U8(b[2], 188);
    CHECK_NEAR_U8(c[0], 188);
}

static void linear_content(ax_ir_builder *b)
{
    ax_ir_rect(b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
    ax_ir_rect(b, ax_rect_make(16.5f, 18, 4, 4), ax_color_make(1, 1, 1, 1)); /* half of x=16 */
}

AX_TEST(a_linear_layer_blends_in_linear_light)
{
    env e;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 64, 24, 0, ax_color_make(0, 0, 0, 1));
    linear_content(&e.b); /* sRGB space */
    d.flags = AX_IR_LAYER_LINEAR_BLEND;
    d.transform = ax_affine2_translate(32, 0);
    ax_ir_push_layer(&e.b, &d);
    linear_content(&e.b);
    ax_ir_pop_layer(&e.b);
    got = axt_soft_draw(e.r, finish(&e), 64, 24, 1.0f, NULL);
    AX_REQUIRE(got != NULL);
    CHECK_PX(got, 64, 8, 8, 128, 128, 128);
    CHECK_NEAR_U8(px(got, 64, 16, 20)[0], 128);
    check_linear(got, 64, 32);
    free(got);
    env_deinit(&e);
}

/* Issue #4's option 1 (AX_RENDER_BLEND_SRGB_CORRECTED): translucency stays in sRGB space (50%
 * white over black is 128, as CSS), and a shape's edge is corrected for its color's luminance so
 * a half-covered pixel shows what linear light gives against the opposite luminance: 188 for
 * white on black, and 188 for black on white (half of the light taken away). The default blends
 * the same edges to 128 and 128 (the roping of decision 58). */
static void edges(ax_ir_builder *b)
{
    ax_ir_rect(b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    ax_ir_rect(b, ax_rect_make(16.5f, 0, 4, 16), white()); /* half of column 16 */
    ax_ir_rect(b, ax_rect_make(32, 0, 32, 16), white());
    ax_ir_rect(b, ax_rect_make(40.5f, 4, 4, 8), ax_color_make(0, 0, 0, 1)); /* half of col. 40 */
}

static uint8_t *draw_edges(uint32_t blend)
{
    ax_render_soft_desc d;
    ax_render_soft *r = NULL;
    ax_ir_builder b;
    const ax_render_ir *ir = NULL;
    uint8_t *got;
    memset(&d, 0, sizeof d);
    d.blend = blend;
    if (ax_render_soft_create(NULL, &d, &r) != AX_OK) {
        return NULL;
    }
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, 64, 16, 0, ax_color_make(0, 0, 0, 1));
    edges(&b);
    got = ax_ir_builder_end(&b, &ir) == AX_OK ? axt_soft_draw(r, ir, 64, 16, 1.0f, NULL) : NULL;
    ax_ir_builder_deinit(&b);
    ax_render_soft_destroy(r);
    return got;
}

AX_TEST(corrected_srgb_blending_corrects_edges_and_keeps_translucency)
{
    uint8_t *def = draw_edges(AX_RENDER_BLEND_SRGB);
    uint8_t *cor = draw_edges(AX_RENDER_BLEND_SRGB_CORRECTED);
    AX_REQUIRE(def != NULL && cor != NULL);
    CHECK_PX(def, 64, 8, 8, 128, 128, 128);
    CHECK_NEAR_U8(px(def, 64, 16, 8)[0], 128);
    CHECK_NEAR_U8(px(def, 64, 40, 8)[0], 128);
    CHECK_PX(cor, 64, 8, 8, 128, 128, 128); /* translucency: as CSS */
    CHECK_NEAR_U8(px(cor, 64, 16, 8)[0], 188); /* white edge on black */
    CHECK_NEAR_U8(px(cor, 64, 40, 8)[0], 188); /* black edge on white */
    CHECK_PX(cor, 64, 18, 8, 255, 255, 255);   /* full coverage unchanged */
    CHECK_PX(cor, 64, 42, 8, 0, 0, 0);
    free(def);
    free(cor);
}

AX_TEST(linear_blending_by_desc_is_the_diagnostic)
{
    uint8_t *lin = draw_edges(AX_RENDER_BLEND_LINEAR);
    AX_REQUIRE(lin != NULL);
    CHECK_NEAR_U8(px(lin, 64, 8, 8)[0], 188); /* translucency in linear light */
    CHECK_NEAR_U8(px(lin, 64, 16, 8)[0], 188);
    CHECK_NEAR_U8(px(lin, 64, 40, 8)[0], 188);
    free(lin);
}

AX_TEST(the_linear_blend_diagnostic_mixes_every_layer_in_linear_light)
{
    env e;
    uint8_t *got;
    env_init(&e, AX_RENDER_SOFT_DIAG_LINEAR_BLEND);
    ax_ir_builder_begin(&e.b, 32, 24, 0, ax_color_make(0, 0, 0, 1));
    linear_content(&e.b);
    got = axt_soft_draw(e.r, finish(&e), 32, 24, 1.0f, NULL);
    AX_REQUIRE(got != NULL);
    check_linear(got, 32, 0);
    free(got);
    env_deinit(&e);
}

AX_TEST(unknown_items_and_clips_follow_the_ir_rules)
{
    env e;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const uint8_t blob[12] = {0};
    ax_render_counts counts;
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 48, 16, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&e.b, ax_rect_make(1, 1, 6, 6), white());
    ax_ir_item(&e.b, 900, 0, blob, sizeof blob); /* unknown: skipped */
    ax_ir_rect(&e.b, ax_rect_make(9, 1, 6, 6), white());
    d.blend_mode = 77; /* unknown blend mode: drawn as normal */
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(17, 1, 6, 6), white());
    ax_ir_item(&e.b, 901, AX_IR_ITEM_REQUIRED, NULL, 0); /* the rest of this layer is skipped */
    ax_ir_rect(&e.b, ax_rect_make(25, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_RECT; /* drawn: the software renderer applies rect clips */
    d.clip.rect = ax_rect_make(0, 0, 100, 100);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(33, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    d.clip.kind = AX_IR_CLIP_PATH; /* path clips cannot be applied yet: skipped */
    d.clip.path = 0;
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(41, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    got = axt_soft_draw(e.r, finish(&e), 48, 16, 1.0f, &counts);
    AX_REQUIRE(got != NULL);
    AX_CHECK_EQ_U(counts.unknown_items, 2);
    AX_CHECK_EQ_U(counts.skipped_items, 1);
    AX_CHECK_EQ_U(counts.skipped_layers, 1);
    AX_CHECK_EQ_U(counts.neutralized, 1);
    AX_CHECK_EQ_U(counts.approximated, 0);
    AX_CHECK_EQ_U(counts.instances, 4);
    AX_CHECK_EQ_U(px(got, 48, 4, 4)[0], 255);
    AX_CHECK_EQ_U(px(got, 48, 12, 4)[0], 255);
    AX_CHECK_EQ_U(px(got, 48, 20, 4)[0], 255);
    AX_CHECK_EQ_U(px(got, 48, 28, 4)[0], 0);
    AX_CHECK_EQ_U(px(got, 48, 36, 4)[0], 255);
    AX_CHECK_EQ_U(px(got, 48, 44, 4)[0], 0);
    free(got);
    env_deinit(&e);
}

AX_TEST(invalid_items_are_counted_and_skipped)
{
    env e;
    ax_render_counts counts;
    uint8_t *got;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float bad_radii[4] = {1.0f, NAN, 0, 0};
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 16, 16, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, -4, 4), white());
    ax_ir_rect(&e.b, ax_rect_make(0, 0, INFINITY, 4), white());
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 4, 4), ax_color_make(NAN, 0, 0, 1));
    ax_ir_rounded_rect(&e.b, ax_rect_make(0, 0, 4, 4), bad_radii, white());
    d.transform = ax_affine2_scale(0.0f, 1.0f); /* not invertible */
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 4, 4), white());
    ax_ir_pop_layer(&e.b);
    ax_ir_rect(&e.b, ax_rect_make(8, 8, 4, 4), ax_color_make(0, 0, 0, 0)); /* invisible */
    got = axt_soft_draw(e.r, finish(&e), 16, 16, 1.0f, &counts);
    AX_REQUIRE(got != NULL);
    AX_CHECK_EQ_U(counts.invalid_items, 5);
    AX_CHECK_EQ_U(counts.instances, 0);
    AX_CHECK_EQ_U(px(got, 16, 2, 2)[0], 0);
    free(got);
    env_deinit(&e);
}

AX_TEST(rect_and_rounded_clips_are_analytic_and_nest)
{
    env e;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float radii[4] = {8, 8, 8, 8};
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 64, 32, 0, ax_color_make(0, 0, 0, 1));
    /* A rect clip at a fractional edge: the content is cut with the edge's exact coverage. */
    d.clip.kind = AX_IR_CLIP_RECT;
    d.clip.rect = ax_rect_make(2.0f, 2.0f, 10.5f, 20.0f);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 32, 32), white());
    ax_ir_pop_layer(&e.b);
    /* Nested: a rounded clip inside a translated rect clip; only their intersection shows. */
    d = ax_ir_layer_desc_default();
    d.transform = ax_affine2_translate(32, 0);
    d.clip.kind = AX_IR_CLIP_RECT;
    d.clip.rect = ax_rect_make(0, 0, 16, 32);
    ax_ir_push_layer(&e.b, &d);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
    d.clip.rect = ax_rect_make(4, 4, 24, 24);
    memcpy(d.clip.radii, radii, sizeof radii);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 32, 32), white());
    ax_ir_pop_layer(&e.b);
    ax_ir_pop_layer(&e.b);
    got = axt_soft_draw(e.r, finish(&e), 64, 32, 1.0f, NULL);
    AX_REQUIRE(got != NULL);
    AX_CHECK_EQ_U(px(got, 64, 1, 10)[0], 0);    /* outside the rect clip */
    AX_CHECK_EQ_U(px(got, 64, 5, 10)[0], 255);  /* inside */
    CHECK_NEAR_U8(px(got, 64, 12, 10)[0], 128); /* half covered, blended in sRGB space */
    AX_CHECK_EQ_U(px(got, 64, 13, 10)[0], 0);
    AX_CHECK_EQ_U(px(got, 64, 32 + 10, 16)[0], 255); /* inside both */
    AX_CHECK_EQ_U(px(got, 64, 32 + 20, 16)[0], 0);   /* outside the outer rect clip */
    AX_CHECK_EQ_U(px(got, 64, 32 + 4, 4)[0], 0);     /* cut by the rounded corner */
    AX_CHECK_EQ_U(px(got, 64, 32 + 2, 16)[0], 0);    /* outside the rounded clip */
    free(got);
    env_deinit(&e);
}

AX_TEST(group_opacity_composites_isolated_groups)
{
    env e;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_render_counts counts;
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 48, 16, 0, ax_color_make(0, 0, 0, 1));
    d.opacity = 0.5f;
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(&e.b, ax_rect_make(8, 0, 16, 16), ax_color_from_srgb8(0xFF0000FFu));
    ax_ir_pop_layer(&e.b);
    /* One item and no child: the shortcut (the item's color times the opacity) is the same. */
    d.transform = ax_affine2_translate(32, 0);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0xFF0000FFu));
    ax_ir_pop_layer(&e.b);
    got = axt_soft_draw(e.r, finish(&e), 48, 16, 1.0f, &counts);
    AX_REQUIRE(got != NULL);
    AX_CHECK_EQ_U(counts.approximated, 0);
    CHECK_NEAR_U8(px(got, 48, 4, 8)[0], 128);
    /* The overlap shows the blue only, at 50%: no red through it, as a group. */
    CHECK_PX(got, 48, 12, 8, 0, 0, px(got, 48, 20, 8)[2]);
    CHECK_NEAR_U8(px(got, 48, 20, 8)[2], 128);
    CHECK_PX(got, 48, 36, 8, 0, 0, px(got, 48, 20, 8)[2]);
    free(got);
    env_deinit(&e);
}

AX_TEST(a_snapshot_is_never_stretched)
{
    env e;
    uint8_t *got;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 16, 16, 0, ax_color_from_srgb8(0xFF336699u));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), white());
    /* A bigger target: drawn at its scale, top-left; the background fills the rest. */
    got = axt_soft_draw(e.r, finish(&e), 40, 24, 1.0f, NULL);
    AX_REQUIRE(got != NULL);
    CHECK_PX(got, 40, 15, 15, 255, 255, 255);
    CHECK_PX(got, 40, 16, 15, 0x33, 0x66, 0x99);
    CHECK_PX(got, 40, 39, 23, 0x33, 0x66, 0x99);
    free(got);
    /* At scale 2 the same snapshot covers 32 x 32 pixels. */
    ax_ir_builder_begin(&e.b, 16, 16, 0, ax_color_from_srgb8(0xFF336699u));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), white());
    got = axt_soft_draw(e.r, finish(&e), 40, 40, 2.0f, NULL);
    AX_REQUIRE(got != NULL);
    CHECK_PX(got, 40, 31, 31, 255, 255, 255);
    CHECK_PX(got, 40, 32, 31, 0x33, 0x66, 0x99);
    free(got);
    env_deinit(&e);
}

AX_TEST(formats_strides_and_the_background_without_a_snapshot)
{
    env e;
    uint8_t buf[2][8 * 3 * 4 + 64];
    ax_soft_target t;
    const ax_render_ir *ir;
    env_init(&e, 0);
    ax_ir_builder_begin(&e.b, 8, 3, 0, ax_color_from_srgb8(0xFF102030u));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 4, 3), ax_color_from_srgb8(0xFFC08040u));
    ir = finish(&e);
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.width = 8;
    t.height = 3;
    t.stride = 8 * 4 + 16; /* padded rows */
    t.scale = 1.0f;
    memset(buf, 0xEE, sizeof buf);
    t.pixels = buf[0];
    t.format = AX_SOFT_RGBA8;
    AX_REQUIRE(ax_render_soft_draw_ir(e.r, &t, ir, NULL) == AX_OK);
    t.pixels = buf[1];
    t.format = AX_SOFT_BGRA8;
    AX_REQUIRE(ax_render_soft_draw_ir(e.r, &t, ir, NULL) == AX_OK);
    AX_CHECK_EQ_U(buf[0][0], 0xC0);
    AX_CHECK_EQ_U(buf[0][1], 0x80);
    AX_CHECK_EQ_U(buf[0][2], 0x40);
    AX_CHECK_EQ_U(buf[1][0], 0x40);
    AX_CHECK_EQ_U(buf[1][2], 0xC0);
    AX_CHECK_EQ_U(buf[0][2 * 48 + 7 * 4 + 0], 0x10); /* last pixel of the last row */
    AX_CHECK_EQ_U(buf[0][8 * 4], 0xEE);              /* the padding is left alone */
    /* No snapshot: the target's background. */
    t.pixels = buf[0];
    t.format = AX_SOFT_RGBA8;
    t.background = ax_color_from_srgb8(0xFF00FF00u);
    AX_REQUIRE(ax_render_soft_draw_ir(e.r, &t, NULL, NULL) == AX_OK);
    AX_CHECK_EQ_U(buf[0][0], 0);
    AX_CHECK_EQ_U(buf[0][1], 255);
    env_deinit(&e);
}

AX_TEST(argument_errors)
{
    env e;
    ax_soft_target t;
    uint8_t pixels[16];
    ax_render_ir bad;
    const ax_render_ir *ir;
    env_init(&e, 0);
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = pixels;
    t.width = 2;
    t.height = 2;
    t.stride = 8;
    t.scale = 1.0f;
    t.format = 7;
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, &t, NULL, NULL), AX_ERR_INVALID_ARGUMENT);
    t.format = AX_SOFT_RGBA8;
    t.stride = 4;
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, &t, NULL, NULL), AX_ERR_INVALID_ARGUMENT);
    t.stride = 8;
    t.scale = 0.0f;
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, &t, NULL, NULL), AX_ERR_INVALID_ARGUMENT);
    t.scale = 1.0f;
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, NULL, NULL, NULL), AX_ERR_INVALID_ARGUMENT);
    ax_ir_builder_begin(&e.b, 2, 2, 0, ax_color_make(0, 0, 0, 1));
    ir = finish(&e);
    bad = *ir;
    bad.version_major = 2;
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, &t, &bad, NULL), AX_ERR_ABI_MISMATCH);
    AX_CHECK_RESULT(ax_render_soft_draw_ir(e.r, &t, ir, NULL), AX_OK);
    env_deinit(&e);
}
