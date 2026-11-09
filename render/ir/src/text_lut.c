/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The correction of glyph coverage for the text's luminance (decision 101), shared by every
 * renderer. Skia's mask gamma, with AXION's parameters:
 *
 *   For a text luminance L (one of AX_IR_TEXT_LUT_BUCKETS, from black to white), the destination is
 *   assumed to be the opposite luminance 1 - L (the blend cannot read it). For each coverage c:
 *     c' = c + (1 - c) * contrast * (1 - L) * c       contrast, stronger for dark text
 *     the blend a space of exponent `gamma` gives (values v = linear^(1/gamma)):
 *         out = (L^(1/g) c' + (1 - L)^(1/g) (1 - c'))^g
 *     the alpha that gives `out` when blended in the renderer's space: (out - D) / (S - D),
 *     with S and D the text and destination luminances in that space.
 *   gamma 1 imitates linear light; about 2.2 is close to the renderer's own sRGB space (almost no
 *   change). Where text and destination are too close to tell (|S - D| < 1/256), only the contrast
 *   applies.
 *
 * The renderer's space is sRGB-encoded (the default blending, decision 58), so S and D are L and
 * 1 - L encoded. Everything uses base's deterministic exp2 and log2, so the table is the same bytes
 * on every platform.
 */
#include <axion/base/ax_math.h>
#include <axion/render/ax_render_ir.h>

static ax_ir_text_lut_params g_override;
static int g_overridden;

void ax_ir_text_lut_override(const ax_ir_text_lut_params *params)
{
    g_overridden = params != NULL;
    if (params != NULL) {
        g_override = *params;
    }
}

ax_ir_text_lut_params ax_ir_text_lut_default(void)
{
    ax_ir_text_lut_params p;
    if (g_overridden) {
        return g_override;
    }
    /* Tuned against KDE's text on both of the owner's monitors and in both polarities
     * (tools/diag/kde_text_capture.py and text_weight; decision 101): KDE blends glyph coverage
     * in sRGB space with almost no correction. */
    p.contrast = 0.0f;
    p.gamma = 2.3f;
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

void ax_ir_text_lut_build(const ax_ir_text_lut_params *params,
                          uint8_t lut[AX_IR_TEXT_LUT_BUCKETS][256])
{
    const float g = params->gamma > 0.01f ? params->gamma : 1.0f;
    uint32_t bucket, i;
    for (bucket = 0; bucket < AX_IR_TEXT_LUT_BUCKETS; ++bucket) {
        const float lin_src = (float)bucket / (float)(AX_IR_TEXT_LUT_BUCKETS - 1u);
        const float lin_dst = 1.0f - lin_src;
        const float s = ax_linear_to_srgb(lin_src);
        const float d = ax_linear_to_srgb(lin_dst);
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
                const float out = ax_linear_to_srgb(lin_out);
                a = (out - d) / (s - d);
            }
            lut[bucket][i] = (uint8_t)(clamp01(a) * 255.0f + 0.5f);
        }
        lut[bucket][0] = 0;
        lut[bucket][255] = 255;
    }
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
