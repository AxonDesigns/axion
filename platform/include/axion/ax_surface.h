/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: surfaces, anything that can be rendered into and receive input.
 *
 * Every window has exactly one surface. Later, host textures and offscreen targets are surfaces
 * too, so the core never needs to know which kind it draws into. Presenting into a surface is
 * done by the renderer through the internal SDK (axion/platform/ax_present.h).
 */
#ifndef AX_SURFACE_H
#define AX_SURFACE_H

#include <stdint.h>

#include "ax_window.h"
#include <axion/ax_config.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

typedef struct ax_surface_info {
    uint32_t struct_size; /* set by the caller: sizeof(ax_surface_info) */
    uint32_t pixel_width; /* size of a full-surface buffer in device pixels */
    uint32_t pixel_height;
    float logical_width; /* size in logical units */
    float logical_height;
    float scale; /* device pixels per logical unit; may be fractional */
} ax_surface_info;

/* The most recently configured size and scale (not necessarily presented yet). thread: any */
AX_API ax_result ax_surface_get_info(ax_surface surface, ax_surface_info *out_info);

AX_EXTERN_C_END

#endif /* AX_SURFACE_H */
