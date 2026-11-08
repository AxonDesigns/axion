/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_render_wgpu against ax_render_soft, the reference (decisions 81 and 83): the shared scenes
 * (tests/scenes) rendered offscreen and compared with the software renderer's drawing of the
 * same snapshot, within a perceptual tolerance:
 *   - per pixel, the color difference in OKLab (Euclidean, from both pixels decoded to linear
 *     light), dE;
 *   - PERCENTILE of the pixels within DE_PERCENTILE_LIMIT, and the mean dE within DE_MEAN_LIMIT.
 * A percentile rather than a maximum: both renderers compute the same coverage, but a GPU rounds
 * into its 8-bit target after every blend while the software renderer rounds once, so a few
 * pixels where many translucent edges overlap may differ by more than the rest.
 * Each test prints its measures. With AX_TEST_ARTIFACT_DIR set, every image is also written there
 * as <scene>.ppm, for CI to compare WebGPU implementations with each other
 * (tools/ci/compare_ppm.py).
 */
#if defined(__linux__)
#    define _POSIX_C_SOURCE 200809L
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/render/ax_render_soft.h"
#include "axion/render/ax_render_wgpu.h"
#include "axt_scenes.h"
#include "gpu_test_util.h"
#include "rw_testing.h"

#define PERCENTILE          0.999 /* of the pixels ... */
#define DE_PERCENTILE_LIMIT 0.02  /* ... within this dE (OKLab) */
#define DE_MEAN_LIMIT       0.002

#define PI_F 3.14159265358979f

static const WGPUTextureFormat k_format = WGPUTextureFormat_RGBA8Unorm;

typedef struct env {
    WGPUTextureFormat format;
    ax_gpu *gpu;
    ax_render_wgpu *r;
    ax_ir_builder b;
} env;

/* 1: ready; 0: skip (no adapter); -1: failed (reported). */
static int env_init_ex(env *e, WGPUTextureFormat format, uint32_t diagnostic_flags)
{
    ax_render_wgpu_desc d;
    int g;
    memset(e, 0, sizeof *e);
    e->format = format;
    g = axt_gpu_create(NULL, &e->gpu);
    if (g <= 0) {
        return g;
    }
    memset(&d, 0, sizeof d);
    d.gpu = e->gpu;
    d.diagnostic_flags = diagnostic_flags;
    if (ax_render_wgpu_create(NULL, &d, &e->r) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "ax_render_wgpu_create: %s", ax_last_error()->message.ptr);
        ax_gpu_destroy(e->gpu);
        return -1;
    }
    if (!ax__render_wgpu_test_wait_ready(e->r, format, 30ull * 1000000000ull)) {
        ax_test_fail(__FILE__, __LINE__, "the pipeline did not become ready");
        ax_render_wgpu_destroy(e->r);
        ax_gpu_destroy(e->gpu);
        return -1;
    }
    ax_ir_builder_init(&e->b, ax_default_allocator());
    return 1;
}

static int env_init(env *e)
{
    return env_init_ex(e, k_format, 0);
}

static void env_deinit(env *e)
{
    ax_ir_builder_deinit(&e->b);
    ax_render_wgpu_destroy(e->r);
    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(e->gpu), 0);
    ax_gpu_destroy(e->gpu);
}

#define ENV_INIT_EX(e, init)                                                           \
    do {                                                                               \
        int axt__e = (init);                                                           \
        if (axt__e == 0) {                                                             \
            AX_SKIP("no WebGPU adapter (set AX_TEST_GPU_ADAPTER=software and install " \
                    "a software Vulkan driver)");                                      \
        }                                                                              \
        if (axt__e < 0) {                                                              \
            return;                                                                    \
        }                                                                              \
    } while (0)
#define ENV_INIT(e) ENV_INIT_EX(e, env_init(e))

