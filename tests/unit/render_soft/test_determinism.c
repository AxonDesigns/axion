/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_soft gives the same bytes with the scalar path and the SIMD path, and with any number
 * of threads (decision 81): every shared scene and 120 random ones (rects and rounded rects under
 * random transforms, nested clips, groups and linear layers), drawn every way and compared byte
 * for byte. Where the build has no SIMD path both runs are scalar.
 */
#include <stdio.h>
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"
#include "axt_scenes.h"
#include "soft_test_util.h"

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

static ax_color random_color(rng *r)
{
    const float a = (next_u32(r) & 3u) == 0 ? uniform(r, 0.05f, 1.0f) : 1.0f;
    return ax_color_from_srgb(uniform(r, 0, 1), uniform(r, 0, 1), uniform(r, 0, 1), a);
}

static void random_items(ax_ir_builder *b, rng *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        const ax_rect rect = ax_rect_make(uniform(r, -10, 90), uniform(r, -10, 70),
                                          uniform(r, 0.1f, 40), uniform(r, 0.1f, 30));
        if (next_u32(r) & 1u) {
            const float radii[4] = {uniform(r, 0, 12), uniform(r, 0, 12), uniform(r, 0, 12),
                                    uniform(r, 0, 12)};
            ax_ir_rounded_rect(b, rect, radii, random_color(r));
        } else {
            ax_ir_rect(b, rect, random_color(r));
        }
    }
}

static void random_layers(ax_ir_builder *b, rng *r, int depth)
{
    const uint32_t children = depth > 0 ? next_u32(r) % 3u : 0u;
    uint32_t i;
    random_items(b, r, 1u + next_u32(r) % 5u);
    for (i = 0; i < children; ++i) {
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        d.transform = ax_affine2_mul(
            ax_affine2_translate(uniform(r, -20, 60), uniform(r, -20, 40)),
            ax_affine2_mul(ax_affine2_rotate(uniform(r, -3.2f, 3.2f)),
                           ax_affine2_scale(uniform(r, 0.4f, 2.0f), uniform(r, 0.4f, 2.0f))));
        if ((next_u32(r) & 3u) == 0) {
            d.opacity = uniform(r, 0.1f, 0.9f);
        }
        if ((next_u32(r) & 3u) == 0) {
            d.clip.kind = (next_u32(r) & 1u) ? AX_IR_CLIP_ROUNDED_RECT : AX_IR_CLIP_RECT;
            d.clip.rect = ax_rect_make(uniform(r, -5, 30), uniform(r, -5, 30), uniform(r, 5, 60),
                                       uniform(r, 5, 50));
            d.clip.radii[0] = uniform(r, 0, 15);
            d.clip.radii[1] = uniform(r, 0, 15);
            d.clip.radii[2] = uniform(r, 0, 15);
            d.clip.radii[3] = uniform(r, 0, 15);
        }
        if ((next_u32(r) & 7u) == 0) {
            d.flags = AX_IR_LAYER_LINEAR_BLEND;
        }
        ax_ir_push_layer(b, &d);
        random_layers(b, r, depth - 1);
        ax_ir_pop_layer(b);
    }
}

typedef struct renderers {
    ax_job_system *js[3];     /* 1, 2 and 4 workers (NULL without threads) */
    ax_job_system *inline_js; /* inline mode */
    ax_render_soft *scalar;   /* scalar path, no job system */
    ax_render_soft *simd;     /* SIMD path, no job system */
    ax_render_soft *threaded[3];
    ax_render_soft *inline_r;
} renderers;

static ax_render_soft *make(uint32_t flags, ax_job_system *js)
{
    ax_render_soft_desc d;
    ax_render_soft *r = NULL;
    memset(&d, 0, sizeof d);
    d.flags = flags;
    d.jobs = js;
    if (ax_render_soft_create(NULL, &d, &r) != AX_OK) {
        return NULL;
    }
    return r;
}

static void renderers_init(renderers *rs)
{
    static const uint32_t workers[3] = {1, 2, 4};
    ax_jobs_desc jd = ax_jobs_default_desc();
    int i;
    memset(rs, 0, sizeof *rs);
    jd.mode = AX_JOBS_INLINE;
    AX_REQUIRE(ax_jobs_create(&jd, &rs->inline_js) == AX_OK);
    for (i = 0; i < 3 && ax_threads_supported(); ++i) {
        jd = ax_jobs_default_desc();
        jd.worker_count = workers[i];
        AX_REQUIRE(ax_jobs_create(&jd, &rs->js[i]) == AX_OK);
    }
    rs->scalar = make(AX_RENDER_SOFT_SCALAR, NULL);
    rs->simd = make(0, NULL);
    rs->inline_r = make(0, rs->inline_js);
    for (i = 0; i < 3; ++i) {
        rs->threaded[i] =
            rs->js[i] != NULL ? make(i == 1 ? AX_RENDER_SOFT_SCALAR : 0, rs->js[i]) : NULL;
    }
}

