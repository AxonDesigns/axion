/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m0_window: what the two renderers (shared memory, Render IR) share.
 */
#ifndef M0_WINDOW_H
#define M0_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "axion/ax_platform.h"
#include "axion/base/ax_atomic.h"
#include "axion/platform/ax_present.h"

typedef enum m0_renderer { M0_RENDERER_SHM, M0_RENDERER_IR } m0_renderer;

typedef struct options {
    uint32_t decorations;
    bool stats;
    double duration; /* seconds; 0 = until closed */
    float width, height;
    m0_renderer renderer;
    int present_mode;     /* ir: WGPUPresentMode, 0 = the pacing's own */
    int pacing;           /* ir: ax_gpu_pacing, 0 = default */
    int adapter;          /* ir: ax_gpu_adapter_kind */
    uint64_t max_wait_ns; /* ir: bound of the size handshake's wait, 0 = default */
} options;

typedef struct shared {
    options opt;
    ax_app app;
    ax_window window;
    ax_surface surface;
    ax_atomic_u32 stop;
    ax_atomic_u32 draw_failed;
    uint64_t start_ns;
} shared;

static inline bool m0_stopping(shared *s)
{
    return ax_atomic_load_u32(&s->stop, AX_MO_ACQUIRE) != 0;
}

/* ---- what is drawn: the same picture on both renderers ----------------------------------------
 */

#define M0_CAPTION_HEIGHT 32.0f /* logical units */
#define M0_BORDER         8.0f
#define M0_CORNER         16.0f

#define M0_BAR_SPEED 400.0  /* logical units per second */
#define M0_BAR_TRACK 1200.0 /* logical units per lap, whatever the window size */

#define M0_COLOR_BG      0xFF1E2129u /* XRGB8888, sRGB */
#define M0_COLOR_CAPTION 0xFF2D3240u
#define M0_COLOR_EDGE    0xFF4F7CFFu
#define M0_COLOR_BAR     0xFFE8ECF4u

typedef struct m0_layout {
    int32_t caption_px; /* 0 when the compositor draws the frame */
    int32_t edge_px;
    int32_t bar_x0, bar_x1; /* the bar, in pixels; it spans caption_px .. height - bottom */
    int32_t bar_bottom_px;  /* pixels kept free under the bar (the bottom edge) */
} m0_layout;

/* The bar moves at M0_BAR_SPEED logical units per second along a track of fixed length, timed by
 * the predicted present time: its position depends only on time, never on the window's size or
 * scale, so resizing never makes it jump, and a dropped or late frame shows as a hitch. */
void m0_layout_frame(const ax_frame_target *t, bool csd, m0_layout *out);

/* ---- statistics --------------------------------------------------------------------------------
 */

typedef struct m0_draw_stats {
    uint64_t frames;
    uint64_t sum_ns, max_ns; /* CPU time spent drawing (shm: painting; gpu: encoding + submit) */
} m0_draw_stats;

void m0_draw_stats_add(m0_draw_stats *d, uint64_t ns);

/* One "stats:" line from the platform's presentation statistics (reset after reading) and the
 * renderer's draw times. `extra` is appended (renderer-specific), may be NULL. */
void m0_print_stats(shared *s, m0_draw_stats *draw, const char *extra);

/* Totals over the run, printed at exit. */
void m0_print_totals(void);

/* ---- renderers: each is the surface's presenter thread ----------------------------------------
 */

int m0_shm_draw_thread(void *arg);
#if defined(M0_HAVE_IR)
int m0_ir_draw_thread(void *arg); /* starts the scene thread too */
#endif

#endif