/* Renders ir into a fresh texture and reads it back (width * height * 4 bytes, RGBA). */
static uint8_t *render(env *e, const ax_render_ir *ir, uint32_t w, uint32_t h, float scale,
                       ax_frame_status *status, ax_render_counts *counts)
{
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    WGPUTexture tex;
    WGPUTextureView view;
    ax_render_wgpu_draw draw;
    uint8_t *pixels = (uint8_t *)malloc((size_t)w * h * 4);
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.size.width = w;
    td.size.height = h;
    td.size.depthOrArrayLayers = 1;
    td.format = e->format;
    tex = wgpuDeviceCreateTexture(ax_gpu_device(e->gpu), &td);
    view = wgpuTextureCreateView(tex, NULL);
    memset(&draw, 0, sizeof draw);
    draw.struct_size = sizeof draw;
    draw.target = view;
    draw.format = e->format;
    draw.width = w;
    draw.height = h;
    draw.scale = scale;
    draw.background = ax_color_make(0, 0, 0, 1);
    if (pixels == NULL || ax_render_wgpu_draw_ir(e->r, &draw, ir, status, counts) != AX_OK ||
        !axt_gpu_read_texture(e->gpu, tex, w, h, pixels)) {
        ax_test_fail(__FILE__, __LINE__, "rendering failed");
        free(pixels);
        pixels = NULL;
    }
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(tex);
    return pixels;
}

static void write_artifact(const char *scene, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    const char *dir = getenv("AX_TEST_ARTIFACT_DIR");
    char path[1024];
    FILE *f;
    uint32_t i;
    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    snprintf(path, sizeof path, "%s/%s.ppm", dir, scene);
    f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (i = 0; i < w * h; ++i) {
        fwrite(rgba + (size_t)i * 4, 1, 3, f);
    }
    fclose(f);
}

typedef struct compare_result {
    double de_percentile, de_mean, de_max;
    uint32_t max_channel; /* 8-bit, for reference */
    uint32_t worst_x, worst_y;
} compare_result;

