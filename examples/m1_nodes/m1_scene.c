/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "m1_scene.h"

#include <math.h>

#include "axion/base/ax_math.h"
#include "axion/core/ax_world_sdk.h"

#define PI_F 3.14159265358979f

/* ---- building the tree (app thread) ---------------------------------------------------------- */

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

ax_rgba m1_c8(uint32_t argb)
{
    return ax_rgba_from_srgb8((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                              (uint8_t)(argb >> 24));
}

static ax_transform2d rotation(float radians)
{
    ax_affine2 m = ax_affine2_rotate(radians);
    ax_transform2d t;
    t.a = m.a;
    t.b = m.b;
    t.c = m.c;
    t.d = m.d;
    t.tx = m.tx;
    t.ty = m.ty;
    return t;
}

/* A rect node under parent. */
static ax_node rect(ax_txn *t, ax_node parent, float x, float y, float w, float h, ax_rgba color)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, n, AX_PROP_POSITION, AX_POSITION_ABSOLUTE); /* placed by its insets */
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
    ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR, color);
    ax_txn_append_child(t, parent, n);
    return n;
}

static void radii(ax_txn *t, ax_node n, float tl, float tr, float br, float bl)
{
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_TOP_LEFT, tl);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_TOP_RIGHT, tr);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, br);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_BOTTOM_LEFT, bl);
}

/* m0_window's scene (examples/m0_window/scene.c) as nodes. Returns the first rounded rect, whose
 * color the app thread changes every second. */
ax_node m1_scene_build(ax_world world, ax_node root, m1_moving *m, float w, float h,
                       uint64_t *out_seq)
{
    static const float r4[4][4] = {{0.0f, 6.0f, 12.0f, 24.0f},
                                   {16.0f, 16.0f, 16.0f, 16.0f},
                                   {2.0f, 30.0f, 2.0f, 30.0f},
                                   {40.0f, 0.0f, 40.0f, 0.0f}};
    static const float thin[4] = {0.25f, 0.5f, 1.0f, 1.5f};
    const ax_rgba fg = m1_c8(COLOR_FG), accent = m1_c8(COLOR_EDGE), none = {0, 0, 0, 0};
    ax_txn *t = ax_txn_begin(world);
    ax_node first = AX_HANDLE_NULL_INIT, n, group;
    float left, top;
    int i;

    m->top = top = (m->csd ? CAPTION_HEIGHT : 0.0f) + 16.0f;
    m->left = left = (m->csd ? BORDER : 0.0f) + 16.0f;
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, m1_c8(COLOR_BG));

    /* Rounded rects, a different radius per corner. */
    for (i = 0; i < 4; ++i) {
        n = rect(t, root, left + (float)i * 96.0f, top, 80.0f, 56.0f, i % 2 == 0 ? fg : accent);
        radii(t, n, r4[i][0], r4[i][1], r4[i][2], r4[i][3]);
        if (i == 0) {
            first = n;
        }
    }

    /* A rounded rect turning about its center, with a translucent rect turning with it. */
    m->turning = rect(t, root, left + 60.0f - 40.0f, top + 130.0f - 26.0f, 80.0f, 52.0f, fg);
    radii(t, m->turning, 10.0f, 2.0f, 18.0f, 6.0f);
    (void)rect(t, m->turning, 20.0f, -14.0f, 40.0f, 80.0f,
               ax_rgba_from_srgb(0.31f, 0.49f, 1.0f, 0.5f));

    /* A hairline fan: a point at its center, turning, with 0.5-unit lines every 15 degrees. */
    m->fan = rect(t, root, left + 190.0f, top + 130.0f, 0.0f, 0.0f, none);
    for (i = 0; i < 12; ++i) {
        n = rect(t, m->fan, -50.0f, -0.25f, 100.0f, 0.5f, fg);
        ax_txn_set_transform(t, n, AX_PROP_TRANSFORM, rotation((float)i * PI_F / 12.0f));
    }

    /* A slow, fractional slider: a track line, a knob and its tick. */
    (void)rect(t, root, left + 260.0f, top + 104.0f, 104.0f + 24.0f, 0.5f, accent);
    m->knob = rect(t, root, left + 262.0f, top + 110.0f, 24.0f, 24.0f, fg);
    radii(t, m->knob, 4.0f, 4.0f, 4.0f, 4.0f);
    m->tick = rect(t, root, left + 273.5f, top + 140.0f, 1.0f, 20.0f, fg);

    /* Thin rects: straight, then a group turned by 3 degrees. */
    for (i = 0; i < 4; ++i) {
        (void)rect(t, root, left + 400.0f + (float)i * 14.0f, top, thin[i], 120.0f, fg);
        (void)rect(t, root, left + 380.0f, top + 140.0f + (float)i * 8.0f, 90.0f, thin[i], fg);
    }
    group = rect(t, root, left + 480.0f, top + 60.0f, 0.0f, 0.0f, none);
    ax_txn_set_transform(t, group, AX_PROP_TRANSFORM, rotation(0.0523599f));
    for (i = 0; i < 4; ++i) {
        (void)rect(t, group, (float)i * 14.0f, -60.0f, thin[i], 120.0f, fg);
    }

    /* The timed bar along the bottom (placed by the driver). */
    m->bar = rect(t, root, -24.0f, h - 40.0f, 24.0f, 40.0f, fg);

    /* Without server decorations: the title band, resize borders and corners, and an outline,
     * over exactly the hit regions; the driver follows the window's size. */
    if (m->csd) {
        const ax_rgba band = m1_c8(COLOR_BAND), corner = m1_c8(COLOR_CORNER);
        m->band_left = rect(t, root, 0, 0, BORDER, h, band);
        m->band_right = rect(t, root, w - BORDER, 0, BORDER, h, band);
        m->band_bottom = rect(t, root, 0, h - BORDER, w, BORDER, band);
        m->caption = rect(t, root, 0, 0, w, CAPTION_HEIGHT, m1_c8(COLOR_CAPTION));
        (void)rect(t, root, 0, 0, CORNER, CORNER, corner);
        m->corner_tr = rect(t, root, w - CORNER, 0, CORNER, CORNER, corner);
        m->corner_bl = rect(t, root, 0, h - CORNER, CORNER, CORNER, corner);
        m->corner_br = rect(t, root, w - CORNER, h - CORNER, CORNER, CORNER, corner);
        m->edge_top = rect(t, root, 0, 0, w, 1, accent);
        m->edge_bottom = rect(t, root, 0, h - 1, w, 1, accent);
        m->edge_left = rect(t, root, 0, 0, 1, h, accent);
        m->edge_right = rect(t, root, w - 1, 0, 1, h, accent);
    }
    if (ax_txn_commit(t, out_seq) != AX_OK) {
        first.h = 0;
    }
    ax_atomic_store_u32(&m->ready, 1, AX_MO_RELEASE);
    return first;
}

