/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window, the scene (--renderer=ir): what the scene thread records into the Render IR each
 * frame. Everything is in logical units, anchored top-left, timed by the predicted present time,
 * and chosen so that any antialiasing fault shows at a glance:
 *   - rounded rects with a different radius per corner;
 *   - a rounded rect turning, and a hairline fan turning (jagged edges under rotation);
 *   - a rect moving at a slow, fractional speed (one-pixel jumps);
 *   - rects 0.25, 0.5, 1 and 1.5 units thin, straight and rotated (breaking up, uneven weight);
 *   - the timed bar of the shm renderer along the bottom (a late or dropped frame is a hitch);
 *   - without server decorations, the title band and the resize borders drawn as rects over
 *     exactly the hit-test regions the app thread publishes.
 */
#include <math.h>

#include "axion/base/ax_math.h"
#include "m0_scene.h"

#define PI_F 3.14159265358979f

static ax_color c8(uint32_t argb)
{
    return ax_color_from_srgb8(argb);
}

static void push(ax_ir_builder *b, ax_affine2 t)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    d.transform = t;
    ax_ir_push_layer(b, &d);
}

static void decorations(ax_ir_builder *b, float w, float h)
{
    const ax_color band = c8(0xFF262A33u);
    const ax_color edge = c8(M0_COLOR_EDGE);
    /* The resize borders (left, right, bottom; the top one lies under the title band). */
    ax_ir_rect(b, ax_rect_make(0, 0, M0_BORDER, h), band);
    ax_ir_rect(b, ax_rect_make(w - M0_BORDER, 0, M0_BORDER, h), band);
    ax_ir_rect(b, ax_rect_make(0, h - M0_BORDER, w, M0_BORDER), band);
    /* The title band (the caption region), then the corners, which win in the hit-test map. */
    ax_ir_rect(b, ax_rect_make(0, 0, w, M0_CAPTION_HEIGHT), c8(M0_COLOR_CAPTION));
    ax_ir_rect(b, ax_rect_make(0, 0, M0_CORNER, M0_CORNER), c8(0xFF3A4152u));
    ax_ir_rect(b, ax_rect_make(w - M0_CORNER, 0, M0_CORNER, M0_CORNER), c8(0xFF3A4152u));
    ax_ir_rect(b, ax_rect_make(0, h - M0_CORNER, M0_CORNER, M0_CORNER), c8(0xFF3A4152u));
    ax_ir_rect(b, ax_rect_make(w - M0_CORNER, h - M0_CORNER, M0_CORNER, M0_CORNER),
               c8(0xFF3A4152u));
    /* A one-unit outline at the window edge: 1.325 px at that scale, never snapped. */
    ax_ir_rect(b, ax_rect_make(0, 0, w, 1), edge);
    ax_ir_rect(b, ax_rect_make(0, h - 1, w, 1), edge);
    ax_ir_rect(b, ax_rect_make(0, 0, 1, h), edge);
    ax_ir_rect(b, ax_rect_make(w - 1, 0, 1, h), edge);
}

