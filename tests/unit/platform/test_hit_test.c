/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "common/hit_test.h"

/* A 400x300 window: 8 px resize border, 32 px caption, a close button in the caption. */
static const ax_hit_region frame[] = {
    {0, 0, 400, 32, AX_REGION_CAPTION},           {368, 0, 32, 32, AX_REGION_CLOSE},
    {0, 0, 400, 8, AX_REGION_RESIZE_TOP},         {0, 292, 400, 8, AX_REGION_RESIZE_BOTTOM},
    {0, 0, 8, 300, AX_REGION_RESIZE_LEFT},        {392, 0, 8, 300, AX_REGION_RESIZE_RIGHT},
    {0, 0, 8, 8, AX_REGION_RESIZE_TOP_LEFT},      {392, 0, 8, 8, AX_REGION_RESIZE_TOP_RIGHT},
    {0, 292, 8, 8, AX_REGION_RESIZE_BOTTOM_LEFT}, {392, 292, 8, 8, AX_REGION_RESIZE_BOTTOM_RIGHT},
};
#define FRAME_COUNT ((uint32_t)(sizeof frame / sizeof frame[0]))

AX_TEST(later_regions_win)
{
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 100, 20), AX_REGION_CAPTION);
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 380, 20), AX_REGION_CLOSE);
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 100, 4), AX_REGION_RESIZE_TOP);
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 2, 2), AX_REGION_RESIZE_TOP_LEFT);
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 398, 298), AX_REGION_RESIZE_BOTTOM_RIGHT);
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 396, 150), AX_REGION_RESIZE_RIGHT);
}

AX_TEST(uncovered_points_are_client)
{
    AX_CHECK_EQ_U(ax_hit_test(frame, FRAME_COUNT, 200, 150), AX_REGION_CLIENT);
    AX_CHECK_EQ_U(ax_hit_test(frame, 0, 2, 2), AX_REGION_CLIENT);
}

AX_TEST(edges_are_half_open)
{
    /* [x, x + width): x = 400 is outside the 400-wide caption. */
    AX_CHECK_EQ_U(ax_hit_test(frame, 1, 399.9f, 10), AX_REGION_CAPTION);
    AX_CHECK_EQ_U(ax_hit_test(frame, 1, 400.0f, 10), AX_REGION_CLIENT);
    AX_CHECK_EQ_U(ax_hit_test(frame, 1, 0.0f, 31.99f), AX_REGION_CAPTION);
    AX_CHECK_EQ_U(ax_hit_test(frame, 1, 0.0f, 32.0f), AX_REGION_CLIENT);
}

AX_TEST(resize_edges_and_cursors)
{
    AX_CHECK_EQ_U(ax_hit_region_edges(AX_REGION_RESIZE_TOP_LEFT), AX_EDGE_TOP | AX_EDGE_LEFT);
    AX_CHECK_EQ_U(ax_hit_region_edges(AX_REGION_RESIZE_BOTTOM_RIGHT),
                  AX_EDGE_BOTTOM | AX_EDGE_RIGHT);
    AX_CHECK_EQ_U(ax_hit_region_edges(AX_REGION_CAPTION), 0);
    AX_CHECK_EQ_U(ax_hit_region_edges(AX_REGION_CLIENT), 0);

    AX_CHECK_EQ_I(ax_hit_region_cursor(AX_REGION_RESIZE_LEFT, true), AX_CURSOR_RESIZE_W);
    AX_CHECK_EQ_I(ax_hit_region_cursor(AX_REGION_RESIZE_TOP_RIGHT, true), AX_CURSOR_RESIZE_NE);
    AX_CHECK_EQ_I(ax_hit_region_cursor(AX_REGION_RESIZE_LEFT, false), AX_CURSOR_DEFAULT);
    AX_CHECK_EQ_I(ax_hit_region_cursor(AX_REGION_CAPTION, true), AX_CURSOR_DEFAULT);
}
