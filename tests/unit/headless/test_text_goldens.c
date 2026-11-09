/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Goldens of text (decisions 82, 99 to 101): paragraphs of the bundled Noto Sans at several sizes
 * and widths, drawn headless by ax_render_soft at scales 1 and 1.325 (the owner's two monitors),
 * dark on light and light on dark. The software renderer is deterministic, so these images are
 * the same bytes on every platform (the determinism job checks it, decision 86).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/headless/ax_headless.h"
#include "axt_golden.h"
#if defined(AXT_HAVE_GPU)
#    include "gpu_test_util.h"
#endif

static const char PARAGRAPH[] =
    "AXION lays text out with HarfBuzz and draws it from an atlas of coverage glyphs. "
    "Office fi fl ffi, AV To Wa, 0123456789.";

typedef struct column {
    float size;  /* font size */
    float width; /* the paragraph's box */
} column;

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* A page of paragraphs, one per (size, width), in a column. */
static ax_headless *page_with(uint32_t renderer, float scale, bool dark, float width, float height,
                              const column *cols, uint32_t count, ax_node *root)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    ax_txn *t;
    uint32_t i;
    d.renderer = renderer;
    s.width = width;
    s.height = height;
    s.scale = scale;
    if (ax_headless_create(&d, &h) != AX_OK) {
        return NULL; /* the caller decides (no GPU renderer here) */
    }
    if (ax_headless_add_surface(h, &s, root) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "headless: %s", ax_last_error()->message.ptr);
        if (h != NULL) {
            ax_headless_destroy(h);
        }
        return NULL;
    }
    t = ax_txn_begin(ax_headless_world(h));
    ax_txn_set_color(t, *root, AX_PROP_BACKGROUND_COLOR,
                     dark ? ax_rgba_from_srgb8(0x1E, 0x21, 0x29, 255)
                          : ax_rgba_from_srgb8(0xFA, 0xFA, 0xFA, 255));
    ax_txn_set_color(t, *root, AX_PROP_COLOR,
                     dark ? ax_rgba_from_srgb8(0xE8, 0xEC, 0xF4, 255)
                          : ax_rgba_from_srgb8(0x20, 0x22, 0x28, 255));
    ax_txn_set_dimension(t, *root, AX_PROP_PADDING, pt(10.0f));
    ax_txn_set_dimension(t, *root, AX_PROP_ROW_GAP, pt(8.0f));
    ax_txn_set_u32(t, *root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    for (i = 0; i < count; ++i) {
        ax_node n = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_str(t, n, AX_PROP_TEXT, PARAGRAPH, sizeof PARAGRAPH - 1u);
        ax_txn_set_f32(t, n, AX_PROP_FONT_SIZE, cols[i].size);
        ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(cols[i].width));
        ax_txn_append_child(t, *root, n);
    }
    if (ax_txn_commit(t, NULL) != AX_OK || ax_headless_settle(h, 50, NULL) != AX_OK) {
        ax_test_fail(__FILE__, __LINE__, "headless: %s", ax_last_error()->message.ptr);
        ax_headless_destroy(h);
        return NULL;
    }
    return h;
}

static ax_headless *page(float scale, bool dark, float width, float height, const column *cols,
                         uint32_t count, ax_node *root)
{
    ax_headless *h =
        page_with(AX_HEADLESS_RENDER_SOFT, scale, dark, width, height, cols, count, root);
    if (h == NULL) {
        ax_test_fail(__FILE__, __LINE__, "headless: %s", ax_last_error()->message.ptr);
    }
    return h;
}

static void check(ax_headless *h, ax_node root, const char *name)
{
    ax_headless_image img;
    AX_REQUIRE(ax_headless_capture(h, root, &img) == AX_OK);
    AXT_CHECK_GOLDEN("text", name, img.rgba, img.width, img.height, img.stride);
    ax_headless_image_free(h, &img);
}

static const column SMALL[] = {{11.0f, 300.0f}, {13.0f, 300.0f}, {16.0f, 300.0f},
                               {20.0f, 300.0f}};

