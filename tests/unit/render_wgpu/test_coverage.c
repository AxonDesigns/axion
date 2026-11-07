/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Coverage tests for ax_render_wgpu (decision 56): fixed scenes rendered offscreen and compared
 * pixel by pixel with coverage_reference.c, the provisional CPU reference (supersampled exact
 * area coverage, sRGB-space compositing), until ax_render_soft arrives in M1.
 *
 * Tolerances, per channel, on 8-bit values:
 *   - EDGE_TOLERANCE away from corners, where the renderer's coverage is exact (straight edges
 *     at any angle and slabs, including rects thinner than a pixel): 3/255 covers the 8-bit
 *     rounding of each blend and the reference's sampling error (at most 1/128 of the area);
 *   - CORNER_TOLERANCE near a rect's vertex under rotation or a rounded corner's arc, where it
 *     multiplies two slabs or treats the arc as straight;
 *   - MEAN_TOLERANCE for the mean error over the whole image.
 * Each test prints its measured errors. With AX_TEST_ARTIFACT_DIR set, every image is also written
 * there as <scene>.ppm, for CI to compare implementations with each other
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
#include "axion/render/ax_render_wgpu.h"
#include "coverage_reference.h"
#include "gpu_test_util.h"
#include "rw_testing.h"

#define EDGE_TOLERANCE   3.0
#define CORNER_TOLERANCE 20.0
#define MEAN_TOLERANCE   0.35

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
    double max_edge, max_corner, mean;
    uint32_t worst_x, worst_y;
} compare_result;

/* Renders, compares with the reference, checks the tolerances, prints the errors. */
static void check_scene(env *e, const char *scene, const ax_render_ir *ir, uint32_t w, uint32_t h,
                        float scale, compare_result *out)
{
    axt_reference ref;
    ax_frame_status status = AX_FRAME_NOTHING;
    uint8_t *got = render(e, ir, w, h, scale, &status, NULL);
    compare_result res = {0, 0, 0, 0, 0};
    double sum = 0.0;
    uint32_t i;

    if (got == NULL) {
        return;
    }
    AX_CHECK_EQ_U(status, AX_FRAME_PRESENTED);
    write_artifact(scene, got, w, h);
    if (!axt_reference_render(ir, w, h, scale, 128, &ref)) {
        ax_test_fail(__FILE__, __LINE__, "out of memory");
        free(got);
        return;
    }
    for (i = 0; i < w * h; ++i) {
        int k;
        for (k = 0; k < 4; ++k) {
            const double expected = floor((double)ref.rgba[i * 4 + k] * 255.0 + 0.5);
            const double err = fabs((double)got[i * 4 + k] - expected);
            sum += err;
            if (ref.corner[i]) {
                if (err > res.max_corner) {
                    res.max_corner = err;
                }
            } else if (err > res.max_edge) {
                res.max_edge = err;
                res.worst_x = i % w;
                res.worst_y = i / w;
            }
        }
    }
    res.mean = sum / (double)(w * h * 4);
    printf(
        "%s: max error %.0f/255 at edges (worst at %u,%u), %.0f/255 near corners, mean %.3f/255\n",
        scene, res.max_edge, res.worst_x, res.worst_y, res.max_corner, res.mean);
    if (res.max_edge > EDGE_TOLERANCE) {
        const uint8_t *p = got + ((size_t)res.worst_y * w + res.worst_x) * 4;
        const float *q = ref.rgba + ((size_t)res.worst_y * w + res.worst_x) * 4;
        ax_test_fail(__FILE__, __LINE__,
                     "%s: edge error %.0f/255 > %.0f at (%u, %u): got %u %u %u, expected %.1f "
                     "%.1f %.1f",
                     scene, res.max_edge, EDGE_TOLERANCE, res.worst_x, res.worst_y, p[0], p[1],
                     p[2], (double)q[0] * 255.0, (double)q[1] * 255.0, (double)q[2] * 255.0);
    }
    if (res.max_corner > CORNER_TOLERANCE) {
        ax_test_fail(__FILE__, __LINE__, "%s: corner error %.0f/255 > %.0f", scene, res.max_corner,
                     CORNER_TOLERANCE);
    }
    if (res.mean > MEAN_TOLERANCE) {
        ax_test_fail(__FILE__, __LINE__, "%s: mean error %.3f/255 > %.2f", scene, res.mean,
                     MEAN_TOLERANCE);
    }
    if (out != NULL) {
        *out = res;
    }
    axt_reference_free(&ref);
    free(got);
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

AX_TEST(axis_aligned_rects_at_fractional_positions)
{
    env e;
    const ax_render_ir *ir;
    int i;
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 96, 64, 0, ax_color_make(0, 0, 0, 1));
    for (i = 0; i < 8; ++i) {
        const float f = (float)i / 8.0f;
        ax_ir_rect(&e.b, ax_rect_make(2.0f + (float)i * 11.0f + f, 3.0f + f, 7.5f + f, 20.25f),
                   white());
        ax_ir_rect(&e.b, ax_rect_make(2.0f + (float)i * 11.0f, 30.0f + f * 0.5f, 6.0f + f, 3.3f),
                   ax_color_from_srgb8(0xFF4F7CFFu));
    }
    ir = finish(&e);
    if (ir != NULL) {
        check_scene(&e, "axis_aligned", ir, 96, 64, 1.0f, NULL);
    }
    env_deinit(&e);
}