void m0_scene_record(ax_ir_builder *b, float w, float h, double t, bool csd)
{
    static const float radii[4][4] = {{0.0f, 6.0f, 12.0f, 24.0f},
                                      {16.0f, 16.0f, 16.0f, 16.0f},
                                      {2.0f, 30.0f, 2.0f, 30.0f},
                                      {40.0f, 0.0f, 40.0f, 0.0f}};
    static const float thin[4] = {0.25f, 0.5f, 1.0f, 1.5f};
    const float top = (csd ? M0_CAPTION_HEIGHT : 0.0f) + 16.0f;
    const float left = (csd ? M0_BORDER : 0.0f) + 16.0f;
    const ax_color fg = c8(M0_COLOR_BAR);
    const ax_color accent = c8(M0_COLOR_EDGE);
    int i;

    /* Rounded rects, a different radius per corner. */
    for (i = 0; i < 4; ++i) {
        ax_ir_rounded_rect(b, ax_rect_make(left + (float)i * 96.0f, top, 80.0f, 56.0f), radii[i],
                           i % 2 == 0 ? fg : accent);
    }

    /* A rounded rect turning (45 degrees a second) and a translucent one over it. */
    push(b, ax_affine2_mul(ax_affine2_translate(left + 60.0f, top + 130.0f),
                           ax_affine2_rotate((float)fmod(t * PI_F / 4.0, 2.0 * PI_F))));
    {
        const float r[4] = {10.0f, 2.0f, 18.0f, 6.0f};
        ax_ir_rounded_rect(b, ax_rect_make(-40.0f, -26.0f, 80.0f, 52.0f), r, fg);
        ax_ir_rect(b, ax_rect_make(-20.0f, -40.0f, 40.0f, 80.0f),
                   ax_color_from_srgb(0.31f, 0.49f, 1.0f, 0.5f));
    }
    ax_ir_pop_layer(b);

    /* A hairline fan turning slowly: 0.5-unit lines every 15 degrees. */
    push(b, ax_affine2_mul(ax_affine2_translate(left + 190.0f, top + 130.0f),
                           ax_affine2_rotate((float)fmod(t * PI_F / 18.0, 2.0 * PI_F))));
    for (i = 0; i < 12; ++i) {
        push(b, ax_affine2_rotate((float)i * PI_F / 12.0f));
        ax_ir_rect(b, ax_rect_make(-50.0f, -0.25f, 100.0f, 0.5f), fg);
        ax_ir_pop_layer(b);
    }
    ax_ir_pop_layer(b);

    /* A rect moving at 7.3 units a second over 100 units and back: fractional positions. */
    {
        const double period = 2.0 * 100.0 / 7.3;
        const double phase = fmod(t, period) / period;
        const float x = (float)(phase < 0.5 ? phase * 200.0 : (1.0 - phase) * 200.0);
        const float r[4] = {4.0f, 4.0f, 4.0f, 4.0f};
        ax_ir_rect(b, ax_rect_make(left + 260.0f, top + 104.0f, 104.0f + 24.0f, 0.5f), accent);
        ax_ir_rounded_rect(
            b, ax_rect_make(left + 260.0f + x * 0.5f + 2.0f, top + 110.0f, 24.0f, 24.0f), r, fg);
        ax_ir_rect(b, ax_rect_make(left + 260.0f + x * 0.5f + 13.5f, top + 140.0f, 1.0f, 20.0f),
                   fg);
    }

    /* Thin rects: straight, then rotated by 3 degrees. */
    for (i = 0; i < 4; ++i) {
        const float x = left + 400.0f + (float)i * 14.0f;
        ax_ir_rect(b, ax_rect_make(x, top, thin[i], 120.0f), fg);
        ax_ir_rect(b, ax_rect_make(left + 380.0f, top + 140.0f + (float)i * 8.0f, 90.0f, thin[i]),
                   fg);
    }
    push(b, ax_affine2_mul(ax_affine2_translate(left + 480.0f, top + 60.0f),
                           ax_affine2_rotate(0.0523599f)));
    for (i = 0; i < 4; ++i) {
        ax_ir_rect(b, ax_rect_make((float)i * 14.0f, -60.0f, thin[i], 120.0f), fg);
    }
    ax_ir_pop_layer(b);

    /* The timed bar, as in the shm renderer, along the bottom. */
    {
        const double pos = fmod(t * M0_BAR_SPEED, M0_BAR_TRACK) - 24.0;
        const float bottom = h - (csd ? M0_BORDER : 0.0f);
        ax_ir_rect(b, ax_rect_make((float)pos, bottom - 40.0f, 24.0f, 40.0f), fg);
    }

    if (csd) {
        decorations(b, w, h);
    }
}
