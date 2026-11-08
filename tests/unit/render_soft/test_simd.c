/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The SIMD path against the scalar one, bit for bit, before anything is rounded to 8 bits
 * (decision 81): coverage at every pixel of every item and clip of random scenes, and each tile's
 * float planes after drawing. Comparing 8-bit output alone would miss a difference of one ulp,
 * which almost never crosses a rounding step. Uses the renderer's internals.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_arena.h"
#include "soft_internal.h"

typedef struct rng {
    uint32_t s;
} rng;

static uint32_t next_u32(rng *r)
{
    r->s ^= r->s << 13;
    r->s ^= r->s >> 17;
    r->s ^= r->s << 5;
    return r->s;
}

static float uniform(rng *r, float lo, float hi)
{
    return lo + (hi - lo) * (float)(next_u32(r) >> 8) / 16777216.0f;
}

static void random_scene(ax_ir_builder *b, rng *r)
{
    int layer, depth = 0;
    ax_ir_builder_begin(b, 120, 90, 0, ax_color_make(0.1f, 0.1f, 0.1f, 1));
    for (layer = 0; layer < 6; ++layer) {
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        int i;
        d.transform = ax_affine2_mul(
            ax_affine2_translate(uniform(r, 0, 100), uniform(r, 0, 80)),
            ax_affine2_mul(ax_affine2_rotate(uniform(r, -3.2f, 3.2f)),
                           ax_affine2_scale(uniform(r, 0.3f, 3.0f), uniform(r, 0.3f, 3.0f))));
        if (layer % 2 == 1) {
            d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
            d.clip.rect = ax_rect_make(-20, -15, uniform(r, 10, 50), uniform(r, 10, 40));
            d.clip.radii[0] = uniform(r, 0, 20);
            d.clip.radii[1] = uniform(r, 0, 20);
            d.clip.radii[2] = uniform(r, 0, 20);
            d.clip.radii[3] = uniform(r, 0, 20);
            d.opacity = uniform(r, 0.2f, 1.0f);
        }
        ax_ir_push_layer(b, &d);
        depth++;
        for (i = 0; i < 4; ++i) {
            const ax_rect rect = ax_rect_make(uniform(r, -25, 10), uniform(r, -20, 10),
                                              uniform(r, 0.05f, 30), uniform(r, 0.05f, 25));
            const float radii[4] = {uniform(r, 0, 15), uniform(r, 0, 15), uniform(r, 0, 15),
                                    uniform(r, 0, 15)};
            const ax_color c = ax_color_make(uniform(r, 0, 0.5f), uniform(r, 0, 0.5f),
                                             uniform(r, 0, 0.5f), uniform(r, 0.5f, 1));
            if (i % 2) {
                ax_ir_rounded_rect(b, rect, radii, c);
            } else {
                ax_ir_rect(b, rect, c);
            }
        }
        if (layer % 3 == 2) {
            ax_ir_pop_layer(b);
            depth--;
        }
    }
    while (depth-- > 0) {
        ax_ir_pop_layer(b);
    }
}

static bool same_bits(float a, float b)
{
    uint32_t x, y;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    return x == y;
}

/* Coverage over the shape's pixels and a margin, four pixels at a time. */
static uint64_t compare_shape(const soft_shape *s, soft_box box, uint32_t *mismatches)
{
    uint64_t n = 0;
    int32_t x, y;
    for (y = box.y0 - 2; y < box.y1 + 2; ++y) {
        for (x = box.x0 - 4; x < box.x1 + 4; x += 4) {
            float xs[4], simd[4];
            const float py = (float)y + 0.5f;
            int k;
            for (k = 0; k < 4; ++k) {
                xs[k] = (float)(x + k) + 0.5f;
            }
            ax__soft_shape_coverage4(s, xs, py, simd);
            for (k = 0; k < 4; ++k) {
                const float scalar = ax__soft_shape_coverage(s, xs[k], py);
                if (!same_bits(scalar, simd[k])) {
                    if (*mismatches < 3) {
                        printf("coverage at (%g, %g): scalar %.9g, SIMD %.9g\n", (double)xs[k],
                               (double)py, (double)scalar, (double)simd[k]);
                    }
                    (*mismatches)++;
                }
                n++;
            }
        }
    }
    return n;
}

