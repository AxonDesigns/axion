/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (builds with the platform module): the platform presenter's options and counters.
 */
#ifndef AX_RUNTIME_PLATFORM_H
#define AX_RUNTIME_PLATFORM_H

#include <stdint.h>

#include "../ax_runtime_window.h"
#include "ax_runtime_sdk.h"
#include <axion/render/ax_renderer.h>

AX_EXTERN_C_BEGIN

typedef struct ax_rt_platform_desc {
    ax_app app;
    /* The renderer. NULL: the fallback chain, ax_render_wgpu when the gpu module is built, and
     * ax_render_soft (CPU buffers, decision 84) when it is not or when ax_render_wgpu cannot be
     * created (no usable GPU). ax_render_soft_vtbl() pins the software renderer. Created on the
     * render thread at the first window; ax_render_soft's tiles run on the runtime's jobs. */
    const ax_renderer_vtbl *renderer;
    const ax_renderer_desc *renderer_desc; /* NULL: defaults; copied, chain included */
    uint64_t max_wait_ns;                  /* the size handshake's bound; 0: decision 57's */
    ax_rt_scene_driver driver;             /* examples only (ax_runtime_sdk.h) */
    void *driver_user;
} ax_rt_platform_desc;

/* As ax_runtime_create_for_app, with the options. The renderer desc's chain must stay valid
 * until the first window is added. thread: app */
AX_API ax_result ax_rt_create_for_app(const ax_runtime_desc *desc, const ax_rt_platform_desc *pd,
                                      ax_runtime *out_runtime);

/* A window's presentation counters, from the render thread. thread: any */
typedef struct ax_rt_window_counters {
    uint64_t frames;          /* render thread frames that presented a snapshot */
    uint64_t snapshots_shown; /* distinct snapshots reported on screen */
    uint64_t presented;       /* the platform's presented frames (ax_present_stats) */
    uint64_t missed_vblanks;  /* the platform's missed refreshes while presenting */
    uint64_t scene_late, forced_acks, hidden_acks; /* decision 57's handshake */
    uint64_t refresh_ns;
    float shown_width, shown_height; /* logical size of the newest frame presented */
} ax_rt_window_counters;

/* Errors: AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (no such window). */
AX_API ax_result ax_rt_get_window_counters(ax_runtime rt, ax_window window,
                                           ax_rt_window_counters *out);

/* The renderer in use: the table of the one created (after a fallback, ax_render_soft's), NULL
 * until the first window is attached. thread: any */
AX_API const ax_renderer_vtbl *ax_rt_renderer_vtbl(ax_runtime rt);

/* The renderer, for the examples' statistics (NULL until the first window). thread: render */
AX_API void *ax_rt_renderer(ax_runtime rt);

AX_EXTERN_C_END

#endif /* AX_RUNTIME_PLATFORM_H */
