/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a window's hit-test region map.
 *
 * Where the app draws its own frame (no server decorations), the map says what a pointer press
 * means: a caption drags the window, an edge or corner resizes it, anything else is content. The
 * scene will publish it every frame from the AX_PROP_WINDOW_REGION node property; until then
 * apps and examples call this directly.
 *
 * Publishing never blocks, and the main thread reads the newest map on pointer down without
 * waiting for any other thread (a triple buffer of fixed capacity). Regions are in logical
 * surface coordinates; where they overlap, the one later in the array wins; uncovered points are
 * AX_REGION_CLIENT.
 */
#ifndef AX_PLATFORM_HIT_REGIONS_H
#define AX_PLATFORM_HIT_REGIONS_H

#include <stdint.h>

#include "../ax_window.h"

AX_EXTERN_C_BEGIN

#define AX_HIT_REGIONS_MAX 64

typedef struct ax_hit_region {
    float x, y, width, height; /* logical units, surface-local */
    uint32_t region;           /* ax_window_region */
} ax_hit_region;

/* Replaces the window's map. count <= AX_HIT_REGIONS_MAX, else AX_ERR_UNSUPPORTED.
 * thread: any (one publishing thread per window) */
AX_API ax_result ax_window_set_hit_regions(ax_window window, const ax_hit_region *regions,
                                           uint32_t count);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_HIT_REGIONS_H */
