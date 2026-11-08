/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Goldens of animation frames (decisions 82, 94 and 99): m1_nodes' cards, headless with the manual
 * clock and ax_render_soft, captured mid-way through a reorder (the default spring moving every
 * card), a card growing (the resize preset behind its rounded clip), a color easing and the list
 * narrowing (its paragraphs re-wrapped at each width), and at rest. The software renderer gives the same bytes on every platform (decision 86): these frames
 * check that animation sampling is deterministic too.
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/headless/ax_headless.h"
#include "axt_golden.h"
#include "m1_cards.h"

static ax_headless *start(ax_node *root, m1_cards *c)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    d.renderer = AX_HEADLESS_RENDER_SOFT;
    s.width = 320.0f;
    s.height = 420.0f;
    s.scale = 1.0f;
    if (ax_headless_create(&d, &h) != AX_OK || ax_headless_add_surface(h, &s, root) != AX_OK ||
        m1_cards_build(ax_headless_world(h), *root, c, false, NULL) != AX_OK ||
        ax_headless_settle(h, 100, NULL) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "headless setup: %s", ax_last_error()->message.ptr);
        if (h != NULL) {
            ax_headless_destroy(h);
        }
        return NULL;
    }
    return h;
}

/* Steps `frames` frames, then checks the frame against its golden. */
static void frame_golden(ax_headless *h, ax_node root, uint32_t frames, const char *name)
{
    ax_headless_image img;
    AX_REQUIRE(ax_headless_step(h, frames) == AX_OK);
    AX_REQUIRE(ax_headless_capture(h, root, &img) == AX_OK);
    AXT_CHECK_GOLDEN("anim", name, img.rgba, img.width, img.height, img.stride);
    ax_headless_image_free(h, &img);
}

AX_TEST(cards_reorder_grow_and_change_color_frame_by_frame)
{
    static m1_cards c;
    ax_node root;
    ax_headless *h = start(&root, &c);
    uint32_t frames = 0;
    if (h == NULL) {
        return;
    }
    frame_golden(h, root, 1, "cards_rest"); /* the first content, at rest */

    /* The last card to the top: every card moves, with the default spring. */
    AX_REQUIRE(m1_cards_step(ax_headless_world(h), &c, 0, NULL) == AX_OK);
    frame_golden(h, root, 2, "cards_reorder_f02");
    frame_golden(h, root, 4, "cards_reorder_f06");
    frame_golden(h, root, 6, "cards_reorder_f12");
    AX_REQUIRE(ax_headless_settle(h, 200, &frames) == AX_OK);
    AX_CHECK(frames > 6 && frames < 60); /* the 0.4 s spring came to rest */

    /* A card grows: its content laid out at each interpolated height, behind its rounded clip. */
    AX_REQUIRE(m1_cards_step(ax_headless_world(h), &c, 1, NULL) == AX_OK);
    frame_golden(h, root, 3, "cards_grow_f03");
    frame_golden(h, root, 6, "cards_grow_f09");
    AX_REQUIRE(ax_headless_settle(h, 200, NULL) == AX_OK);

    /* A card's color eases over 0.2 s. */
    AX_REQUIRE(m1_cards_step(ax_headless_world(h), &c, 2, NULL) == AX_OK);
    frame_golden(h, root, 4, "cards_color_f04");
    AX_REQUIRE(ax_headless_settle(h, 200, NULL) == AX_OK);

    /* The list narrows: every paragraph re-wraps at each interpolated width (decision 99). */
    AX_REQUIRE(m1_cards_step(ax_headless_world(h), &c, 3, NULL) == AX_OK);
    frame_golden(h, root, 4, "cards_narrow_f04");
    frame_golden(h, root, 6, "cards_narrow_f10");
    AX_REQUIRE(ax_headless_settle(h, 200, NULL) == AX_OK);
    frame_golden(h, root, 1, "cards_rest_after");
    ax_headless_destroy(h);
}