AX_TEST(thin_rects_keep_their_area)
{
    /* Thinner than a pixel: drawn at their true area (a 0.25 px line is a 1 px line at 25%). */
    static const float widths[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f};
    env e;
    const ax_render_ir *ir;
    size_t i;
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 80, 64, 0, ax_color_make(0, 0, 0, 1));
    for (i = 0; i < AX_COUNTOF(widths); ++i) {
        const float x = 4.0f + (float)i * 12.0f + 0.3f;
        ax_ir_rect(&e.b, ax_rect_make(x, 4.0f, widths[i], 26.0f), white()); /* vertical */
        ax_ir_rect(&e.b, ax_rect_make(4.0f, 36.0f + (float)i * 4.0f + 0.6f, 70.0f, widths[i]),
                   white()); /* horizontal */
    }
    ir = finish(&e);
    if (ir != NULL) {
        check_scene(&e, "thin", ir, 80, 64, 1.0f, NULL);
    }
    env_deinit(&e);
}

static void rotated(env *e, float degrees, float x, float y, float w, float h, ax_color c)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    d.transform =
        ax_affine2_mul(ax_affine2_translate(x, y), ax_affine2_rotate(degrees * PI_F / 180));
    ax_ir_push_layer(&e->b, &d);
    ax_ir_rect(&e->b, ax_rect_make(-w * 0.5f, -h * 0.5f, w, h), c);
    ax_ir_pop_layer(&e->b);
}

AX_TEST(rotated_rects_have_smooth_edges)
{
    env e;
    const ax_render_ir *ir;
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 128, 96, 0, ax_color_make(0, 0, 0, 1));
    rotated(&e, 7.0f, 22.3f, 24.6f, 30.0f, 18.0f, white());
    rotated(&e, 30.0f, 64.0f, 24.0f, 28.0f, 20.0f, white());
    rotated(&e, 45.0f, 104.5f, 24.5f, 26.0f, 26.0f, white());
    rotated(&e, 30.0f, 32.0f, 70.0f, 50.0f, 0.5f, white()); /* a rotated hairline */
    rotated(&e, -60.0f, 80.0f, 70.0f, 40.0f, 1.25f, white());
    rotated(&e, 15.0f, 112.0f, 72.0f, 12.0f, 30.0f, ax_color_from_srgb8(0xFFE8ECF4u));
    ir = finish(&e);
    if (ir != NULL) {
        check_scene(&e, "rotated", ir, 128, 96, 1.0f, NULL);
    }
    env_deinit(&e);
}