static double srgb_to_linear(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* OKLab of an 8-bit sRGB pixel (opaque: every scene has an opaque background). */
static void oklab(const uint8_t *p, double out[3])
{
    const double r = srgb_to_linear(p[0] / 255.0), g = srgb_to_linear(p[1] / 255.0),
                 b = srgb_to_linear(p[2] / 255.0);
    const double l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    out[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    out[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    out[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* The software renderer's drawing of ir (RGBA8, like the GPU's target). */
static uint8_t *reference(const ax_render_ir *ir, uint32_t w, uint32_t h, float scale,
                          uint32_t diag)
{
    ax_render_soft_desc d;
    ax_render_soft *r = NULL;
    ax_soft_target t;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4u);
    memset(&d, 0, sizeof d);
    d.diagnostic_flags = diag;
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = px;
    t.width = w;
    t.height = h;
    t.stride = (size_t)w * 4u;
    t.format = AX_SOFT_RGBA8;
    t.scale = scale;
    t.background = ax_color_make(0, 0, 0, 1);
    if (px == NULL || ax_render_soft_create(NULL, &d, &r) != AX_OK ||
        ax_render_soft_draw_ir(r, &t, ir, NULL) != AX_OK) {
        free(px);
        px = NULL;
    }
    ax_render_soft_destroy(r);
    return px;
}

/* Renders, compares with the software renderer, checks the tolerance, prints the measures. */
static void check_scene_ex(env *e, const char *scene, const ax_render_ir *ir, uint32_t w,
                           uint32_t h, float scale, uint32_t soft_diag, compare_result *out)
{
    ax_frame_status status = AX_FRAME_NOTHING;
    uint8_t *got = render(e, ir, w, h, scale, &status, NULL);
    uint8_t *want;
    double *de;
    compare_result res;
    double sum = 0.0;
    uint32_t i, n = w * h;

    if (got == NULL) {
        return;
    }
    memset(&res, 0, sizeof res);
    AX_CHECK_EQ_U(status, AX_FRAME_PRESENTED);
    write_artifact(scene, got, w, h);
    want = reference(ir, w, h, scale, soft_diag);
    de = (double *)malloc(sizeof(double) * n);
    if (want == NULL || de == NULL) {
        ax_test_fail(__FILE__, __LINE__, "out of memory");
        free(got);
        free(want);
        free(de);
        return;
    }
    for (i = 0; i < n; ++i) {
        double a[3], b[3];
        int k;
        oklab(got + i * 4u, a);
        oklab(want + i * 4u, b);
        de[i] = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                     (a[2] - b[2]) * (a[2] - b[2]));
        sum += de[i];
        if (de[i] > res.de_max) {
            res.de_max = de[i];
            res.worst_x = i % w;
            res.worst_y = i / w;
        }
        for (k = 0; k < 4; ++k) {
            const uint32_t d = (uint32_t)abs((int)got[i * 4u + k] - (int)want[i * 4u + k]);
            res.max_channel = d > res.max_channel ? d : res.max_channel;
        }
    }
    res.de_mean = sum / (double)n;
    qsort(de, n, sizeof(double), cmp_double);
    res.de_percentile = de[(size_t)((double)(n - 1u) * PERCENTILE)];
    printf("%s: dE p99.9 %.4f, mean %.5f, max %.4f at (%u, %u); max channel difference %u/255\n",
           scene, res.de_percentile, res.de_mean, res.de_max, res.worst_x, res.worst_y,
           res.max_channel);
    {
        /* The measures go with the images, so CI's artifacts carry every implementation's. */
        const char *dir = getenv("AX_TEST_ARTIFACT_DIR");
        if (dir != NULL && dir[0] != '\0') {
            char path[1024];
            FILE *f;
            snprintf(path, sizeof path, "%s/measures.txt", dir);
            f = fopen(path, "a");
            if (f != NULL) {
                fprintf(f, "%s: dE p99.9 %.4f mean %.5f max %.4f channel %u\n", scene,
                        res.de_percentile, res.de_mean, res.de_max, res.max_channel);
                fclose(f);
            }
        }
    }
    if (res.de_percentile > DE_PERCENTILE_LIMIT) {
        ax_test_fail(__FILE__, __LINE__, "%s: dE at the 99.9th percentile %.4f > %.3f", scene,
                     res.de_percentile, DE_PERCENTILE_LIMIT);
    }
    if (res.de_mean > DE_MEAN_LIMIT) {
        ax_test_fail(__FILE__, __LINE__, "%s: mean dE %.5f > %.4f", scene, res.de_mean,
                     DE_MEAN_LIMIT);
    }
    if (out != NULL) {
        *out = res;
    }
    free(de);
    free(want);
    free(got);
}

static void check_scene(env *e, const char *scene, const ax_render_ir *ir, uint32_t w, uint32_t h,
                        float scale, compare_result *out)
{
    check_scene_ex(e, scene, ir, w, h, scale, 0, out);
}

/* A shared scene (tests/scenes), at its size and scale. */
static void check_shared(env *e, const char *name)
{
    const axt_scene *s = axt_scene_find(name);
    const ax_render_ir *ir;
    AX_REQUIRE(s != NULL);
    ir = axt_scene_record(s, &e->b);
    AX_REQUIRE(ir != NULL);
    check_scene(e, name, ir, s->width, s->height, s->scale, NULL);
}

static const ax_render_ir *finish(env *e)
{
    const ax_render_ir *ir = NULL;
    if (ax_ir_builder_end(&e->b, &ir) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "recording failed");
    }
    return ir;
}

static ax_color white(void)
{
    return ax_color_make(1, 1, 1, 1);
}

/* ---- scenes -----------------------------------------------------------------------------------
 */

/* The shared scenes ax_render_wgpu draws exactly (no clips, group opacity or linear layers). */

AX_TEST(axis_aligned_rects_at_fractional_positions)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "axis_aligned");
    env_deinit(&e);
}

AX_TEST(thin_rects_keep_their_area)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "thin");
    env_deinit(&e);
}

AX_TEST(rotated_rects_have_smooth_edges)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "rotated");
    env_deinit(&e);
}

AX_TEST(rounded_rects_with_radii_per_corner)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "rounded");
    env_deinit(&e);
}

AX_TEST(fractional_and_integer_device_scales)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "scale_1_325");
    check_shared(&e, "scale_2");
    env_deinit(&e);
}

/* Antialiasing torture scenes ("Antialiasing and image quality", Verification), the zone plate
 * also behind three nested rounded clips, the middle one turned (decision 93). */
AX_TEST(rotating_hairlines)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "hairlines_0");
    check_shared(&e, "hairlines_1");
    check_shared(&e, "hairlines_2");
    check_shared(&e, "hairlines_3");
    env_deinit(&e);
}

AX_TEST(zone_plate)
{
    env e;
    ENV_INIT(&e);
    check_shared(&e, "zone_plate");
    check_shared(&e, "zone_plate_clipped");
    env_deinit(&e);
}

