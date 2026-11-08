/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "axt_scenes.h"

#include <string.h>

#include "axion/ax_base.h"
#include "axion/base/ax_compiler.h"

#define PI_F 3.14159265358979f

static ax_color white(void)
{
    return ax_color_make(1, 1, 1, 1);
}

void axt_rotated_rect(ax_ir_builder *b, float degrees, float x, float y, float w, float h,
                      ax_color c)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    d.transform =
        ax_affine2_mul(ax_affine2_translate(x, y), ax_affine2_rotate(degrees * PI_F / 180.0f));
    ax_ir_push_layer(b, &d);
    ax_ir_rect(b, ax_rect_make(-w * 0.5f, -h * 0.5f, w, h), c);
    ax_ir_pop_layer(b);
}

/* ---- the coverage scenes (decision 56) ------------------------------------------------------- */

static void axis_aligned(ax_ir_builder *b)
{
    int i;
    for (i = 0; i < 8; ++i) {
        const float f = (float)i / 8.0f;
        ax_ir_rect(b, ax_rect_make(2.0f + (float)i * 11.0f + f, 3.0f + f, 7.5f + f, 20.25f),
                   white());
        ax_ir_rect(b, ax_rect_make(2.0f + (float)i * 11.0f, 30.0f + f * 0.5f, 6.0f + f, 3.3f),
                   ax_color_from_srgb8(0xFF4F7CFFu));
    }
}

static void thin(ax_ir_builder *b)
{
    static const float widths[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f};
    size_t i;
    for (i = 0; i < AX_COUNTOF(widths); ++i) {
        const float x = 4.0f + (float)i * 12.0f + 0.3f;
        ax_ir_rect(b, ax_rect_make(x, 4.0f, widths[i], 26.0f), white());
        ax_ir_rect(b, ax_rect_make(4.0f, 36.0f + (float)i * 4.0f + 0.6f, 70.0f, widths[i]),
                   white());
    }
}

static void rotated(ax_ir_builder *b)
{
    axt_rotated_rect(b, 7.0f, 22.3f, 24.6f, 30.0f, 18.0f, white());
    axt_rotated_rect(b, 30.0f, 64.0f, 24.0f, 28.0f, 20.0f, white());
    axt_rotated_rect(b, 45.0f, 104.5f, 24.5f, 26.0f, 26.0f, white());
    axt_rotated_rect(b, 30.0f, 32.0f, 70.0f, 50.0f, 0.5f, white());
    axt_rotated_rect(b, -60.0f, 80.0f, 70.0f, 40.0f, 1.25f, white());
    axt_rotated_rect(b, 15.0f, 112.0f, 72.0f, 12.0f, 30.0f, ax_color_from_srgb8(0xFFE8ECF4u));
}

static void rounded(ax_ir_builder *b)
{
    const float r1[4] = {0.0f, 4.0f, 10.0f, 16.0f};
    const float r2[4] = {12.0f, 12.0f, 12.0f, 12.0f};
    const float pill[4] = {100.0f, 100.0f, 100.0f, 100.0f};
    const float tiny[4] = {0.5f, 1.0f, 1.5f, 2.0f};
    ax_ir_rounded_rect(b, ax_rect_make(4.25f, 4.5f, 52.0f, 36.0f), r1, white());
    ax_ir_rounded_rect(b, ax_rect_make(64.6f, 4.2f, 30.0f, 30.0f), r2,
                       ax_color_from_srgb8(0xFF4F7CFFu));
    ax_ir_rounded_rect(b, ax_rect_make(6.4f, 48.3f, 70.0f, 17.5f), pill, white());
    ax_ir_rounded_rect(b, ax_rect_make(100.5f, 10.5f, 6.0f, 50.0f), tiny, white());
    ax_ir_rounded_rect(b, ax_rect_make(84.0f, 46.0f, 36.0f, 28.0f), r1,
                       ax_color_from_srgb8(0xFFE8ECF4u));
}

static void nested(ax_ir_builder *b)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float r[4] = {3.0f, 6.0f, 0.0f, 9.0f};
    ax_ir_rect(b, ax_rect_make(2.5f, 2.5f, 30.3f, 0.75f), white());
    d.transform = ax_affine2_translate(10.4f, 8.2f);
    ax_ir_push_layer(b, &d);
    ax_ir_rounded_rect(b, ax_rect_make(0, 0, 28.0f, 18.0f), r, ax_color_from_srgb8(0xFF4F7CFFu));
    d.transform =
        ax_affine2_mul(ax_affine2_translate(48.0f, 6.0f),
                       ax_affine2_mul(ax_affine2_rotate(0.4f), ax_affine2_scale(1.5f, 0.75f)));
    ax_ir_push_layer(b, &d);
    ax_ir_rounded_rect(b, ax_rect_make(-8.0f, -6.0f, 16.0f, 12.0f), r, white());
    ax_ir_pop_layer(b);
    ax_ir_pop_layer(b);
}