AX_TEST(paragraphs_at_small_sizes)
{
    ax_node root;
    ax_headless *h = page(1.0f, false, 320.0f, 560.0f, SMALL, 4, &root);
    if (h == NULL) {
        return;
    }
    check(h, root, "small_light_1x");
    ax_headless_destroy(h);
    h = page(1.0f, true, 320.0f, 560.0f, SMALL, 4, &root);
    if (h == NULL) {
        return;
    }
    check(h, root, "small_dark_1x");
    ax_headless_destroy(h);
    h = page(1.325f, false, 320.0f, 560.0f, SMALL, 4, &root);
    if (h == NULL) {
        return;
    }
    check(h, root, "small_light_1325");
    ax_headless_destroy(h);
}

static const column WIDTHS[] = {{14.0f, 120.0f}, {14.0f, 200.0f}, {14.0f, 380.0f}};
static const column LARGE[] = {{28.0f, 380.0f}, {48.0f, 380.0f}};

AX_TEST(paragraphs_wrap_at_several_widths_and_large_sizes)
{
    ax_node root;
    ax_headless *h = page(1.0f, false, 400.0f, 420.0f, WIDTHS, 3, &root);
    if (h == NULL) {
        return;
    }
    check(h, root, "widths");
    ax_headless_destroy(h);
    h = page(1.0f, false, 400.0f, 460.0f, LARGE, 2, &root);
    if (h == NULL) {
        return;
    }
    check(h, root, "large");
    ax_headless_destroy(h);
}

/* ---- Parity with ax_render_wgpu (decision 83's measures) -------------------------------------- */

static double srgb_to_linear(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

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

/* The same page drawn by both renderers: dE (OKLab) at the 99.9th percentile within 0.02 and on
 * average within 0.002, as the coverage tests (decision 83). */
static void compare_renderers(float scale, bool dark, const char *what)
{
    ax_node rs, rg;
    ax_headless *soft = page(scale, dark, 320.0f, 560.0f, SMALL, 4, &rs);
    ax_headless *gpu =
        page_with(AX_HEADLESS_RENDER_WGPU, scale, dark, 320.0f, 560.0f, SMALL, 4, &rg);
    ax_headless_image a, b;
    double *de, sum = 0.0, p999;
    size_t n, i;
    if (gpu == NULL) {
        if (soft != NULL) {
            ax_headless_destroy(soft);
        }
        AX_SKIP("no GPU renderer in this build or on this machine");
    }
    AX_REQUIRE(soft != NULL);
    AX_REQUIRE(ax_headless_capture(soft, rs, &a) == AX_OK);
    AX_REQUIRE(ax_headless_capture(gpu, rg, &b) == AX_OK);
    AX_REQUIRE(a.width == b.width && a.height == b.height);
    n = (size_t)a.width * a.height;
    de = (double *)malloc(sizeof(double) * n);
    AX_REQUIRE(de != NULL);
    for (i = 0; i < n; ++i) {
        const size_t y = i / a.width, x = i % a.width;
        double la[3], lb[3];
        oklab(a.rgba + y * a.stride + x * 4u, la);
        oklab(b.rgba + y * b.stride + x * 4u, lb);
        de[i] = sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) +
                     (la[2] - lb[2]) * (la[2] - lb[2]));
        sum += de[i];
    }
    qsort(de, n, sizeof(double), cmp_double);
    p999 = de[(size_t)((double)(n - 1u) * 0.999)];
    printf("text %s: dE p99.9 %.4f, mean %.5f, max %.4f\n", what, p999, sum / (double)n,
           de[n - 1u]);
    AX_CHECK(p999 <= 0.02);
    AX_CHECK(sum / (double)n <= 0.002);
    free(de);
    ax_headless_image_free(soft, &a);
    ax_headless_image_free(gpu, &b);
    ax_headless_destroy(gpu);
    ax_headless_destroy(soft);
}

AX_TEST(the_gpu_renderer_draws_text_like_the_software_one)
{
#if defined(AXT_HAVE_GPU)
    axt_gpu_pin_drivers();
#endif
    compare_renderers(1.0f, false, "light 1x");
    compare_renderers(1.0f, true, "dark 1x");
    compare_renderers(1.325f, false, "light 1.325x");
}