static void rotated(env *e, float degrees, float x, float y, float w, float h, ax_color c)
{
    axt_rotated_rect(&e->b, degrees, x, y, w, h, c);
}

AX_TEST(blending_is_in_srgb_space_with_premultiplied_alpha)
{
    env e;
    const ax_render_ir *ir;
    uint8_t *got;
    ax_frame_status status;
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 32, 16, 0, ax_color_make(0, 0, 0, 1));
    /* 50% white over black: 128 in sRGB space (browsers); linear blending would give 188. */
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    /* 50% blue over 100% red, overlapping. */
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
    ir = finish(&e);
    if (ir == NULL) {
        env_deinit(&e);
        return;
    }
    got = render(&e, ir, 32, 16, 1.0f, &status, NULL);
    if (got != NULL) {
        const uint8_t *a = got + (8 * 32 + 8) * 4;
        const uint8_t *b = got + (8 * 32 + 24) * 4;
        AX_CHECK(a[0] >= 127 && a[0] <= 129);
        AX_CHECK(a[1] >= 127 && a[1] <= 129);
        AX_CHECK(b[0] >= 126 && b[0] <= 128); /* 255 * (1 - 128/255) */
        AX_CHECK_EQ_U(b[1], 0);
        AX_CHECK(b[2] >= 127 && b[2] <= 129);
        AX_CHECK_EQ_U(a[3], 255);
        free(got);
    }
    check_scene(&e, "blending", ir, 32, 16, 1.0f, NULL);
    env_deinit(&e);
}

/* The unsupported diagnostic option (decision 58): the same scene mixes in linear light. */
AX_TEST(the_linear_blend_diagnostic_mixes_in_linear_light)
{
    env e;
    const ax_render_ir *ir;
    uint8_t *got;
    ax_frame_status status;
    ENV_INIT_EX(
        &e, env_init_ex(&e, WGPUTextureFormat_RGBA8UnormSrgb, AX_RENDER_WGPU_DIAG_LINEAR_BLEND));
    ax_ir_builder_begin(&e.b, 32, 24, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), ax_color_from_srgb8(0x80FFFFFFu));
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0xFFFF0000u));
    ax_ir_rect(&e.b, ax_rect_make(16, 0, 16, 16), ax_color_from_srgb8(0x800000FFu));
    /* Below them, a half-covered pixel column: x = 8.25 at scale 2 puts the edge through pixel 16's
     * middle. */
    ax_ir_rect(&e.b, ax_rect_make(8.25f, 18, 4, 4), white());
    ir = finish(&e);
    if (ir == NULL) {
        env_deinit(&e);
        return;
    }
    got = render(&e, ir, 64, 48, 2.0f, &status, NULL);
    if (got != NULL) {
        const uint8_t *a = got + (8 * 64 + 8) * 4;
        const uint8_t *b = got + (8 * 64 + 40) * 4;
        const uint8_t *c = got + (40 * 64 + 16) * 4;
        AX_CHECK_EQ_U(status, AX_FRAME_PRESENTED);
        /* 50% of linear 1 is 0.5, encoded 188 (sRGB-space blending gives 128). */
        AX_CHECK(a[0] >= 186 && a[0] <= 190);
        AX_CHECK(b[0] >= 186 && b[0] <= 190);
        AX_CHECK_EQ_U(b[1], 0);
        AX_CHECK(b[2] >= 186 && b[2] <= 190);
        /* Coverage blends in linear light too: half a pixel of white over black is 188. */
        AX_CHECK(c[0] >= 186 && c[0] <= 190);
        free(got);
    }
    /* And the same as the software renderer's diagnostic. */
    check_scene_ex(&e, "linear_blend", ir, 64, 48, 2.0f, AX_RENDER_SOFT_DIAG_LINEAR_BLEND, NULL);
    env_deinit(&e);
}