static void blending(ax_ir_builder *b)
{
    ax_ir_rect(b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
}

/* ---- torture scenes -------------------------------------------------------------------------- */

/* Rotating hairlines: a fan of 24 lines 0.25 to 1.5 units wide, each frame turned 1.7 degrees
 * further, light on dark and dark on light. */
static void hairlines(ax_ir_builder *b, uint32_t frame)
{
    static const float widths[4] = {0.25f, 0.5f, 1.0f, 1.5f};
    int i;
    ax_ir_rect(b, ax_rect_make(80, 0, 80, 160), ax_color_from_srgb8(0xFFE8ECF4u));
    for (i = 0; i < 24; ++i) {
        const float deg = (float)i * 15.0f + (float)frame * 1.7f;
        const float cx = i % 2 == 0 ? 40.0f : 120.0f;
        const ax_color c =
            i % 2 == 0 ? ax_color_from_srgb8(0xFFE8ECF4u) : ax_color_from_srgb8(0xFF1E2129u);
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        d.transform =
            ax_affine2_mul(ax_affine2_translate(cx, 80.0f), ax_affine2_rotate(deg * PI_F / 180.0f));
        ax_ir_push_layer(b, &d);
        ax_ir_rect(b, ax_rect_make(6.0f, -widths[i % 4] * 0.5f, 68.0f, widths[i % 4]), c);
        ax_ir_pop_layer(b);
    }
}

static void hairlines_0(ax_ir_builder *b)
{
    hairlines(b, 0);
}
static void hairlines_1(ax_ir_builder *b)
{
    hairlines(b, 1);
}
static void hairlines_2(ax_ir_builder *b)
{
    hairlines(b, 2);
}
static void hairlines_3(ax_ir_builder *b)
{
    hairlines(b, 3);
}

/* A zone plate: cells of one unit whose grey is 0.5 + 0.5 cos(k r^2), up to the cells' own
 * frequency at the corners, drawn at scale 1.5 so cell edges fall inside pixels. */
static void zone_plate_cells(ax_ir_builder *b, float size)
{
    const float k = PI_F / size; /* the phase step reaches pi per cell at the far edge */
    int x, y;
    for (y = 0; y < (int)size; ++y) {
        for (x = 0; x < (int)size; ++x) {
            const float fx = (float)x + 0.5f - size * 0.5f;
            const float fy = (float)y + 0.5f - size * 0.5f;
            const float v = 0.5f + 0.5f * ax_affine2_rotate(k * (fx * fx + fy * fy)).a;
            ax_ir_rect(b, ax_rect_make((float)x, (float)y, 1.0f, 1.0f),
                       ax_color_from_srgb(v, v, v, 1.0f));
        }
    }
}

static void zone_plate(ax_ir_builder *b)
{
    zone_plate_cells(b, 96.0f);
}

/* The zone plate behind three nested rounded clips, the middle one turned 9 degrees. */
static void zone_plate_clipped(ax_ir_builder *b)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float r_outer[4] = {20.0f, 20.0f, 20.0f, 20.0f};
    const float r_mid[4] = {6.0f, 18.0f, 6.0f, 18.0f};
    const float r_inner[4] = {30.0f, 30.0f, 30.0f, 30.0f};
    ax_ir_rect(b, ax_rect_make(0, 0, 96, 96), ax_color_from_srgb8(0xFF4F7CFFu));
    d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
    d.clip.rect = ax_rect_make(4.3f, 4.3f, 87.4f, 87.4f);
    memcpy(d.clip.radii, r_outer, sizeof r_outer);
    ax_ir_push_layer(b, &d);
    zone_plate_cells(b, 96.0f);
    d = ax_ir_layer_desc_default();
    d.transform = ax_affine2_mul(ax_affine2_translate(48.0f, 48.0f),
                                 ax_affine2_mul(ax_affine2_rotate(9.0f * PI_F / 180.0f),
                                                ax_affine2_translate(-48.0f, -48.0f)));
    d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
    d.clip.rect = ax_rect_make(14.5f, 20.25f, 67.0f, 55.5f);
    memcpy(d.clip.radii, r_mid, sizeof r_mid);
    ax_ir_push_layer(b, &d);
    ax_ir_rect(b, ax_rect_make(0, 0, 96, 96), ax_color_from_srgb8(0xFF1E2129u));
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
    d.clip.rect = ax_rect_make(24.0f, 28.0f, 48.0f, 40.0f);
    memcpy(d.clip.radii, r_inner, sizeof r_inner);
    ax_ir_push_layer(b, &d);
    zone_plate_cells(b, 96.0f);
    ax_ir_pop_layer(b);
    ax_ir_pop_layer(b);
    ax_ir_pop_layer(b);
}

/* ---- what only the software renderer draws exactly ------------------------------------------- */

/* Group opacity: overlapping items in a 50% layer show no overlap; a nested group; a layer with
 * one item takes the shortcut. */
