/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (for now): the parts of the renderer interface that need no window, shared by
 * axion/render/ax_renderer.h and the renderers' own headers.
 */
#ifndef AX_RENDER_RENDERER_TYPES_H
#define AX_RENDER_RENDERER_TYPES_H

#include <stdint.h>

#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

/* Extension structs chained from ax_renderer_desc.next start with this header. */
typedef struct ax_renderer_chain {
    const struct ax_renderer_chain *next;
    uint32_t type; /* AX_RENDERER_CHAIN_* */
} ax_renderer_chain;

#define AX_RENDERER_CHAIN_WGPU 1u /* ax_render_wgpu_desc (axion/render/ax_render_wgpu.h) */

/*
 * How a renderer blends (issue #4's comparison; every renderer draws them the same way). The default
 * does not change without the owner's approval:
 *   AX_RENDER_BLEND_SRGB            sRGB space, glyph coverage corrected for the text's luminance
 *                                   (decisions 53, 58 and 101). The default.
 *   AX_RENDER_BLEND_SRGB_CORRECTED  sRGB space, and the coverage of rects and rounded rects
 *                                   corrected for their color's luminance too (AX_IR_LUT_SHAPE_SRGB):
 *                                   translucency as CSS, edges as linear light would blend them
 *                                   against the opposite luminance.
 *   AX_RENDER_BLEND_LINEAR          linear light everywhere, glyphs with their own correction
 *                                   (AX_IR_LUT_TEXT_LINEAR).
 * In any mode, a layer with AX_IR_LAYER_LINEAR_BLEND blends its content in linear light (the
 * per-layer option), its glyphs with AX_IR_LUT_TEXT_LINEAR and its shapes uncorrected.
 */
typedef enum ax_render_blend {
    AX_RENDER_BLEND_SRGB = 0,
    AX_RENDER_BLEND_SRGB_CORRECTED = 1,
    AX_RENDER_BLEND_LINEAR = 2
} ax_render_blend;

typedef enum ax_frame_status {
    AX_FRAME_PRESENTED = 0,       /* the snapshot was drawn (and presented, for a surface) */
    AX_FRAME_CLEARED = 1,         /* only the background was drawn: pipelines not ready yet */
    AX_FRAME_NOTHING = 2,         /* nothing to do: not configured, not required, or skipped */
    AX_FRAME_CONTENT_MISMATCH = 3 /* the configure waits for a snapshot of its logical size */
} ax_frame_status;

/* What a renderer could not draw as recorded, per the Render IR's "Compatibility" rules, and how
 * much it drew. */
typedef struct ax_render_counts {
    uint32_t unknown_items;  /* items of unknown types, skipped */
    uint32_t skipped_items;  /* items skipped after an unknown AX_IR_ITEM_REQUIRED item */
    uint32_t skipped_layers; /* layers skipped (with their subtrees) for a clip it cannot apply */
    uint32_t neutralized;    /* layers drawn with normal blending or without their effects */
    uint32_t approximated;   /* layers whose group opacity was applied to each item */
    uint32_t invalid_items;  /* items with non-finite or degenerate geometry, skipped */
    uint32_t instances;      /* primitives drawn */
    uint32_t clips_approximated; /* clips beyond a renderer's chain limit, drawn as their
                                  * bounding box (ax_render_wgpu, decision 93) */
    uint32_t linear_layers; /* layers blended in linear light for AX_IR_LAYER_LINEAR_BLEND */
    uint32_t linear_layers_unsupported; /* such layers drawn in sRGB space: the target has no
                                         * sRGB view (ax_render_wgpu) */
} ax_render_counts;

AX_EXTERN_C_END

#endif
