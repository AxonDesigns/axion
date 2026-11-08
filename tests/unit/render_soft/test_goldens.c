/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every shared scene against its golden image, exactly (decisions 81 and 82): the coverage scenes,
 * the antialiasing torture scenes (rotating hairlines, the zone plate with and without nested
 * rounded clips) and what only the software renderer draws (group opacity, linear layers). The
 * same bytes on every platform CI builds.
 */
#include <stdlib.h>

#include "ax_test.h"
#include "axt_golden.h"
#include "axt_scenes.h"
#include "soft_test_util.h"

static void check(const char *name)
{
    const axt_scene *s = axt_scene_find(name);
    ax_ir_builder b;
    ax_render_soft *r = NULL;
    const ax_render_ir *ir;
    uint8_t *got;
    AX_REQUIRE(s != NULL);
    ax_ir_builder_init(&b, ax_default_allocator());
    ir = axt_scene_record(s, &b);
    AX_REQUIRE(ir != NULL);
    AX_REQUIRE(ax_render_soft_create(NULL, NULL, &r) == AX_OK);
    got = axt_soft_draw(r, ir, s->width, s->height, s->scale, NULL);
    AX_REQUIRE(got != NULL);
    AXT_CHECK_GOLDEN("render_soft", name, got, s->width, s->height, (size_t)s->width * 4u);
    free(got);
    ax_render_soft_destroy(r);
    ax_ir_builder_deinit(&b);
}

AX_TEST(coverage_scenes)
{
    check("axis_aligned");
    check("thin");
    check("rotated");
    check("rounded");
    check("scale_1_325");
    check("scale_2");
    check("blending");
}

AX_TEST(rotating_hairlines)
{
    check("hairlines_0");
    check("hairlines_1");
    check("hairlines_2");
    check("hairlines_3");
}

AX_TEST(zone_plate)
{
    check("zone_plate");
}

AX_TEST(zone_plate_behind_nested_rounded_clips)
{
    check("zone_plate_clipped");
}

AX_TEST(group_opacity_and_linear_layers)
{
    check("group_opacity");
    check("linear_layer");
}
