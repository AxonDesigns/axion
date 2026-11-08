/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Presenting CPU-drawn buffers (axion/platform/ax_present_cpu.h, decision 84): checks and the
 * backend's operations.
 */
#include <string.h>

#include "axion/platform/ax_present_cpu.h"
#include "platform_internal.h"

ax_result ax_surface_cpu_acquire(ax_surface surface, const ax_frame_target *frame,
                                 ax_cpu_buffer *out)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    if (out == NULL || out->struct_size < sizeof(ax_cpu_buffer) || frame == NULL ||
        frame->struct_size < sizeof(ax_frame_target)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_surface_cpu_acquire: struct_size");
    }
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_surface_cpu_acquire: invalid surface");
    }
    if (w->app->ops->cpu_acquire == NULL) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_surface_cpu_acquire: this backend cannot present CPU buffers");
    }
    if (!frame->ready || frame->pixel_width == 0 || frame->pixel_height == 0) {
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_surface_cpu_acquire: the frame is not ready");
    }
    return w->app->ops->cpu_acquire(w, frame->pixel_width, frame->pixel_height, out);
}

ax_result ax_surface_cpu_present(ax_surface surface, const ax_frame_target *frame,
                                 const ax_cpu_buffer *buffer)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    ax_result r;
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_surface_cpu_present: invalid surface");
    }
    if (buffer == NULL || w->app->ops->cpu_present == NULL || buffer->slot >= AX_CPU_MAX_BUFFERS ||
        frame == NULL || buffer->width != frame->pixel_width ||
        buffer->height != frame->pixel_height) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "ax_surface_cpu_present: not a buffer acquired for this frame");
    }
    r = ax_surface_prepare_commit(surface, frame);
    if (r != AX_OK) {
        return r;
    }
    w->app->ops->cpu_present(w, buffer->slot);
    return AX_OK;
}

void ax_surface_cpu_release(ax_surface surface)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    if (w != NULL && w->app->ops->cpu_release != NULL) {
        w->app->ops->cpu_release(w);
    }
}
