/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The animator's internals (decision 94). Implicit animations run on the scene thread: a change
 * of an animatable property, or of a frame, starts or retargets an animation of its channel. Every
 * channel's presentation value is its target plus an offset that goes to zero (a spring's
 * displacement, a decay's remaining distance, or the sum of timing curves' remaining deltas), so a
 * new target or a target that moves every frame (a relayout at an interpolated size) never jumps.
 */
#ifndef AX_CORE_ANIM_INTERNAL_H
#define AX_CORE_ANIM_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/core/ax_anim.h>
#include <axion/core/ax_node_store.h>

/* Channels: what animates on a node. */
enum {
    AXA_FRAME = 0,      /* x, y, w, h: the frame in the parent's space */
    AXA_BACKGROUND = 1, /* r, g, b, a: linear light, premultiplied */
    AXA_RADIUS = 2,     /* 2..5: one per corner, as the properties are */
    AXA_OPACITY = 6,
    AXA_TRANSFORM = 7, /* tx, ty, angle, scale x, scale y, skew (CSS's 2D decomposition) */
    AXA_CHANNELS = 8
};

#define AXA_MAX_COMPONENTS 6
#define AXA_CONTRIBUTIONS  3 /* timing curves blended at once on a channel */

typedef struct axa_track {
    uint8_t kind;  /* 0: idle; AX_TRANSITION_SPRING, _DECAY or _TIMING */
    uint8_t count; /* TIMING: contributions */
    uint16_t curve[AXA_CONTRIBUTIONS];
    uint64_t t0[AXA_CONTRIBUTIONS];                 /* ns, world clock */
    float d[AXA_CONTRIBUTIONS][AXA_MAX_COMPONENTS]; /* TIMING: deltas; else [0]: x0 */
    float v0[AXA_MAX_COMPONENTS];                   /* SPRING: initial velocity */
    float off[AXA_MAX_COMPONENTS];                  /* offset at the last sample */
    float vel[AXA_MAX_COMPONENTS];                  /* velocity at the last sample */
} axa_track;

/* A node's animations (a side component of animating nodes only). */
typedef struct axa_node {
    uint32_t active; /* bit per channel */
    uint32_t stamp_mask;
    uint64_t stamp_seq; /* the transaction whose first change of each channel was handled */
    axa_track track[AXA_CHANNELS];
    /* Presentation values at the last sample (paint and composite; the frame is target + off). */
    ax_color background;
    float radii[4];
    float opacity;
    ax_affine2 transform;
} axa_node;

/* A node's own transitions (a side component): interned curve + 1 per channel, 0 for none. */
typedef struct axa_transitions {
    uint16_t curve[AXA_CHANNELS];
} axa_transitions;

/* The channel of a core property (AX_PROP_LAYOUT for the frame); -1 when it does not animate. */
int axa_channel_of(ax_prop prop);

/* CSS's 2D decomposition of an affine transform into the transform channel's components, and
 * back. */
void axa_decompose(ax_affine2 m, float out[AXA_MAX_COMPONENTS]);
ax_affine2 axa_recompose(const float c[AXA_MAX_COMPONENTS]);

#endif /* AX_CORE_ANIM_INTERNAL_H */