static void group_opacity(ax_ir_builder *b)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    ax_ir_rect(b, ax_rect_make(0, 0, 96, 24), ax_color_from_srgb8(0xFFE8ECF4u));
    d.opacity = 0.5f;
    ax_ir_push_layer(b, &d);
    ax_ir_rect(b, ax_rect_make(4.5f, 4.5f, 30, 30), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(b, ax_rect_make(20.5f, 14.5f, 30, 30), ax_color_from_srgb8(0xFF0000FFu));
    d.opacity = 0.5f;
    d.transform = ax_affine2_translate(56.0f, 4.0f);
    ax_ir_push_layer(b, &d);
    ax_ir_rect(b, ax_rect_make(0, 0, 20, 20), ax_color_from_srgb8(0xFF00A000u));
    ax_ir_rect(b, ax_rect_make(10, 10, 20, 20), ax_color_from_srgb8(0xFF00A000u));
    ax_ir_pop_layer(b);
    ax_ir_pop_layer(b);
    d = ax_ir_layer_desc_default();
    d.opacity = 0.25f;
    ax_ir_push_layer(b, &d);
    axt_rotated_rect(b, 20.0f, 30.0f, 40.0f, 30.0f, 6.0f, ax_color_from_srgb8(0xFF000000u));
    ax_ir_pop_layer(b);
}

/* A layer blending in linear light beside the same content in sRGB space. */
static void linear_layer(ax_ir_builder *b)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        d.flags = pass == 1 ? AX_IR_LAYER_LINEAR_BLEND : 0u;
        d.transform = ax_affine2_translate((float)pass * 48.0f, 0.0f);
        ax_ir_push_layer(b, &d);
        ax_ir_rect(b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
        ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
        ax_ir_rect(b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
        axt_rotated_rect(b, 3.0f, 20.0f, 24.0f, 36.0f, 0.66f, ax_color_from_srgb8(0xFFE8ECF4u));
        ax_ir_pop_layer(b);
    }
}

static const axt_scene k_scenes[] = {
    {"axis_aligned", 96, 64, 96, 64, 1.0f, 0, axis_aligned},
    {"thin", 80, 64, 80, 64, 1.0f, 0, thin},
    {"rotated", 128, 96, 128, 96, 1.0f, 0, rotated},
    {"rounded", 128, 80, 128, 80, 1.0f, 0, rounded},
    {"scale_1_325", 80, 40, 106, 53, 1.325f, 0, nested},
    {"scale_2", 80, 40, 160, 80, 2.0f, 0, nested},
    {"blending", 32, 16, 32, 16, 1.0f, 0, blending},
    {"hairlines_0", 160, 160, 160, 160, 1.0f, AXT_SCENE_TORTURE, hairlines_0},
    {"hairlines_1", 160, 160, 160, 160, 1.0f, AXT_SCENE_TORTURE, hairlines_1},
    {"hairlines_2", 160, 160, 160, 160, 1.0f, AXT_SCENE_TORTURE, hairlines_2},
    {"hairlines_3", 160, 160, 160, 160, 1.0f, AXT_SCENE_TORTURE, hairlines_3},
    {"zone_plate", 96, 96, 144, 144, 1.5f, AXT_SCENE_TORTURE, zone_plate},
    {"zone_plate_clipped", 96, 96, 144, 144, 1.5f, AXT_SCENE_TORTURE | AXT_SCENE_CLIPS,
     zone_plate_clipped},
    {"group_opacity", 96, 48, 192, 96, 2.0f, AXT_SCENE_GROUPS, group_opacity},
    {"linear_layer", 96, 40, 96, 40, 1.0f, AXT_SCENE_LINEAR, linear_layer},
};

const axt_scene *axt_scenes(uint32_t *count)
{
    *count = (uint32_t)AX_COUNTOF(k_scenes);
    return k_scenes;
}

const axt_scene *axt_scene_find(const char *name)
{
    size_t i;
    for (i = 0; i < AX_COUNTOF(k_scenes); ++i) {
        if (strcmp(k_scenes[i].name, name) == 0) {
            return &k_scenes[i];
        }
    }
    return NULL;
}

const ax_render_ir *axt_scene_record(const axt_scene *s, ax_ir_builder *b)
{
    const ax_render_ir *ir = NULL;
    ax_color bg = ax_color_from_srgb8(0xFF1E2129u);
    if (strcmp(s->name, "axis_aligned") == 0 || strcmp(s->name, "thin") == 0 ||
        strcmp(s->name, "rotated") == 0 || strcmp(s->name, "blending") == 0 ||
        strcmp(s->name, "linear_layer") == 0) {
        bg = ax_color_make(0, 0, 0, 1);
    }
    ax_ir_builder_begin(b, s->logical_width, s->logical_height, 0, bg);
    s->build(b);
    if (ax_ir_builder_end(b, &ir) != AX_OK) {
        return NULL;
    }
    return ir;
}
