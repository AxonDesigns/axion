/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Measures issue #4's blending options on ax_render_soft (which ax_render_wgpu matches within the
 * parity tolerance, decision 83), for the recommendation:
 *
 *   default   AX_RENDER_BLEND_SRGB: sRGB space, glyphs corrected (decision 101);
 *   1         AX_RENDER_BLEND_SRGB_CORRECTED: shapes corrected too;
 *   2         AX_RENDER_BLEND_LINEAR: linear light, glyphs with their own correction;
 *   3         the default, with the content in a layer with AX_IR_LAYER_LINEAR_BLEND.
 *
 * Three measures:
 *   - roping (decision 58's table): a line rotated 3 degrees from vertical, 0.33 to 1.99 device
 *     pixels wide, #E8ECF4 on #1E2129, the reverse, and #E8ECF4 on mid gray #6E7480; per scanline, the light the line adds to
 *     (or takes from) the background, in linear luminance; max/min over the scanlines (1.00: even);
 *   - text weight: the three lines of tools/diag/text_weight in the bundled Noto Sans at 13.333 px,
 *     at scale 1 and 1.325, light on dark and dark on light (the same two colors); ink as
 *     text_weight counts it; the ratio light/dark (1: the same weight in both themes) and each
 *     polarity's ink over the default's (the default matched KDE's within 1-4 %, decision 101);
 *   - translucency against CSS (sRGB-space blending of 8-bit colors): 50 % white over black, and
 *     the OKLab distance of a set of mixes from CSS's result (mean and max).
 *
 * Text in a layer with AX_IR_LAYER_LINEAR_BLEND is drawn exactly as with AX_RENDER_BLEND_LINEAR
 * (the same table and blend), so option 3's text is option 2's; its shapes and translucency are
 * measured inside such a layer.
 *
 *   build/diag/tools/diag/blend_measure [--tune-linear-text]
 *
 * --tune-linear-text sweeps AX_IR_LUT_TEXT_LINEAR's gamma and contrast and prints how far text in
 * linear light is from the default's weight, in both polarities and at both scales.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_math.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/headless/ax_headless.h"
#include "axion/render/ax_render_ir.h"
#include "axion/render/ax_render_soft.h"

typedef struct option {
    const char *name;
    uint32_t blend;     /* ax_render_blend */
    bool linear_layer;  /* the content in an AX_IR_LAYER_LINEAR_BLEND layer */
    uint32_t text_from; /* the option whose text it draws (option 3: option 2's) */
} option;

static const option OPTIONS[4] = {
    {"default (sRGB)", AX_RENDER_BLEND_SRGB, false, 0},
    {"1: sRGB, shapes corrected", AX_RENDER_BLEND_SRGB_CORRECTED, false, 1},
    {"2: linear", AX_RENDER_BLEND_LINEAR, false, 2},
    {"3: linear layer", AX_RENDER_BLEND_SRGB, true, 2},
};

static const char *const LINES[3] = {"The quick brown fox jumps over the lazy dog",
                                     "Sphinx of black quartz, judge my vow",
                                     "0123456789 office AV To Wa"};

static const uint32_t LIGHT = 0xFFE8ECF4u, DARK = 0xFF1E2129u, MID = 0xFF6E7480u;

static double lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luma(const uint8_t *p)
{
    return 0.2126 * lin(p[0]) + 0.7152 * lin(p[1]) + 0.0722 * lin(p[2]);
}

static void rgb8(uint32_t argb, uint8_t out[4])
{
    out[0] = (uint8_t)(argb >> 16);
    out[1] = (uint8_t)(argb >> 8);
    out[2] = (uint8_t)argb;
    out[3] = 255;
}

/* Draws ir with the option into a fresh RGBA8 image (w x h at scale 1). */
static uint8_t *draw(const option *o, const ax_render_ir *ir, uint32_t w, uint32_t h)
{
    ax_render_soft_desc d;
    ax_render_soft *r = NULL;
    ax_soft_target t;
    uint8_t *px = (uint8_t *)malloc((size_t)w * h * 4u);
    memset(&d, 0, sizeof d);
    d.blend = o->blend;
    memset(&t, 0, sizeof t);
    t.struct_size = sizeof t;
    t.pixels = px;
    t.width = w;
    t.height = h;
    t.stride = (size_t)w * 4u;
    t.format = AX_SOFT_RGBA8;
    t.scale = 1.0f;
    if (px == NULL || ax_render_soft_create(NULL, &d, &r) != AX_OK ||
        ax_render_soft_draw_ir(r, &t, ir, NULL) != AX_OK) {
        free(px);
        px = NULL;
    }
    ax_render_soft_destroy(r);
    return px;
}

static void begin_content(ax_ir_builder *b, const option *o)
{
    if (o->linear_layer) {
        ax_ir_layer_desc d = ax_ir_layer_desc_default();
        d.flags = AX_IR_LAYER_LINEAR_BLEND;
        ax_ir_push_layer(b, &d);
    }
}

static void end_content(ax_ir_builder *b, const option *o)
{
    if (o->linear_layer) {
        ax_ir_pop_layer(b);
    }
}

/* ---- roping ----------------------------------------------------------------------------------- */

#define ROPE_W 24u
#define ROPE_H 120u

static double roping(const option *o, float width, uint32_t fg, uint32_t bg)
{
    ax_ir_builder b;
    const ax_render_ir *ir = NULL;
    ax_ir_layer_desc d = ax_ir_layer_desc_default();
    uint8_t *img;
    uint8_t bg8[4];
    double lo = 1e30, hi = 0.0, lb;
    uint32_t x, y;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, (float)ROPE_W, (float)ROPE_H, 0, ax_color_from_srgb8(bg));
    begin_content(&b, o);
    d.transform = ax_affine2_mul(ax_affine2_translate((float)ROPE_W * 0.5f + 0.3f,
                                                      (float)ROPE_H * 0.5f),
                                 ax_affine2_rotate(3.0f * 3.14159265f / 180.0f));
    ax_ir_push_layer(&b, &d);
    ax_ir_rect(&b, ax_rect_make(-width * 0.5f, -400.0f, width, 800.0f), ax_color_from_srgb8(fg));
    ax_ir_pop_layer(&b);
    end_content(&b, o);
    if (ax_ir_builder_end(&b, &ir) != AX_OK || (img = draw(o, ir, ROPE_W, ROPE_H)) == NULL) {
        ax_ir_builder_deinit(&b);
        return -1.0;
    }
    rgb8(bg, bg8);
    lb = luma(bg8);
    for (y = 0; y < ROPE_H; ++y) {
        double sum = 0.0;
        for (x = 0; x < ROPE_W; ++x) {
            sum += fabs(luma(img + ((size_t)y * ROPE_W + x) * 4u) - lb);
        }
        lo = sum < lo ? sum : lo;
        hi = sum > hi ? sum : hi;
    }
    free(img);
    ax_ir_builder_deinit(&b);
    return lo > 0.0 ? hi / lo : -1.0;
}

/* ---- text weight -------------------------------------------------------------------------- */

static double text_ink(const option *o, float scale, uint32_t fg, uint32_t bg)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    ax_headless_image img;
    ax_node root;
    ax_txn *t;
    uint8_t fg8[4], bg8[4];
    double lf, lb, sum = 0.0;
    uint32_t i, x, y;
    d.renderer = AX_HEADLESS_RENDER_SOFT;
    d.blend = o->blend;
    s.width = 700.0f;
    s.height = 120.0f;
    s.scale = scale;
    if (ax_headless_create(&d, &h) != AX_OK || ax_headless_add_surface(h, &s, &root) != AX_OK) {
        ax_headless_destroy(h);
        return -1.0;
    }
    rgb8(fg, fg8);
    rgb8(bg, bg8);
    t = ax_txn_begin(ax_headless_world(h));
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR,
                     ax_rgba_from_srgb8(bg8[0], bg8[1], bg8[2], 255));
    ax_txn_set_color(t, root, AX_PROP_COLOR, ax_rgba_from_srgb8(fg8[0], fg8[1], fg8[2], 255));
    ax_txn_set_f32(t, root, AX_PROP_FONT_SIZE, 13.333f);
    {
        ax_dimension pad = {8.0f, AX_UNIT_POINTS};
        ax_txn_set_dimension(t, root, AX_PROP_PADDING, pad);
    }
    ax_txn_set_u32(t, root, AX_PROP_ALIGN_ITEMS, AX_ALIGN_FLEX_START);
    for (i = 0; i < 3; ++i) {
        ax_node n = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_str(t, n, AX_PROP_TEXT, LINES[i], strlen(LINES[i]));
        ax_txn_append_child(t, root, n);
    }
    (void)ax_txn_commit(t, NULL);
    (void)ax_headless_settle(h, 20, NULL);
    if (ax_headless_capture(h, root, &img) != AX_OK) {
        ax_headless_destroy(h);
        return -1.0;
    }
    lf = luma(fg8);
    lb = luma(bg8);
    for (y = 0; y < img.height; ++y) {
        for (x = 0; x < img.width; ++x) {
            const double v = (luma(img.rgba + (size_t)y * img.stride + (size_t)x * 4u) - lb) /
                             (lf - lb);
            sum += v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        }
    }
    ax_headless_image_free(h, &img);
    ax_headless_destroy(h);
    return sum;
}