AX_TEST(rounded_rects_with_radii_per_corner)
{
    env e;
    const ax_render_ir *ir;
    const float r1[4] = {0.0f, 4.0f, 10.0f, 16.0f};
    const float r2[4] = {12.0f, 12.0f, 12.0f, 12.0f};
    const float pill[4] = {100.0f, 100.0f, 100.0f, 100.0f}; /* clamped to half the height */
    const float tiny[4] = {0.5f, 1.0f, 1.5f, 2.0f};
    ENV_INIT(&e);
    ax_ir_builder_begin(&e.b, 128, 80, 0, ax_color_from_srgb8(0xFF1E2129u));
    ax_ir_rounded_rect(&e.b, ax_rect_make(4.25f, 4.5f, 52.0f, 36.0f), r1, white());
    ax_ir_rounded_rect(&e.b, ax_rect_make(64.6f, 4.2f, 30.0f, 30.0f), r2,
                       ax_color_from_srgb8(0xFF4F7CFFu));
    ax_ir_rounded_rect(&e.b, ax_rect_make(6.4f, 48.3f, 70.0f, 17.5f), pill, white());
    ax_ir_rounded_rect(&e.b, ax_rect_make(100.5f, 10.5f, 6.0f, 50.0f), tiny, white());
    ax_ir_rounded_rect(&e.b, ax_rect_make(84.0f, 46.0f, 36.0f, 28.0f), r1,
                       ax_color_from_srgb8(0xFFE8ECF4u));
    ir = finish(&e);
    if (ir != NULL) {
        check_scene(&e, "rounded", ir, 128, 80, 1.0f, NULL);
    }
    env_deinit(&e);
}

/* The same logical scene at fractional and integer device scales, with nested transforms. */
static const ax_render_ir *nested_scene(env *e, float lw, float lh)
{
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    const float r[4] = {3.0f, 6.0f, 0.0f, 9.0f};
    ax_ir_builder_begin(&e->b, lw, lh, 0, ax_color_from_srgb8(0xFF1E2129u));
    ax_ir_rect(&e->b, ax_rect_make(2.5f, 2.5f, 30.3f, 0.75f), white());
    d.transform = ax_affine2_translate(10.4f, 8.2f);
    ax_ir_push_layer(&e->b, &d);
    ax_ir_rounded_rect(&e->b, ax_rect_make(0, 0, 28.0f, 18.0f), r,
                       ax_color_from_srgb8(0xFF4F7CFFu));
    d.transform =
        ax_affine2_mul(ax_affine2_translate(48.0f, 6.0f),
                       ax_affine2_mul(ax_affine2_rotate(0.4f), ax_affine2_scale(1.5f, 0.75f)));
    ax_ir_push_layer(&e->b, &d);
    ax_ir_rounded_rect(&e->b, ax_rect_make(-8.0f, -6.0f, 16.0f, 12.0f), r, white());
    ax_ir_pop_layer(&e->b);
    ax_ir_pop_layer(&e->b);
    return finish(e);
}

AX_TEST(fractional_and_integer_device_scales)
{
    env e;
    const ax_render_ir *ir;
    ENV_INIT(&e);
    ir = nested_scene(&e, 80, 40);
    if (ir != NULL) {
        check_scene(&e, "scale_1_325", ir, 106, 53, 1.325f, NULL);
    }
    ir = nested_scene(&e, 80, 40);
    if (ir != NULL) {
        check_scene(&e, "scale_2", ir, 160, 80, 2.0f, NULL);
    }
    env_deinit(&e);
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
    d.clip.kind = AX_IR_CLIP_RECT; /* no clips in M0: the layer is skipped */
    d.clip.rect = ax_rect_make(0, 0, 100, 100);
    ax_ir_push_layer(&e.b, &d);
    ax_ir_rect(&e.b, ax_rect_make(33, 1, 6, 6), white());
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
        AX_CHECK_EQ_U(counts.instances, 3);
        AX_CHECK_EQ_U(got[(4 * 48 + 4) * 4], 255);  /* drawn */
        AX_CHECK_EQ_U(got[(4 * 48 + 12) * 4], 255); /* drawn after the unknown item */
        AX_CHECK_EQ_U(got[(4 * 48 + 20) * 4], 255); /* drawn with normal blending */
        AX_CHECK_EQ_U(got[(4 * 48 + 28) * 4], 0);   /* skipped after the REQUIRED item */
        AX_CHECK_EQ_U(got[(4 * 48 + 36) * 4], 0);   /* skipped: clipped layer */
        free(got);
    }
    check_scene(&e, "fallbacks", ir, 48, 16, 1.0f, NULL);
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
