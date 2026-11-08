/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft against the supersampled reference (coverage_reference.c, decision 56's
 * provisional reference, now a test of the software renderer itself, decision 81).
 *
 * Tolerances, per channel, on 8-bit values:
 *   - EDGE_TOLERANCE away from corners, where the coverage is exact: the reference's sampling
 *     error (at most 1/128 of an edge pixel's area) and one rounding;
 *   - CORNER_TOLERANCE near a rect's vertex under rotation or an arc, where the coverage multiplies
 *     two slabs or treats the arc as straight (decision 55);
 *   - MEAN_TOLERANCE over the image.
 */
#include <math.h>
#include <stdio.h>

#include "ax_test.h"
#include "axt_scenes.h"
#include "coverage_reference.h"
#include "soft_test_util.h"

#define EDGE_TOLERANCE   2.0
#define CORNER_TOLERANCE 20.0
#define MEAN_TOLERANCE   0.1

static void check_scene(const char *name)
{
    const axt_scene *s = axt_scene_find(name);
    ax_ir_builder b;
    const ax_render_ir *ir;
    ax_render_soft *r = NULL;
    axt_reference ref;
    uint8_t *got;
    double max_edge = 0, max_corner = 0, sum = 0;
    uint32_t i, wx = 0, wy = 0;

    AX_REQUIRE(s != NULL);
    ax_ir_builder_init(&b, ax_default_allocator());
    ir = axt_scene_record(s, &b);
    AX_REQUIRE(ir != NULL);
    AX_REQUIRE(ax_render_soft_create(NULL, NULL, &r) == AX_OK);
    got = axt_soft_draw(r, ir, s->width, s->height, s->scale, NULL);
    AX_REQUIRE(got != NULL);
    AX_REQUIRE(axt_reference_render(ir, s->width, s->height, s->scale, 128, &ref));
    for (i = 0; i < s->width * s->height; ++i) {
        int k;
        for (k = 0; k < 4; ++k) {
            const double expected = floor((double)ref.rgba[i * 4 + k] * 255.0 + 0.5);
            const double err = fabs((double)got[i * 4 + k] - expected);
            sum += err;
            if (ref.corner[i]) {
                max_corner = err > max_corner ? err : max_corner;
            } else if (err > max_edge) {
                max_edge = err;
                wx = i % s->width;
                wy = i / s->width;
            }
        }
    }
    sum /= (double)(s->width * s->height * 4u);
    printf("%s: max error %.0f/255 at edges (worst at %u,%u), %.0f/255 near corners, mean "
           "%.4f/255\n",
           name, max_edge, wx, wy, max_corner, sum);
    AX_CHECK(max_edge <= EDGE_TOLERANCE);
    AX_CHECK(max_corner <= CORNER_TOLERANCE);
    AX_CHECK(sum <= MEAN_TOLERANCE);
    axt_reference_free(&ref);
    free(got);
    ax_render_soft_destroy(r);
    ax_ir_builder_deinit(&b);
}

AX_TEST(axis_aligned_rects_at_fractional_positions)
{
    check_scene("axis_aligned");
}

AX_TEST(thin_rects_keep_their_area)
{
    check_scene("thin");
}

AX_TEST(rotated_rects_have_smooth_edges)
{
    check_scene("rotated");
}

AX_TEST(rounded_rects_with_radii_per_corner)
{
    check_scene("rounded");
}

AX_TEST(fractional_and_integer_device_scales)
{
    check_scene("scale_1_325");
    check_scene("scale_2");
}

AX_TEST(translucent_overlaps_blend_in_srgb_space)
{
    check_scene("blending");
}

AX_TEST(rotating_hairlines)
{
    check_scene("hairlines_0");
    check_scene("hairlines_3");
}
