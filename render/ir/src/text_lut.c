/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The correction of glyph coverage for the text's luminance (decision 101), shared by every
 * renderer. Skia's mask gamma, with AXION's parameters:
 *
 *   For a text luminance L (one of AX_IR_TEXT_LUT_BUCKETS, from black to white), the destination is
 *   assumed (the blend cannot read it): the opposite luminance 1 - L, or with `destination` toward
 *   1, black under light text and white under dark text. For each coverage c:
 *     c' = c + (1 - c) * contrast * (1 - L) * c       contrast, stronger for dark text
 *     the blend a space of exponent `gamma` gives (values v = linear^(1/gamma)):
 *         out = (L^(1/g) c' + (1 - L)^(1/g) (1 - c'))^g
 *     the alpha that gives `out` when blended in the renderer's space: (out - D) / (S - D),
 *     with S and D the text and destination luminances in that space.
 *   gamma 1 imitates linear light; about 2.2 is close to the renderer's own sRGB space (almost no
 *   change). Where text and destination are too close to tell (|S - D| < 1/256), only the contrast
 *   applies.
 *
 * In sRGB space (the default blending, decision 58) S and D are L and 1 - L encoded; for a
 * renderer blending in linear light (AX_IR_LUT_TEXT_LINEAR) they are L and 1 - L. Everything uses base's deterministic exp2 and log2, so the table is the same bytes
 * on every platform.
 */
#include <axion/base/ax_math.h>
#include <axion/render/ax_render_ir.h>

static ax_ir_text_lut_params g_override[AX_IR_LUT_TABLES];
static int g_overridden[AX_IR_LUT_TABLES];

void ax_ir_lut_override(ax_ir_lut_table table, const ax_ir_text_lut_params *params)
{
    if ((unsigned)table >= AX_IR_LUT_TABLES) {
        return;
    }
    g_overridden[table] = params != NULL;
    if (params != NULL) {
        g_override[table] = *params;
    }
}

void ax_ir_text_lut_override(const ax_ir_text_lut_params *params)
{
    ax_ir_lut_override(AX_IR_LUT_TEXT_SRGB, params);
}

ax_ir_text_lut_params ax_ir_lut_default(ax_ir_lut_table table)
{
    ax_ir_text_lut_params p;
    if ((unsigned)table < AX_IR_LUT_TABLES && g_overridden[table]) {
        return g_override[table];
    }
    if (table == AX_IR_LUT_SHAPE_SRGB) {
        /* Shapes (option 1 of issue #4): the blend linear light gives, against black or white (the
         * roping of light lines on dark halves again over the opposite luminance's). */
        p.contrast = 0.0f;
        p.gamma = 1.0f;
        p.destination = 1.0f;
        return p;
    }
    if (table == AX_IR_LUT_TEXT_LINEAR) {
        /* Text blended in linear light keeps the weight it has in sRGB space (KDE's, decision
         * 101), within 0.4 % in both polarities at scales 1 and 1.325 (tools/diag/blend_measure
         * --tune-linear-text): a gamma space against black or white backgrounds. */
        p.contrast = 0.05f;
        p.gamma = 1.85f;
        p.destination = 1.0f;
        return p;
    }
    /* Tuned against KDE's text on both of the owner's monitors and in both polarities
     * (tools/diag/kde_text_capture.py and text_weight; decision 101): KDE blends glyph coverage
     * in sRGB space with almost no correction. */
    p.contrast = 0.0f;
    p.gamma = 2.3f;
    p.destination = 0.0f;
    return p;
}

static float powf_det(float x, float y)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    return ax_exp2f(y * ax_log2f(x));
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

ax_ir_text_lut_params ax_ir_text_lut_default(void)
{
    return ax_ir_lut_default(AX_IR_LUT_TEXT_SRGB);
}

/* `linear`: the renderer blends in linear light, so S and D are the luminances themselves. */
static void build(const ax_ir_text_lut_params *params, bool linear,
                  uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256])
{
    const float g = params->gamma > 0.01f ? params->gamma : 1.0f;
    uint32_t bucket, i;
    for (bucket = 0; bucket < AX_IR_TEXT_LUT_BUCKETS; ++bucket) {
        const float lin_src = (float)bucket / (float)(AX_IR_TEXT_LUT_BUCKETS - 1u);
        const float extreme = lin_src > 0.5f ? 0.0f : 1.0f;
        const float dest = clamp01(params->destination);
        const float lin_dst = (1.0f - dest) * (1.0f - lin_src) + dest * extreme;
        const float s = linear ? lin_src : ax_linear_to_srgb(lin_src);
        const float d = linear ? lin_dst : ax_linear_to_srgb(lin_dst);
        const float contrast = params->contrast * lin_dst;
        const float vs = powf_det(lin_src, 1.0f / g), vd = powf_det(lin_dst, 1.0f / g);
        for (i = 0; i < 256u; ++i) {
            const float raw = (float)i / 255.0f;
            const float c = raw + (1.0f - raw) * contrast * raw;
            float a;
            if (s - d < 1.0f / 256.0f && d - s < 1.0f / 256.0f) {
                a = c;
            } else {
                const float lin_out = powf_det(vs * c + vd * (1.0f - c), g);
                const float out = linear ? lin_out : ax_linear_to_srgb(lin_out);
                a = (out - d) / (s - d);
            }
            lut[bucket][i] = (uint8_t)(clamp01(a) * 255.0f + 0.5f);
        }
        lut[bucket][0] = 0;
        lut[bucket][255] = 255;
    }
}

void ax_ir_text_lut_build(const ax_ir_text_lut_params *params,
                          uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256])
{
    build(params, false, lut);
}

void ax_ir_lut_build(ax_ir_lut_table table, const ax_ir_text_lut_params *params,
                     uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256])
{
    build(params, table == AX_IR_LUT_TEXT_LINEAR, lut);
}

uint32_t ax_ir_text_lut_bucket(ax_color c)
{
    float y;
    uint32_t b;
    if (!(c.a > 0.0f)) {
        return 0;
    }
    /* Luminance of the straight color, linear light (Rec. 709). */
    y = (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / c.a;
    y = clamp01(y);
    b = (uint32_t)(y * (float)(AX_IR_TEXT_LUT_BUCKETS - 1u) + 0.5f);
    return b < AX_IR_TEXT_LUT_BUCKETS ? b : AX_IR_TEXT_LUT_BUCKETS - 1u;
}
