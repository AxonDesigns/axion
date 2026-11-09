/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes --blend-compare's scene (issue #4): what each blending option changes, in a dark and a
 * light panel side by side: a fan of hairlines, rects rotated 3 degrees from 0.25 to 1.5 units
 * wide, text from 11 to 24 px, and translucent swatches (50 % white over black should read 128 as
 * in CSS). The renderer's blend mode decides how it all looks; the scene is the same.
 */
#ifndef M1_BLEND_H
#define M1_BLEND_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_txn.h"

/* Builds the scene under root, titled with the option's name, in one transaction (not animated).
 * App thread. */
ax_result m1_blend_build(ax_world world, ax_node root, const char *option, bool csd,
                         uint64_t *out_seq);

#endif /* M1_BLEND_H */
