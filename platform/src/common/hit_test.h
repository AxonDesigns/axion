/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hit-test lookup over a window's region map. Pure functions, used by the main thread on
 * pointer events and tested without a windowing system.
 */
#ifndef AX_PLATFORM_HIT_TEST_H
#define AX_PLATFORM_HIT_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/platform/ax_hit_regions.h"

/* The region at (x, y): the last region in the array that contains the point, or
 * AX_REGION_CLIENT. A region contains [x, x + width) x [y, y + height). */
uint32_t ax_hit_test(const ax_hit_region *regions, uint32_t count, float x, float y);

/* Edges a resize region moves, as a bit set of AX_EDGE_*; 0 for regions that do not resize. */
#define AX_EDGE_TOP    1u
#define AX_EDGE_BOTTOM 2u
#define AX_EDGE_LEFT   4u
#define AX_EDGE_RIGHT  8u
uint32_t ax_hit_region_edges(uint32_t region);

/* Cursor a region should show. */
typedef enum ax_cursor {
    AX_CURSOR_DEFAULT = 0,
    AX_CURSOR_RESIZE_N,
    AX_CURSOR_RESIZE_S,
    AX_CURSOR_RESIZE_W,
    AX_CURSOR_RESIZE_E,
    AX_CURSOR_RESIZE_NW,
    AX_CURSOR_RESIZE_NE,
    AX_CURSOR_RESIZE_SW,
    AX_CURSOR_RESIZE_SE
} ax_cursor;
ax_cursor ax_hit_region_cursor(uint32_t region, bool resizable);

#endif /* AX_PLATFORM_HIT_TEST_H */
