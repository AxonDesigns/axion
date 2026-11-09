/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft: creation, statistics and drawing a snapshot tile by tile.
 */
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_thread.h"
#include "soft_internal.h"

/* Tile contexts: one per thread drawing tiles at once (claimed through a bitmask). */
#define SOFT_MAX_CTX 64u

struct ax_render_soft {
    ax_allocator allocator;
    ax_render_soft_desc desc;
    ax_arena scratch; /* the frame's commands */
    soft_tile_ctx ctx[SOFT_MAX_CTX];
    uint32_t ctx_count;
    ax_atomic_u64 free_ctx; /* bit i: ctx[i] is free */
    ax_render_soft_stats stats;
    /* Coverage corrections (decision 101 and issue #4), one per ax_ir_lut_table. */
    uint8_t luts[AX_IR_LUT_TABLES][AX_IR_TEXT_LUT_BUCKETS][256];
    void *user; /* the window side's state (surface.c), freed with the renderer */
    size_t user_size;
};

void *ax__render_soft_alloc_user(ax_render_soft *r, size_t size)
{
    AX_ASSERT(r->user == NULL);
    r->user = ax_mem_alloc(&r->allocator, size, 16);
    r->user_size = r->user != NULL ? size : 0;
    return r->user;
}

void *ax__render_soft_user(ax_render_soft *r)
{
    return r->user;
}

/* One job: every `step`-th tile from `first`. */
typedef struct tile_job {
    ax_render_soft *r;
    const soft_frame *f;
    uint32_t first, step, total, tiles_x;
} tile_job;

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
    {
        uint32_t t;
        for (t = 0; t < AX_IR_LUT_TABLES; ++t) {
            const ax_ir_text_lut_params lp = ax_ir_lut_default((ax_ir_lut_table)t);
            ax_ir_lut_build((ax_ir_lut_table)t, &lp, r->luts[t]);
        }
    }
    if (desc != NULL) {
        r->desc = *desc;
    }
    ax_arena_init(&r->scratch, &r->allocator, 0);
    r->ctx_count = 1;
    if (r->desc.jobs != NULL && !ax_jobs_is_inline(r->desc.jobs)) {
        /* Workers, the caller, and frame threads that may help while they wait. */
        r->ctx_count = ax_jobs_worker_count(r->desc.jobs) + 4u;
        if (r->ctx_count > SOFT_MAX_CTX) {
            r->ctx_count = SOFT_MAX_CTX;
        }
    }
    ax_atomic_init_u64(&r->free_ctx,
                       r->ctx_count == 64u ? UINT64_MAX : (((uint64_t)1 << r->ctx_count) - 1u));
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
    uint32_t i;
    for (i = 0; i < r->ctx_count; ++i) {
        free_ctx(r, &r->ctx[i]);
    }
    ax_arena_deinit(&r->scratch);
    if (r->user != NULL) {
        ax_mem_free(&r->allocator, r->user, r->user_size);
    }
    a = r->allocator;
    ax_mem_free(&a, r, sizeof *r);
}

bool ax_render_soft_has_simd(void)
{
    return ax__soft_simd_available();
}

static uint32_t claim_ctx(ax_render_soft *r)
{
    for (;;) {
        uint64_t m = ax_atomic_load_u64(&r->free_ctx, AX_MO_ACQUIRE);
        uint32_t bit = 0;
        if (m == 0) {
            ax_thread_yield(); /* more threads than contexts draw at once: rare, wait */
            continue;
        }
        while (((m >> bit) & 1u) == 0) {
            bit++;
        }
        if (ax_atomic_compare_exchange_u64(&r->free_ctx, &m, m & ~((uint64_t)1 << bit),
                                           AX_MO_ACQ_REL, AX_MO_RELAXED)) {
            return bit;
        }
    }
}

static void release_ctx(ax_render_soft *r, uint32_t bit)
{
    uint64_t m = ax_atomic_load_u64(&r->free_ctx, AX_MO_RELAXED);
    while (!ax_atomic_compare_exchange_u64(&r->free_ctx, &m, m | ((uint64_t)1 << bit),
                                           AX_MO_RELEASE, AX_MO_RELAXED)) {
    }
}

static void tile_job_fn(void *arg)
{
    const tile_job *j = (const tile_job *)arg;
    const uint32_t c = claim_ctx(j->r);
    uint32_t t;
    for (t = j->first; t < j->total; t += j->step) {
        ax__soft_draw_tile(j->f, &j->r->ctx[c], t % j->tiles_x, t / j->tiles_x);
    }
    release_ctx(j->r, c);
}

/* Draws every tile: as frame jobs on the job system (the caller helps while it waits), or on the
 * calling thread. Tiles are independent and each writes only its own pixels, so the result is
 * the same either way. */
static ax_result draw_tiles(ax_render_soft *r, const soft_frame *f, uint32_t tiles_x,
                            uint32_t tiles_y)
{
    const uint32_t total = tiles_x * tiles_y;
    ax_job_system *js = r->desc.jobs;
    uint32_t i, jobs;
    tile_job *args;
    ax_job *list;
    ax_job_counter done = AX_JOB_COUNTER_INIT;

    if (js == NULL || ax_jobs_is_inline(js) || total == 1u) {
        for (i = 0; i < total; ++i) {
            ax__soft_draw_tile(f, &r->ctx[0], i % tiles_x, i / tiles_x);
        }
        return AX_OK;
    }
    /* A few jobs per thread, interleaved, so uneven tiles balance. */
    jobs = 4u * (ax_jobs_worker_count(js) + 1u);
    jobs = jobs < total ? jobs : total;
    args = AX_ARENA_NEW_ARRAY(&r->scratch, tile_job, jobs);
    list = AX_ARENA_NEW_ARRAY(&r->scratch, ax_job, jobs);
    if (args == NULL || list == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft: out of memory");
    }
    for (i = 0; i < jobs; ++i) {
        args[i].r = r;
        args[i].f = f;
        args[i].first = i;
        args[i].step = jobs;
        args[i].total = total;
        args[i].tiles_x = tiles_x;
        list[i].fn = tile_job_fn;
        list[i].arg = &args[i];
    }
    ax_jobs_submit(js, list, jobs, &done);
    ax_jobs_wait(js, &done);
    return AX_OK;
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
    uint32_t tiles_x, tiles_y, i;
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
        soft_blend blend;
        blend.mode = (r->desc.diagnostic_flags & AX_RENDER_SOFT_DIAG_LINEAR_BLEND) != 0
                         ? (uint32_t)AX_RENDER_BLEND_LINEAR
                         : r->desc.blend;
        blend.luts = (const uint8_t(*)[AX_IR_TEXT_LUT_BUCKETS][256])r->luts;
        res = ax__soft_prepare(ir, target->width, target->height, target->scale, &blend,
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
    f.simd = ax__soft_simd_available() && (r->desc.flags & AX_RENDER_SOFT_SCALAR) == 0;
    for (i = 0; i < r->ctx_count; ++i) {
        if (!ensure_ctx(r, &r->ctx[i], f.group_depth)) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_render_soft: out of memory");
        }
    }

    tiles_x = (f.width + SOFT_TILE - 1u) / SOFT_TILE;
    tiles_y = (f.height + SOFT_TILE - 1u) / SOFT_TILE;
    res = draw_tiles(r, &f, tiles_x, tiles_y);
    if (res != AX_OK) {
        return res;
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