/* ---- the scene driver (scene thread; example only, until animations) ------------------------- */

static void set_dim(ax_scene *scene, ax_node n, ax_prop p, float v)
{
    ax_value value;
    value.dimension = pt(v);
    (void)ax_scene_set(scene, n, p, AX_VALUE_DIMENSION, &value);
}

static void set_box(ax_scene *scene, ax_node n, float x, float y, float w, float h)
{
    set_dim(scene, n, AX_PROP_LEFT, x);
    set_dim(scene, n, AX_PROP_TOP, y);
    set_dim(scene, n, AX_PROP_WIDTH, w);
    set_dim(scene, n, AX_PROP_HEIGHT, h);
}

static void set_rotation(ax_scene *scene, ax_node n, float radians)
{
    ax_value value;
    value.transform2d = ax_affine2_rotate(radians);
    (void)ax_scene_set(scene, n, AX_PROP_TRANSFORM, AX_VALUE_TRANSFORM2D, &value);
}

void m1_scene_slide(void *user, ax_world world, ax_node root, uint64_t time_ns, float w, float h)
{
    m1_moving *m = (m1_moving *)user;
    ax_scene *scene = ax_world_scene(world);
    const double t = (double)time_ns / 1e9; /* the predicted present time, as in m0_window */
    (void)root;
    if (ax_atomic_load_u32(&m->ready, AX_MO_ACQUIRE) == 0) {
        return;
    }
    set_rotation(scene, m->turning, (float)fmod(t * PI_F / 4.0, 2.0 * PI_F));
    set_rotation(scene, m->fan, (float)fmod(t * PI_F / 18.0, 2.0 * PI_F));
    {
        /* 7.3 units a second over 100 units and back. */
        const double period = 2.0 * 100.0 / 7.3;
        const double phase = fmod(t, period) / period;
        const float x = (float)(phase < 0.5 ? phase * 200.0 : (1.0 - phase) * 200.0);
        set_dim(scene, m->knob, AX_PROP_LEFT, m->left + 260.0f + x * 0.5f + 2.0f);
        set_dim(scene, m->tick, AX_PROP_LEFT, m->left + 260.0f + x * 0.5f + 13.5f);
    }
    {
        const float bottom = h - (m->csd ? BORDER : 0.0f);
        set_dim(scene, m->bar, AX_PROP_LEFT, (float)(fmod(t * BAR_SPEED, BAR_TRACK) - 24.0));
        set_dim(scene, m->bar, AX_PROP_TOP, bottom - 40.0f);
    }
    if (m->csd) { /* setting what did not change marks nothing */
        set_box(scene, m->band_left, 0, 0, BORDER, h);
        set_box(scene, m->band_right, w - BORDER, 0, BORDER, h);
        set_box(scene, m->band_bottom, 0, h - BORDER, w, BORDER);
        set_box(scene, m->caption, 0, 0, w, CAPTION_HEIGHT);
        set_box(scene, m->corner_tr, w - CORNER, 0, CORNER, CORNER);
        set_box(scene, m->corner_bl, 0, h - CORNER, CORNER, CORNER);
        set_box(scene, m->corner_br, w - CORNER, h - CORNER, CORNER, CORNER);
        set_box(scene, m->edge_top, 0, 0, w, 1);
        set_box(scene, m->edge_bottom, 0, h - 1, w, 1);
        set_box(scene, m->edge_left, 0, 0, 1, h);
        set_box(scene, m->edge_right, w - 1, 0, 1, h);
    }
}
