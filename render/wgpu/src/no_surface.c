/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu without the platform module (offscreen only): no window surfaces to release.
 */
#include "rw_internal.h"

void ax__render_wgpu_destroy_surfaces(ax_render_wgpu *r)
{
    (void)r;
}