/* ---- translucency against CSS ----------------------------------------------------------------- */

typedef struct mix {
    uint32_t fg, bg; /* opaque sRGB colors */
    uint8_t alpha;
} mix;

static const mix MIXES[] = {
    {0xFFFFFFFFu, 0xFF000000u, 128}, {0xFF000000u, 0xFFFFFFFFu, 128}, {0xFFFF0000u, 0xFF00FF00u, 128},
    {0xFF0000FFu, 0xFFFFFF00u, 128}, {0xFF3DAEE9u, 0xFF232629u, 64},  {0xFF3DAEE9u, 0xFF232629u, 128},
    {0xFF3DAEE9u, 0xFF232629u, 191}, {0xFF3DAEE9u, 0xFFFFFFFFu, 128}, {0xFFDA4453u, 0xFFEFF0F1u, 51},
    {0xFF000000u, 0xFFEFF0F1u, 26},
};
#define MIX_COUNT (sizeof MIXES / sizeof MIXES[0])

static void oklab(const double rgb_lin[3], double out[3])
{
    const double l = 0.4122214708 * rgb_lin[0] + 0.5363325363 * rgb_lin[1] + 0.0514459929 * rgb_lin[2];
    const double m = 0.2119034982 * rgb_lin[0] + 0.6806995451 * rgb_lin[1] + 0.1073969566 * rgb_lin[2];
    const double s = 0.0883024619 * rgb_lin[0] + 0.2817188376 * rgb_lin[1] + 0.6299787005 * rgb_lin[2];
    const double l3 = cbrt(l), m3 = cbrt(m), s3 = cbrt(s);
    out[0] = 0.2104542553 * l3 + 0.7936177850 * m3 - 0.0040720468 * s3;
    out[1] = 1.9779984951 * l3 - 2.4285922050 * m3 + 0.4505937099 * s3;
    out[2] = 0.0259040371 * l3 + 0.7827717662 * m3 - 0.8086757660 * s3;
}

