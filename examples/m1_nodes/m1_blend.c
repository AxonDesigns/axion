/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "m1_blend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_math.h"
#include "m1_cards.h"

#define DARK_BG  0xFF1E2129u
#define DARK_FG  0xFFE8ECF4u
#define LIGHT_BG 0xFFEFF0F1u
#define LIGHT_FG 0xFF232629u
#define CAPTION  0xFF2D3240u

#define PANEL_W 440.0f
#define PANEL_H 560.0f

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* A node placed by its insets in its parent: x, y, w, h. */
static ax_node place(ax_txn *t, ax_node parent, float x, float y, float w, float h)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, n, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    if (w > 0.0f) {
        ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    }
    if (h > 0.0f) {
        ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
    }
    ax_txn_append_child(t, parent, n);
    return n;
}

static ax_node fill(ax_txn *t, ax_node parent, float x, float y, float w, float h, uint32_t argb)
{
    ax_node n = place(t, parent, x, y, w, h);
    ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR, m1_c8(argb));
    return n;
}

/* A rect `w` wide turned by `degrees` about its center (the transform's origin). Layout rounds
 * frames to whole pixels, so a line thinner than a pixel is a 10-unit box narrowed by its
 * transform, which is not rounded. */
static void turned(ax_txn *t, ax_node parent, float cx, float cy, float w, float h, float degrees,
                   uint32_t argb)
{
    ax_node n = fill(t, parent, cx - 5.0f, cy - h * 0.5f, 10.0f, h, argb);
    const float r = degrees * 3.14159265f / 180.0f;
    const float sx = w / 10.0f;
    ax_transform2d m; /* rotate(r) * scale(sx, 1) */
    m.a = cosf(r) * sx;
    m.b = sinf(r) * sx;
    m.c = -sinf(r);
    m.d = cosf(r);
    m.tx = 0.0f;
    m.ty = 0.0f;
    ax_txn_set_transform(t, n, AX_PROP_TRANSFORM, m);
}

static void text(ax_txn *t, ax_node parent, float x, float y, const char *s, float size,
                 uint32_t argb)
{
    ax_node n = place(t, parent, x, y, 0.0f, 0.0f);
    ax_txn_set_str(t, n, AX_PROP_TEXT, s, strlen(s));
    ax_txn_set_f32(t, n, AX_PROP_FONT_SIZE, size);
    ax_txn_set_color(t, n, AX_PROP_COLOR, m1_c8(argb));
}

/* One panel: its background, foreground, and the samples. */
static void panel(ax_txn *t, ax_node root, float x, float y, uint32_t bg, uint32_t fg,
                  const char *name)
{
    static const float sizes[5] = {11.0f, 13.0f, 16.0f, 20.0f, 24.0f};
    static const float widths[4] = {0.25f, 0.5f, 1.0f, 1.5f};
    const ax_node p = fill(t, root, x, y, PANEL_W, PANEL_H, bg);
    char line[96];
    int i;
    ax_txn_set_u32(t, p, AX_PROP_OVERFLOW, AX_OVERFLOW_HIDDEN);
    text(t, p, 16, 12, name, 13.0f, fg);

    /* The hairline fan: 0.5 units wide, every 6 degrees, crossing at one point. */
    for (i = 0; i < 30; ++i) {
        turned(t, p, 110, 140, 0.5f, 180.0f, (float)i * 6.0f, fg);
    }
    /* Rects rotated 3 degrees, 0.25 to 1.5 units wide (decision 58's roping). */
    for (i = 0; i < 4; ++i) {
        turned(t, p, 250.0f + (float)i * 24.0f, 140, widths[i], 190.0f, 3.0f, fg);
    }
    text(t, p, 236, 240, "0.25  0.5  1  1.5", 11.0f, fg);

    /* Text from 11 to 24 px. */
    for (i = 0; i < 5; ++i) {
        (void)snprintf(line, sizeof line, "%.0f px  The quick brown fox jumps", sizes[i]);
        text(t, p, 16, 270.0f + (float)i * 34.0f, line, sizes[i], fg);
    }

    /* Translucent swatches over black and white halves: 50 % white over black is 128 in CSS
     * (188 blended in linear light), 50 % black over white 128 too; Breeze blue at 25, 50, 75 %. */
    fill(t, p, 16, 450, 408, 40, 0xFF000000u);
    fill(t, p, 16, 490, 408, 40, 0xFFFFFFFFu);
    fill(t, p, 24, 456, 60, 68, 0x80FFFFFFu);
    fill(t, p, 92, 456, 60, 68, 0x80000000u);
    fill(t, p, 160, 456, 60, 68, 0x403DAEE9u);
    fill(t, p, 228, 456, 60, 68, 0x803DAEE9u);
    fill(t, p, 296, 456, 60, 68, 0xBF3DAEE9u);
    fill(t, p, 364, 456, 52, 68, 0x80FF0000u);
}

ax_result m1_blend_build(ax_world world, ax_node root, const char *option, bool csd,
                         uint64_t *out_seq)
{
    ax_txn *t = ax_txn_begin(world);
    char title[128];
    const float top = csd ? M1_CAPTION_HEIGHT : 0.0f;
    if (t == NULL) {
        return ax_last_error()->code;
    }
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, m1_c8(DARK_BG));
    if (csd) {
        (void)fill(t, root, 0, 0, 2.0f * PANEL_W + 48.0f, M1_CAPTION_HEIGHT, CAPTION);
    }
    (void)snprintf(title, sizeof title, "Blending: %s", option);
    text(t, root, 16, top + 10.0f, title, 16.0f, DARK_FG);
    panel(t, root, 16, top + 44.0f, DARK_BG, DARK_FG, "Light on dark");
    panel(t, root, 32.0f + PANEL_W, top + 44.0f, LIGHT_BG, LIGHT_FG, "Dark on light");
    ax_txn_set_animated(t, false);
    return ax_txn_commit(t, out_seq);
}
