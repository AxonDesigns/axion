/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft: creation, statistics and drawing a snapshot tile by tile.
 */
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_thread.h"
#include "soft_internal.h"

struct ax_render_soft {
    ax_allocator allocator;
    ax_render_soft_desc desc;
    ax_arena scratch; /* the frame's commands */
    soft_tile_ctx ctx;
    ax_render_soft_stats stats;
};

ax_result ax_render_soft_create(const ax_allocator *allocator, const ax_render_soft_desc *desc,
                                ax_render_soft **out)
{
    const ax_allocator *a = allocator != NULL ? allocator : ax_default_allocator();
    ax_render_soft *r;
    AX_ASSERT(out != NULL);
    *out = NULL;
    r = (ax_render_soft *)ax_mem_alloc(a, sizeof *r, AX_ALIGNOF(ax_render_soft));
    if (r == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft_create: out of memory");
    }
    memset(r, 0, sizeof *r);
    r->allocator = *a;
    if (desc != NULL) {
        r->desc = *desc;
    }
    ax_arena_init(&r->scratch, &r->allocator, 0);
    *out = r;
    return AX_OK;
}

static void free_ctx(ax_render_soft *r, soft_tile_ctx *ctx)
{
    if (ctx->planes != NULL) {
        ax_mem_free(&r->allocator, ctx->planes,
                    sizeof(float) * 4u * SOFT_TILE_PIXELS * (ctx->depth_capacity + 1u));
    }
    ctx->planes = NULL;
    ctx->depth_capacity = 0;
}

/* Tile buffers for `depth` nested groups. */
static bool ensure_ctx(ax_render_soft *r, soft_tile_ctx *ctx, uint32_t depth)
{
    if (ctx->planes != NULL && ctx->depth_capacity >= depth) {
        return true;
    }
    free_ctx(r, ctx);
    ctx->planes = (float *)ax_mem_alloc(&r->allocator,
                                        sizeof(float) * 4u * SOFT_TILE_PIXELS * (depth + 1u), 64);
    if (ctx->planes == NULL) {
        return false;
    }
    ctx->depth_capacity = depth;
    return true;
}

void ax_render_soft_destroy(ax_render_soft *r)
{
    ax_allocator a;
    if (r == NULL) {
        return;
    }
    free_ctx(r, &r->ctx);
    ax_arena_deinit(&r->scratch);
    a = r->allocator;
    ax_mem_free(&a, r, sizeof *r);
}

bool ax_render_soft_has_simd(void)
{
    return false;
}

static float clamp01(float v)
{
    return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
}

/* The background, premultiplied and sRGB-encoded. */
static void encode_background(ax_color c, float out[4])
{
    const float a = clamp01(c.a);
    int k;
    const float ch[3] = {c.r, c.g, c.b};
    if (!(c.a > 0.0f)) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    for (k = 0; k < 3; ++k) {
        const float v = ax__soft_linear_to_srgb(ch[k] / c.a) * a;
        out[k] = v < SOFT_MIN_COLOR ? 0.0f : v;
    }
    out[3] = a;
}

ax_result ax_render_soft_draw_ir(ax_render_soft *r, const ax_soft_target *target,
                                 const ax_render_ir *ir, ax_render_counts *out_counts)
{
    const uint64_t start = ax_time_monotonic_ns();
    ax_render_counts counts;
    soft_frame f;
    uint32_t tiles_x, tiles_y, tx, ty;
    uint64_t elapsed;
    ax_result res;

    AX_ASSERT(r != NULL);
    memset(&counts, 0, sizeof counts);
    if (target == NULL || target->struct_size < sizeof(ax_soft_target) || target->pixels == NULL ||
        target->width == 0 || target->height == 0 || target->width > 32768u ||
        target->height > 32768u || target->stride < (size_t)target->width * 4u ||
        (target->format != AX_SOFT_RGBA8 && target->format != AX_SOFT_BGRA8) ||
        !(target->scale > 0.0f) || !(target->scale <= 1024.0f)) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_render_soft_draw_ir: invalid target");
    }
    if (ir != NULL && ir->version_major != AX_IR_VERSION_MAJOR) {
        return ax_set_error(AX_ERR_ABI_MISMATCH,
                            "ax_render_soft_draw_ir: Render IR major version %u, expected %u",
                            (unsigned)ir->version_major, (unsigned)AX_IR_VERSION_MAJOR);
    }

    memset(&f, 0, sizeof f);
    ax_arena_reset(&r->scratch);
    if (ir != NULL) {
        res = ax__soft_prepare(ir, target->width, target->height, target->scale,
                               (r->desc.diagnostic_flags & AX_RENDER_SOFT_DIAG_LINEAR_BLEND) != 0,
                               &r->scratch, &f, &counts);
        if (res != AX_OK) {
            return res;
        }
    }
    encode_background(ir != NULL ? ir->background : target->background, f.background);
    f.width = target->width;
    f.height = target->height;
    f.pixels = target->pixels;
    f.stride = target->stride;
    f.format = target->format;
    if (!ensure_ctx(r, &r->ctx, f.group_depth)) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft: out of memory");
    }

    tiles_x = (f.width + SOFT_TILE - 1u) / SOFT_TILE;
    tiles_y = (f.height + SOFT_TILE - 1u) / SOFT_TILE;
    for (ty = 0; ty < tiles_y; ++ty) {
        for (tx = 0; tx < tiles_x; ++tx) {
            ax__soft_draw_tile(&f, &r->ctx, tx, ty);
        }
    }

    elapsed = ax_time_monotonic_ns() - start;
    r->stats.frames++;
    r->stats.tiles += (uint64_t)tiles_x * tiles_y;
    r->stats.draw_ns_total += elapsed;
    if (elapsed > r->stats.draw_ns_max) {
        r->stats.draw_ns_max = elapsed;
    }
    r->stats.commands_last = f.cmd_count;
    r->stats.simd = f.simd;
    if (out_counts != NULL) {
        *out_counts = counts;
    }
    return AX_OK;
}

void ax_render_soft_get_stats(ax_render_soft *r, ax_render_soft_stats *out, bool reset)
{
    const uint32_t size = out->struct_size;
    *out = r->stats;
    out->struct_size = size;
    if (reset) {
        const bool simd = r->stats.simd;
        memset(&r->stats, 0, sizeof r->stats);
        r->stats.simd = simd;
    }
}