static double delta_e(const uint8_t a[3], const double b_srgb[3])
{
    double la[3], lb[3], oa[3], ob[3];
    int k;
    for (k = 0; k < 3; ++k) {
        la[k] = lin(a[k]);
        lb[k] = b_srgb[k] <= 0.04045 ? b_srgb[k] / 12.92 : pow((b_srgb[k] + 0.055) / 1.055, 2.4);
    }
    oklab(la, oa);
    oklab(lb, ob);
    return sqrt((oa[0] - ob[0]) * (oa[0] - ob[0]) + (oa[1] - ob[1]) * (oa[1] - ob[1]) +
                (oa[2] - ob[2]) * (oa[2] - ob[2]));
}

static void translucency(const option *o, uint32_t *white50, double *mean, double *max)
{
    ax_ir_builder b;
    const ax_render_ir *ir = NULL;
    uint8_t *img;
    uint32_t i;
    *white50 = 0;
    *mean = *max = -1.0;
    ax_ir_builder_init(&b, ax_default_allocator());
    ax_ir_builder_begin(&b, (float)(MIX_COUNT * 8u), 8.0f, 0, ax_color_make(0, 0, 0, 1));
    for (i = 0; i < MIX_COUNT; ++i) {
        ax_ir_rect(&b, ax_rect_make((float)(i * 8u), 0, 8, 8), ax_color_from_srgb8(MIXES[i].bg));
    }
    begin_content(&b, o);
    for (i = 0; i < MIX_COUNT; ++i) {
        const uint32_t c = (MIXES[i].fg & 0x00FFFFFFu) | ((uint32_t)MIXES[i].alpha << 24);
        ax_ir_rect(&b, ax_rect_make((float)(i * 8u), 0, 8, 8), ax_color_from_srgb8(c));
    }
    end_content(&b, o);
    if (ax_ir_builder_end(&b, &ir) == AX_OK && (img = draw(o, ir, MIX_COUNT * 8u, 8)) != NULL) {
        double sum = 0.0, worst = 0.0;
        for (i = 0; i < MIX_COUNT; ++i) {
            const uint8_t *p = img + (4u * MIX_COUNT * 8u + i * 8u + 4u) * 4u; /* row 4 */
            uint8_t f[4], g[4];
            double css[3], de;
            const double a = MIXES[i].alpha / 255.0;
            int k;
            rgb8(MIXES[i].fg, f);
            rgb8(MIXES[i].bg, g);
            for (k = 0; k < 3; ++k) {
                css[k] = (f[k] * a + g[k] * (1.0 - a)) / 255.0;
            }
            de = delta_e(p, css);
            sum += de;
            worst = de > worst ? de : worst;
            if (i == 0) {
                *white50 = p[0];
            }
        }
        *mean = sum / (double)MIX_COUNT;
        *max = worst;
        free(img);
    }
    ax_ir_builder_deinit(&b);
}

