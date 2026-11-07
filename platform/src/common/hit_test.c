/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "hit_test.h"

uint32_t ax_hit_test(const ax_hit_region *regions, uint32_t count, float x, float y)
{
    uint32_t i = count;
    while (i > 0) {
        const ax_hit_region *r = &regions[--i];
        if (x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height) {
            return r->region;
        }
    }
    return AX_REGION_CLIENT;
}

uint32_t ax_hit_region_edges(uint32_t region)
{
    switch (region) {
    case AX_REGION_RESIZE_TOP:
        return AX_EDGE_TOP;
    case AX_REGION_RESIZE_BOTTOM:
        return AX_EDGE_BOTTOM;
    case AX_REGION_RESIZE_LEFT:
        return AX_EDGE_LEFT;
    case AX_REGION_RESIZE_RIGHT:
        return AX_EDGE_RIGHT;
    case AX_REGION_RESIZE_TOP_LEFT:
        return AX_EDGE_TOP | AX_EDGE_LEFT;
    case AX_REGION_RESIZE_TOP_RIGHT:
        return AX_EDGE_TOP | AX_EDGE_RIGHT;
    case AX_REGION_RESIZE_BOTTOM_LEFT:
        return AX_EDGE_BOTTOM | AX_EDGE_LEFT;
    case AX_REGION_RESIZE_BOTTOM_RIGHT:
        return AX_EDGE_BOTTOM | AX_EDGE_RIGHT;
    default:
        return 0;
    }
}

ax_cursor ax_hit_region_cursor(uint32_t region, bool resizable)
{
    if (!resizable) {
        return AX_CURSOR_DEFAULT;
    }
    switch (region) {
    case AX_REGION_RESIZE_TOP:
        return AX_CURSOR_RESIZE_N;
    case AX_REGION_RESIZE_BOTTOM:
        return AX_CURSOR_RESIZE_S;
    case AX_REGION_RESIZE_LEFT:
        return AX_CURSOR_RESIZE_W;
    case AX_REGION_RESIZE_RIGHT:
        return AX_CURSOR_RESIZE_E;
    case AX_REGION_RESIZE_TOP_LEFT:
        return AX_CURSOR_RESIZE_NW;
    case AX_REGION_RESIZE_TOP_RIGHT:
        return AX_CURSOR_RESIZE_NE;
    case AX_REGION_RESIZE_BOTTOM_LEFT:
        return AX_CURSOR_RESIZE_SW;
    case AX_REGION_RESIZE_BOTTOM_RIGHT:
        return AX_CURSOR_RESIZE_SE;
    default:
        return AX_CURSOR_DEFAULT;
    }
}
