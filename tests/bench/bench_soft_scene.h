/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The software renderer's benchmark scene (tests/bench/bench_soft.c, decision 87): m1_nodes'
 * picture before M1's fourth prompt (m0_window's scene as a node tree), kept here so the
 * benchmark's numbers stay comparable, and its motion as one transaction per frame.
 */
#ifndef M1_SCENE_H
#define M1_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"

/* As m0_window (examples/m0_window/m0_window.h). */
#define CAPTION_HEIGHT 32.0f
#define BORDER         8.0f
#define CORNER         16.0f
#define BAR_SPEED      400.0
#define BAR_TRACK      1200.0
#define COLOR_BG       0xFF1E2129u
#define COLOR_CAPTION  0xFF2D3240u
#define COLOR_EDGE     0xFF4F7CFFu
#define COLOR_FG       0xFFE8ECF4u
#define COLOR_BAND     0xFF262A33u
#define COLOR_CORNER   0xFF3A4152u

/* The nodes m1_scene_move moves. The app thread fills them before it commits the tree and
 * then sets `ready` (release); the driver reads them only after it sees `ready` (acquire). */
typedef struct m1_moving {
    ax_atomic_u32 ready;
    bool csd;
    float left, top;
    ax_node turning, fan, knob, tick, bar;
    ax_node band_left, band_right, band_bottom, caption;
    ax_node corner_tr, corner_bl, corner_br;
    ax_node edge_top, edge_bottom, edge_left, edge_right;
} m1_moving;

/* The sRGB color 0xAARRGGBB as a node color. */
ax_rgba m1_c8(uint32_t argb);

/* m0_window's scene (examples/m0_window/scene.c) as nodes under root, for a w x h window. Sets
 * m (the nodes the driver moves) and m->ready. Returns the first rounded rect, whose color the
 * example changes every second. App thread. */
ax_node m1_scene_build(ax_world world, ax_node root, m1_moving *m, float w, float h,
                       uint64_t *out_seq);

/* Moves the scene to its state at time_ns: one transaction, not animated. App thread. */
void m1_scene_move(ax_world world, m1_moving *m, uint64_t time_ns, float w, float h);

#endif
