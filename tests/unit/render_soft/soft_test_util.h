/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helpers for ax_render_soft's tests: draw a snapshot into a fresh RGBA buffer.
 */
#ifndef AXT_SOFT_TEST_UTIL_H
#define AXT_SOFT_TEST_UTIL_H

#include <stdlib.h>
#include <string.h>

#include "axion/render/ax_render_soft.h"

/* Draws ir (w x h pixels at scale) with r; NULL on failure. The caller frees the pixels. */
static inline uint8_t *axt_soft_draw(ax_render_soft *r, const ax_render_ir *ir, uint32_t w,
                                     uint32_t h, float scale, ax_render_counts *counts)
{
    ax_soft_target t;
    uint8_t *pixels = (uint8_t *)malloc((size_t)w * h * 4u);
    if (pixels == NULL) {
        return NULL;
    }
    memset(pixels, 0xCD, (size_t)w * h * 4u); /* every byte must be written */
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = pixels;
    t.width = w;
    t.height = h;
    t.stride = (size_t)w * 4u;
    t.format = AX_SOFT_RGBA8;
    t.scale = scale;
    t.background = ax_color_make(0, 0, 0, 1);
    if (ax_render_soft_draw_ir(r, &t, ir, counts) != AX_OK) {
        free(pixels);
        return NULL;
    }
    return pixels;
}

#endif