static void tune_linear_text(void)
{
    static const float gammas[] = {1.7f, 1.75f, 1.8f, 1.85f, 1.9f};
    static const float contrasts[] = {0.05f, 0.1f, 0.15f};
    static const float destinations[] = {1.0f};
    uint32_t dd;
    double base[2][2];
    uint32_t g, c, k;
    for (k = 0; k < 2; ++k) {
        base[k][0] = text_ink(&OPTIONS[0], k == 0 ? 1.0f : 1.325f, LIGHT, DARK);
        base[k][1] = text_ink(&OPTIONS[0], k == 0 ? 1.0f : 1.325f, DARK, LIGHT);
    }
    printf("| Gamma | Contrast | Destination | LoD x1 | DoL x1 | LoD x1.325 | DoL x1.325 | worst |\n");
    printf("| --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (dd = 0; dd < sizeof destinations / sizeof destinations[0]; ++dd)
    for (g = 0; g < sizeof gammas / sizeof gammas[0]; ++g) {
        for (c = 0; c < sizeof contrasts / sizeof contrasts[0]; ++c) {
            ax_ir_text_lut_params lp;
            double r[4], worst = 0.0;
            lp.gamma = gammas[g];
            lp.contrast = contrasts[c];
            lp.destination = destinations[dd];
            ax_ir_lut_override(AX_IR_LUT_TEXT_LINEAR, &lp);
            for (k = 0; k < 2; ++k) {
                const float scale = k == 0 ? 1.0f : 1.325f;
                r[2 * k] = text_ink(&OPTIONS[2], scale, LIGHT, DARK) / base[k][0];
                r[2 * k + 1] = text_ink(&OPTIONS[2], scale, DARK, LIGHT) / base[k][1];
            }
            for (k = 0; k < 4; ++k) {
                worst = fabs(r[k] - 1.0) > worst ? fabs(r[k] - 1.0) : worst;
            }
            printf("| %.2f | %.2f | %.1f | %.3f | %.3f | %.3f | %.3f | %.3f |\n", lp.gamma,
                   lp.contrast, lp.destination, r[0], r[1], r[2], r[3], worst);
        }
    }
    ax_ir_lut_override(AX_IR_LUT_TEXT_LINEAR, NULL);
}

int main(int argc, char **argv)
{
    static const float widths[4] = {0.33f, 0.66f, 1.325f, 1.99f};
    double ink[3][2][2]; /* text option x scale x polarity (0: light on dark) */
    uint32_t i, k;

    if (argc > 1 && strcmp(argv[1], "--tune-linear-text") == 0) {
        tune_linear_text();
        return 0;
    }
    printf("Roping: max/min of the light per scanline (1.00 is even)\n\n");
    printf("| Option | Polarity | 0.33 px | 0.66 px | 1.325 px | 1.99 px |\n");
    printf("| --- | --- | --- | --- | --- | --- |\n");
    for (i = 0; i < 4; ++i) {
        for (k = 0; k < 3; ++k) {
            /* The third: a background the corrections cannot assume (neither black nor white). */
            static const char *const names[3] = {"light on dark", "dark on light",
                                                 "light on mid gray"};
            const uint32_t fg = k == 1 ? DARK : LIGHT;
            const uint32_t bg = k == 0 ? DARK : (k == 1 ? LIGHT : MID);
            uint32_t w;
            printf("| %s | %s |", OPTIONS[i].name, names[k]);
            for (w = 0; w < 4; ++w) {
                printf(" %.2f |", roping(&OPTIONS[i], widths[w], fg, bg));
            }
            printf("\n");
        }
    }

    for (i = 0; i < 3; ++i) {
        for (k = 0; k < 2; ++k) {
            const float scale = k == 0 ? 1.0f : 1.325f;
            ink[i][k][0] = text_ink(&OPTIONS[i], scale, LIGHT, DARK);
            ink[i][k][1] = text_ink(&OPTIONS[i], scale, DARK, LIGHT);
        }
    }
    printf("\nText weight (ink), Noto Sans 13.333 px: light/dark ratio (1: the same weight in both "
           "themes), and each polarity over the default's\n\n");
    printf("| Option | Scale | Light / dark | Light on dark vs default | Dark on light vs default |\n");
    printf("| --- | --- | --- | --- | --- |\n");
    for (i = 0; i < 4; ++i) {
        const uint32_t t = OPTIONS[i].text_from;
        for (k = 0; k < 2; ++k) {
            printf("| %s | %s | %.3f | %.3f | %.3f |\n", OPTIONS[i].name, k == 0 ? "1" : "1.325",
                   ink[t][k][0] / ink[t][k][1], ink[t][k][0] / ink[0][k][0],
                   ink[t][k][1] / ink[0][k][1]);
        }
    }

    printf("\nTranslucency against CSS (sRGB-space blending of the 8-bit colors), %u mixes\n\n",
           (unsigned)MIX_COUNT);
    printf("| Option | 50%% white over black | OKLab dE from CSS, mean | max |\n");
    printf("| --- | --- | --- | --- |\n");
    for (i = 0; i < 4; ++i) {
        uint32_t w50;
        double mean, max;
        translucency(&OPTIONS[i], &w50, &mean, &max);
        printf("| %s | %u | %.4f | %.4f |\n", OPTIONS[i].name, w50, mean, max);
    }
    return 0;
}