AX_TEST(unknown_items_and_clips_follow_the_ir_rules)
{
    env e;
    const ax_render_ir *ir;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const uint8_t blob[12] = {0};
    ax_render_counts counts;
    ax_frame_status status;
    uint8_t *got;
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 48, 16, 0, ax_color_make(0, 0, 0, 1));
    ax_ir_rect(&e.b, ax_rect_make(1, 1, 6, 6), white());
    ax_ir_item(&e.b, 900, 0, blob, sizeof blob); /* unknown: skipped */
    ax_ir_rect(&e.b, ax_rect_make(9, 1, 6, 6), white());
    d.blend_mode = 77; /* unknown blend mode: drawn as normal */
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(17, 1, 6, 6), white());
    ax_ir_item(&e.b, 901, AX_IR_ITEM_REQUIRED, NULL, 0); /* the rest of this layer is skipped */
    ax_ir_rect(&e.b, ax_rect_make(25, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_PATH; /* a path clip it cannot apply: the layer is skipped */
    d.clip.path = 0;
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(33, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    d = ax_ir_layer_desc_default();
    d.clip.kind = AX_IR_CLIP_RECT; /* a rect clip is drawn: the left half of the rect shows */
    d.clip.rect = ax_rect_make(41, 0, 3, 16);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(41, 1, 6, 6), white());
    ax_ir_pop_layer(&e.b);
    ir = finish(&e);
    if (ir == NULL) {
        env_deinit(&e);
        return;
    }
    got = render(&e, ir, 48, 16, 1.0f, &status, &counts);
    if (got != NULL) {
        AX_CHECK_EQ_U(counts.unknown_items, 2);
        AX_CHECK_EQ_U(counts.skipped_items, 1);
        AX_CHECK_EQ_U(counts.skipped_layers, 1);
        AX_CHECK_EQ_U(counts.neutralized, 1);
        AX_CHECK_EQ_U(counts.instances, 4);
        AX_CHECK_EQ_U(got[(4 * 48 + 4) * 4], 255);  /* drawn */
        AX_CHECK_EQ_U(got[(4 * 48 + 12) * 4], 255); /* drawn after the unknown item */
        AX_CHECK_EQ_U(got[(4 * 48 + 20) * 4], 255); /* drawn with normal blending */
        AX_CHECK_EQ_U(got[(4 * 48 + 28) * 4], 0);   /* skipped after the REQUIRED item */
        AX_CHECK_EQ_U(got[(4 * 48 + 36) * 4], 0);   /* skipped: path clip */
        AX_CHECK_EQ_U(got[(4 * 48 + 42) * 4], 255); /* inside the rect clip */
        AX_CHECK_EQ_U(got[(4 * 48 + 45) * 4], 0);   /* outside it */
        free(got);
    }
    env_deinit(&e);
}

/* Clip chains (decision 93): axis-aligned rect clips nested in translated layers intersect into
 * one device rect on the CPU; rounded and turned clips each take a record, up to the chain's limit,
 * beyond which a clip is drawn as its bounding box and counted. The picture matches the software
 * renderer's within the tolerance while the chain fits. */
AX_TEST(nested_clips_intersect_and_long_chains_are_counted)
{
    env e;
    const ax_render_ir *ir;
    ax_render_counts counts;
    ax_frame_status status;
    uint8_t *got;
    int k;
    ENV_INIT(&e);
    /* Three translated rect clips and two rounded ones, one turned: no approximation. */
    ax_ir_builder_begin(&e.b, 96, 64, 0, ax_color_make(0.05f, 0.05f, 0.1f, 1));
    for (k = 0; k < 5; ++k) {
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        d.transform = k == 3 ? ax_affine2_mul(ax_affine2_translate(48, 32), ax_affine2_rotate(0.2f))
                             : ax_affine2_translate(2.5f, 1.75f);
        if (k == 3) {
            d.transform = ax_affine2_mul(d.transform, ax_affine2_translate(-40, -24));
        }
        d.clip.kind = k >= 3 ? AX_IR_CLIP_ROUNDED_RECT : AX_IR_CLIP_RECT;
        d.clip.rect = ax_rect_make(1.25f * (float)k, 0.5f * (float)k, 84.0f - 6.0f * (float)k,
                                   58.0f - 5.0f * (float)k);
        d.clip.radii[0] = d.clip.radii[1] = d.clip.radii[2] = d.clip.radii[3] = 7.5f;
        d.clip.path = AX_IR_NO_INDEX;
        ax_ir_push_layer(&e.b, &d);
    }
    for (k = 0; k < 12; ++k) {
        ax_ir_rect(&e.b, ax_rect_make(-10.0f + 9.0f * (float)k, -10.0f, 4.5f, 90.0f),
                   ax_color_make(1.0f, 0.8f - 0.05f * (float)k, 0.2f, 1.0f));
    }
    for (k = 0; k < 5; ++k) {
        ax_ir_pop_layer(&e.b);
    }
    ir = finish(&e);
    if (ir != NULL) {
        got = render(&e, ir, 96, 64, 1.0f, &status, &counts);
        if (got != NULL) {
            AX_CHECK_EQ_U(counts.skipped_layers, 0);
            AX_CHECK_EQ_U(counts.clips_approximated, 0);
            free(got);
        }
        check_scene(&e, "clip_chain", ir, 96, 64, 1.0f, NULL);
    }

    /* Six turned rounded clips: four in the chain, two drawn as their bounding boxes. */
    ax_ir_builder_begin(&e.b, 64, 64, 0, ax_color_make(0, 0, 0, 1));
    for (k = 0; k < 6; ++k) {
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        d.transform = ax_affine2_mul(ax_affine2_translate(32, 32), ax_affine2_rotate(0.1f));
        d.transform = ax_affine2_mul(d.transform, ax_affine2_translate(-32, -32));
        d.clip.kind = AX_IR_CLIP_ROUNDED_RECT;
        d.clip.rect = ax_rect_make(2.0f * (float)k, 2.0f * (float)k, 64.0f - 4.0f * (float)k,
                                   64.0f - 4.0f * (float)k);
        d.clip.radii[0] = d.clip.radii[1] = d.clip.radii[2] = d.clip.radii[3] = 6.0f;
        d.clip.path = AX_IR_NO_INDEX;
        ax_ir_push_layer(&e.b, &d);
    }
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 64, 64), white());
    for (k = 0; k < 6; ++k) {
        ax_ir_pop_layer(&e.b);
    }
    ir = finish(&e);
    if (ir != NULL) {
        got = render(&e, ir, 64, 64, 1.0f, &status, &counts);
        if (got != NULL) {
            AX_CHECK_EQ_U(counts.clips_approximated, 2);
            AX_CHECK_EQ_U(counts.instances, 1);
            free(got);
        }
    }
    env_deinit(&e);
}

