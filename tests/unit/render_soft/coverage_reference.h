/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * A supersampled CPU reference: draws a Render IR snapshot's rects and rounded rects by
 * supersampling each pixel's exact area coverage, and composites them in sRGB space with
 * premultiplied alpha. It was the GPU renderer's provisional reference (decision 56); since
 * ax_render_soft became the reference (decision 81) it tests the software renderer itself.
 * Clipped layers are skipped and layer opacity multiplies each item, as in M0's renderer.
 */
#ifndef AXT_COVERAGE_REFERENCE_H
#define AXT_COVERAGE_REFERENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/render/ax_render_ir.h"

/* Per pixel: the expected color (sRGB-encoded, premultiplied, floats) and whether the pixel is
 * near a corner (a rect's vertex or a rounded corner's arc), where the renderer's coverage is an
 * approximation; elsewhere it is exact. */
typedef struct axt_reference {
    uint32_t width, height;
    float *rgba;     /* width * height * 4 */
    uint8_t *corner; /* width * height */
} axt_reference;

/* Renders `ir` at `scale` device pixels per logical unit. `samples` per axis for edge pixels
 * (128: 16384 samples, within 1/256 of the exact area for a straight edge). Returns false when out
 * of memory. */
bool axt_reference_render(const ax_render_ir *ir, uint32_t width, uint32_t height, float scale,
                          uint32_t samples, axt_reference *out);
void axt_reference_free(axt_reference *ref);

#endif
