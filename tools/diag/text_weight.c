/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compares the apparent weight of a KDE application's text (a capture by
 * tools/diag/kde_text_capture.sh) with AXION's rendering of the same text, for a range of text
 * coverage corrections (decision 101), to tune ax_ir_text_lut's parameters.
 *
 *   text_weight --capture kde.png --crop X,Y,W,H [--scale 1.325] [--size 13.333]
 *
 * Weight is ink: the sum, over the pixels of the text, of how far each pixel's linear-light
 * luminance moved from the background toward the text color (0 to 1 per pixel). It counts the
 * light a reader sees, so a blend that makes stems look heavier shows as more ink. The crop holds
 * the three lines of the sample and nothing else; the background is the crop's most common color
 * and the text color its farthest from it. AXION draws the same lines with the system's font
 * (fontconfig) at the same size (KDE's 10 pt is 13.333 logical pixels) and the same colors, once
 * per correction, and the table shows AXION's ink over KDE's (1 is the same weight).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/headless/ax_headless.h"
#include "axion/headless/ax_png.h"
#include "axion/render/ax_render_ir.h"
#include "axion/text/ax_text.h"

static const char *const LINES[3] = {"The quick brown fox jumps over the lazy dog",
                                     "Sphinx of black quartz, judge my vow",
                                     "0123456789 office AV To Wa"};

static double lin(uint8_t v)
{
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luma(const uint8_t *p)
{
    return 0.2126 * lin(p[0]) + 0.7152 * lin(p[1]) + 0.0722 * lin(p[2]);
}

static int cmp(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

typedef struct region {
    const uint8_t *rgba;
    size_t stride;
    uint32_t x, y, w, h;
} region;

/* The most common color (the background) and the farthest from it in luminance (the text). */
static void colors(const region *r, uint8_t bg[4], uint8_t fg[4])
{
    uint32_t best = 0, i, j;
    double far = -1.0, lb;
    uint32_t *keys = (uint32_t *)malloc(sizeof(uint32_t) * r->w * r->h);
    uint32_t n = 0;
    for (j = 0; j < r->h; ++j) {
        for (i = 0; i < r->w; ++i) {
            const uint8_t *p = r->rgba + (r->y + j) * r->stride + (r->x + i) * 4u;
            keys[n++] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
    {
        /* Mode by counting in a sorted copy. */
        uint32_t run = 1, top = 0;
        qsort(keys, n, sizeof(uint32_t), cmp);
        for (i = 1; i <= n; ++i) {
            if (i < n && keys[i] == keys[i - 1u]) {
                run++;
            } else {
                if (run > top) {
                    top = run;
                    best = keys[i - 1u];
                }
                run = 1;
            }
        }
    }
    free(keys);
    bg[0] = (uint8_t)(best >> 16);
    bg[1] = (uint8_t)(best >> 8);
    bg[2] = (uint8_t)best;
    bg[3] = 255;
    lb = luma(bg);
    for (j = 0; j < r->h; ++j) {
        for (i = 0; i < r->w; ++i) {
            const uint8_t *p = r->rgba + (r->y + j) * r->stride + (r->x + i) * 4u;
            const double d = fabs(luma(p) - lb);
            if (d > far) {
                far = d;
                memcpy(fg, p, 4);
            }
        }
    }
    fg[3] = 255;
}

static double ink(const region *r, const uint8_t bg[4], const uint8_t fg[4])
{
    const double lb = luma(bg), lf = luma(fg);
    double sum = 0.0;
    uint32_t i, j;
    for (j = 0; j < r->h; ++j) {
        for (i = 0; i < r->w; ++i) {
            double t = (luma(r->rgba + (r->y + j) * r->stride + (r->x + i) * 4u) - lb) / (lf - lb);
            sum += t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        }
    }
    return sum;
}

/* AXION's rendering of the sample with a correction; its ink. */
static double axion_ink(float scale, float size, const uint8_t bg[4], const uint8_t fg[4],
                        const ax_ir_text_lut_params *lp, const char *save)
{
    ax_headless *h = NULL;
    ax_headless_desc d = ax_headless_default_desc();
    ax_headless_surface_desc s = ax_headless_default_surface_desc();
    ax_headless_image img;
    ax_node root;
    ax_txn *t;
    ax_font_source *fc = NULL;
    ax_text_system *ts = NULL;
    ax_text_system_desc td;
    region r;
    double result;
    uint32_t i;
    ax_ir_text_lut_override(lp);
    d.renderer = AX_HEADLESS_RENDER_SOFT;
    s.width = 700.0f;
    s.height = 120.0f;
    s.scale = scale;
    if (ax_headless_create(&d, &h) != AX_OK) {
        return -1.0;
    }
    memset(&td, 0, sizeof td);
    if (ax_font_source_fontconfig_create(NULL, &fc) == AX_OK) {
        td.sources = &fc;
        td.source_count = 1;
        td.own_sources = true;
    }
    if (ax_text_system_create(&td, &ts) == AX_OK) {
        ax_world_adopt_text_system(ax_headless_world(h), ts);
    }
    if (ax_headless_add_surface(h, &s, &root) != AX_OK) {
        ax_headless_destroy(h);
        return -1.0;
    }
    t = ax_txn_begin(ax_headless_world(h));
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb8(bg[0], bg[1], bg[2], 255));
    ax_txn_set_color(t, root, AX_PROP_COLOR, ax_rgba_from_srgb8(fg[0], fg[1], fg[2], 255));
    ax_txn_set_f32(t, root, AX_PROP_FONT_SIZE, size);
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
    if (save != NULL) {
        (void)ax_png_write_file(save, img.rgba, img.width, img.height, img.stride,
                                AX_PNG_PREMULTIPLIED);
    }
    r.rgba = img.rgba;
    r.stride = img.stride;
    r.x = r.y = 0;
    r.w = img.width;
    r.h = img.height;
    result = ink(&r, bg, fg);
    ax_headless_image_free(h, &img);
    ax_headless_destroy(h);
    ax_ir_text_lut_override(NULL);
    return result;
}

int main(int argc, char **argv)
{
    const char *capture = NULL, *save = NULL;
    unsigned cx = 0, cy = 0, cw = 0, ch = 0;
    float scale = 1.0f, size = 40.0f / 3.0f;
    ax_image img;
    region r;
    uint8_t bg[4], fg[4];
    double kde;
    static const float gammas[] = {1.0f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f, 2.2f};
    static const float contrasts[] = {0.0f, 0.1f, 0.2f, 0.3f, 0.5f};
    ax_ir_text_lut_params best = {0.0f, 2.2f};
    double best_err = 1e9;
    int i;
    size_t gi, ci;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            capture = argv[++i];
        } else if (strcmp(argv[i], "--crop") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%u,%u,%u,%u", &cx, &cy, &cw, &ch) != 4) {
                return 2;
            }
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            size = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        }
    }
    if (capture == NULL || cw == 0 || ch == 0) {
        fprintf(stderr, "usage: text_weight --capture kde.png --crop X,Y,W,H [--scale S] "
                        "[--size PX] [--save axion.png]\n");
        return 2;
    }
    if (ax_png_read_file(NULL, capture, &img) != AX_OK || cx + cw > img.width ||
        cy + ch > img.height) {
        fprintf(stderr, "cannot read %s or the crop is outside it\n", capture);
        return 1;
    }
    r.rgba = img.rgba;
    r.stride = (size_t)img.width * 4u;
    r.x = cx;
    r.y = cy;
    r.w = cw;
    r.h = ch;
    colors(&r, bg, fg);
    kde = ink(&r, bg, fg);
    printf("KDE: background #%02X%02X%02X, text #%02X%02X%02X, ink %.1f\n", bg[0], bg[1], bg[2],
           fg[0], fg[1], fg[2], kde);
    printf("AXION ink / KDE ink, by gamma (rows) and contrast (columns):\n       ");
    for (ci = 0; ci < sizeof contrasts / sizeof contrasts[0]; ++ci) {
        printf("  c=%.1f", (double)contrasts[ci]);
    }
    printf("\n");
    for (gi = 0; gi < sizeof gammas / sizeof gammas[0]; ++gi) {
        printf("g=%.1f  ", (double)gammas[gi]);
        for (ci = 0; ci < sizeof contrasts / sizeof contrasts[0]; ++ci) {
            ax_ir_text_lut_params lp;
            double ratio;
            lp.gamma = gammas[gi];
            lp.contrast = contrasts[ci];
            ratio = axion_ink(scale, size, bg, fg, &lp, NULL) / kde;
            printf("  %5.3f", ratio);
            if (fabs(ratio - 1.0) < best_err) {
                best_err = fabs(ratio - 1.0);
                best = lp;
            }
        }
        printf("\n");
    }
    printf("closest: gamma %.1f, contrast %.1f\n", (double)best.gamma, (double)best.contrast);
    if (save != NULL) {
        (void)axion_ink(scale, size, bg, fg, &best, save);
    }
    ax_image_free(NULL, &img);
    return 0;
}
