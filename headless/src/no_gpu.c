/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Builds without the gpu module: the headless host draws with ax_render_soft only.
 */
#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "hl_internal.h"

ax_result ax__hl_gpu_probe(void)
{
    return ax_set_error(AX_ERR_UNSUPPORTED,
                        "ax_headless: this build has no GPU renderer (the gpu module is off)");
}

ax_result ax__hl_gpu_create(const ax_allocator *allocator, hl_gpu **out)
{
    (void)allocator;
    *out = NULL;
    return ax__hl_gpu_probe();
}

void ax__hl_gpu_destroy(hl_gpu *g)
{
    (void)g;
}

ax_result ax__hl_gpu_draw(hl_gpu *g, const ax_render_ir *ir, uint32_t w, uint32_t h, float scale,
                          ax_color background, uint8_t *rgba)
{
    (void)g;
    (void)ir;
    (void)w;
    (void)h;
    (void)scale;
    (void)background;
    (void)rgba;
    return ax__hl_gpu_probe();
}