AX_TEST(footprint_from_derivatives_matches)
{
    /* The path kept for transforms with perspective gives the same result on affine content. */
    env e;
    const ax_render_ir *ir;
    const float r[4] = {0.0f, 4.0f, 10.0f, 16.0f};
    ENV_INIT(&e);
    ax__render_wgpu_test_set_flags(e.r, RW_TEST_FOOTPRINT_FROM_DERIVATIVES);
    ax_ir_builder_begin(&e.b, 96, 64, 0, ax_color_make(0, 0, 0, 1));
    rotated(&e, 30.0f, 30.0f, 30.0f, 30.0f, 0.75f, white());
    rotated(&e, 7.0f, 30.0f, 40.0f, 30.0f, 12.0f, white());
    ax_ir_rounded_rect(&e.b, ax_rect_make(56.5f, 6.25f, 34.0f, 30.0f), r, white());
    ir = finish(&e);
    if (ir != NULL) {
        check_scene(&e, "derivatives", ir, 96, 64, 1.0f, NULL);
    }
    env_deinit(&e);
}

AX_TEST(frames_before_the_pipeline_is_ready_are_cleared)
{
    env e;
    const ax_render_ir *ir;
    ax_frame_status status = AX_FRAME_PRESENTED;
    uint8_t *got;
    ENV_INIT(&e);
    ax__render_wgpu_test_set_flags(e.r, RW_TEST_HOLD_PIPELINES);
    ax_ir_builder_begin(&e.b, 16, 16, 0, ax_color_from_srgb8(0xFF336699u));
    ax_ir_rect(&e.b, ax_rect_make(0, 0, 16, 16), white());
    ir = finish(&e);
    got = ir != NULL ? render(&e, ir, 16, 16, 1.0f, &status, NULL) : NULL;
    if (got != NULL) {
        AX_CHECK_EQ_U(status, AX_FRAME_CLEARED);
        AX_CHECK_EQ_U(got[0], 0x33); /* the snapshot's background, not its content */
        AX_CHECK_EQ_U(got[1], 0x66);
        AX_CHECK_EQ_U(got[2], 0x99);
        free(got);
    }
    env_deinit(&e);
}