AX_TEST(coverage_is_bit_identical)
{
    ax_ir_builder b;
    ax_arena arena;
    rng r = {12345u};
    uint32_t mismatches = 0;
    uint64_t points = 0;
    int scene;
    if (!ax__soft_simd_available()) {
        AX_SKIP("no SIMD path in this build");
    }
    printf("SIMD path: %s\n", ax__soft_simd_name());
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    for (scene = 0; scene < 40; ++scene) {
        const ax_render_ir *ir = NULL;
        soft_frame f;
        ax_render_counts counts;
        uint32_t i;
        random_scene(&b, &r);
        AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
        memset(&f, 0, sizeof f);
        memset(&counts, 0, sizeof counts);
        ax_arena_reset(&arena);
        AX_REQUIRE(ax__soft_prepare(ir, 240, 180, 2.0f, false, &arena, &f, &counts) == AX_OK);
        for (i = 0; i < f.cmd_count; ++i) {
            if (f.cmds[i].kind == SOFT_CMD_ITEM) {
                points += compare_shape(&f.cmds[i].shape, f.cmds[i].box, &mismatches);
            }
        }
        for (i = 0; i < f.clip_count; ++i) {
            const soft_box all = {0, 0, 240, 180};
            points += compare_shape(&f.clips[i].shape, all, &mismatches);
        }
    }
    printf("%llu points compared, %u differ\n", (unsigned long long)points, mismatches);
    AX_CHECK_EQ_U(mismatches, 0);
    AX_CHECK(points > 100000u);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}

AX_TEST(tile_planes_are_bit_identical)
{
    ax_ir_builder b;
    ax_arena arena;
    rng r = {777u};
    int scene;
    soft_tile_ctx ctx[2];
    uint8_t *pixels = (uint8_t *)malloc(240u * 180u * 4u);
    uint32_t differ = 0;
    if (!ax__soft_simd_available()) {
        free(pixels);
        AX_SKIP("no SIMD path in this build");
    }
    AX_REQUIRE(pixels != NULL);
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_arena_init(&arena, ax_default_allocator(), 0);
    memset(ctx, 0, sizeof ctx);
    for (scene = 0; scene < 40; ++scene) {
        const ax_render_ir *ir = NULL;
        soft_frame f;
        ax_render_counts counts;
        uint32_t tx, ty;
        int k;
        random_scene(&b, &r);
        AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
        memset(&f, 0, sizeof f);
        memset(&counts, 0, sizeof counts);
        ax_arena_reset(&arena);
        AX_REQUIRE(ax__soft_prepare(ir, 240, 180, 2.0f, scene % 4 == 3, &arena, &f, &counts) ==
                   AX_OK);
        f.width = 240;
        f.height = 180;
        f.pixels = pixels;
        f.stride = 240u * 4u;
        f.format = AX_SOFT_RGBA8;
        for (k = 0; k < 2; ++k) {
            const size_t floats = 4u * SOFT_TILE_PIXELS * (f.group_depth + 1u);
            free(ctx[k].planes);
            ctx[k].planes = (float *)calloc(floats, sizeof(float));
            ctx[k].depth_capacity = f.group_depth;
            AX_REQUIRE(ctx[k].planes != NULL);
        }
        for (ty = 0; ty < 3; ++ty) {
            for (tx = 0; tx < 4; ++tx) {
                f.simd = false;
                ax__soft_draw_tile(&f, &ctx[0], tx, ty);
                f.simd = true;
                ax__soft_draw_tile(&f, &ctx[1], tx, ty);
                /* The base planes hold the tile's floats after drawing. */
                if (memcmp(ctx[0].planes, ctx[1].planes, sizeof(float) * 4u * SOFT_TILE_PIXELS) !=
                    0) {
                    differ++;
                }
            }
        }
    }
    AX_CHECK_EQ_U(differ, 0);
    free(ctx[0].planes);
    free(ctx[1].planes);
    free(pixels);
    ax_arena_deinit(&arena);
    ax_ir_builder_deinit(&b);
}