static void renderers_deinit(renderers *rs)
{
    int i;
    ax_render_soft_destroy(rs->scalar);
    ax_render_soft_destroy(rs->simd);
    ax_render_soft_destroy(rs->inline_r);
    for (i = 0; i < 3; ++i) {
        ax_render_soft_destroy(rs->threaded[i]);
        if (rs->js[i] != NULL) {
            ax_jobs_destroy(rs->js[i]);
        }
    }
    ax_jobs_destroy(rs->inline_js);
}

/* Draws ir every way; false (reported) at the first difference. */
static bool same_everywhere(renderers *rs, const char *what, const ax_render_ir *ir, uint32_t w,
                            uint32_t h, float scale)
{
    ax_render_soft *others[5];
    const char *names[5] = {"SIMD", "inline jobs", "1 worker", "2 workers (scalar)", "4 workers"};
    uint8_t *ref = axt_soft_draw(rs->scalar, ir, w, h, scale, NULL);
    bool ok = ref != NULL;
    int i;
    others[0] = rs->simd;
    others[1] = rs->inline_r;
    others[2] = rs->threaded[0];
    others[3] = rs->threaded[1];
    others[4] = rs->threaded[2];
    for (i = 0; ok && i < 5; ++i) {
        uint8_t *got;
        size_t k;
        if (others[i] == NULL) {
            continue;
        }
        got = axt_soft_draw(others[i], ir, w, h, scale, NULL);
        if (got == NULL) {
            ok = false;
            break;
        }
        for (k = 0; k < (size_t)w * h * 4u; ++k) {
            if (got[k] != ref[k]) {
                ax_test_fail(__FILE__, __LINE__,
                             "%s: %s differs from the scalar path at pixel (%u, %u) channel %u: "
                             "%u, expected %u",
                             what, names[i], (unsigned)((k / 4) % w), (unsigned)((k / 4) / w),
                             (unsigned)(k % 4), got[k], ref[k]);
                ok = false;
                break;
            }
        }
        free(got);
    }
    free(ref);
    return ok;
}

AX_TEST(every_scene_gives_the_same_bytes_every_way)
{
    renderers rs;
    ax_ir_builder b;
    uint32_t n, i;
    const axt_scene *scenes = axt_scenes(&n);
    renderers_init(&rs);
    ax_ir_builder_init(&b, ax_default_allocator());
    printf("SIMD path: %s\n", ax_render_soft_has_simd() ? "yes" : "no (both runs are scalar)");
    for (i = 0; i < n; ++i) {
        const ax_render_ir *ir = axt_scene_record(&scenes[i], &b);
        AX_REQUIRE(ir != NULL);
        if (!same_everywhere(&rs, scenes[i].name, ir, scenes[i].width, scenes[i].height,
                             scenes[i].scale)) {
            break;
        }
    }
    ax_ir_builder_deinit(&b);
    renderers_deinit(&rs);
}

AX_TEST(random_scenes_give_the_same_bytes_every_way)
{
    renderers rs;
    ax_ir_builder b;
    rng r = {0x9E3779B9u};
    int i;
    renderers_init(&rs);
    ax_ir_builder_init(&b, ax_default_allocator());
    for (i = 0; i < 120; ++i) {
        const float scale = (i % 3 == 0) ? 1.0f : (i % 3 == 1 ? 1.325f : 2.0f);
        const ax_render_ir *ir = NULL;
        char what[32];
        ax_ir_builder_begin(&b, 100, 70, 0, random_color(&r));
        random_layers(&b, &r, 3);
        AX_REQUIRE(ax_ir_builder_end(&b, &ir) == AX_OK);
        snprintf(what, sizeof what, "random scene %d", i);
        if (!same_everywhere(&rs, what, ir, (uint32_t)(100.0f * scale + 0.5f),
                             (uint32_t)(70.0f * scale + 0.5f), scale)) {
            break;
        }
    }
    ax_ir_builder_deinit(&b);
    renderers_deinit(&rs);
}
