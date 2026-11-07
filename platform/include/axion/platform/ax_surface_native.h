/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a surface's native windowing-system handles, for the GPU layer (to create a
 * WebGPU or Vulkan surface) and for code that presents without a GPU (tests, examples).
 *
 * Pointers are untyped so this header does not pull in any windowing system's headers.
 */
#ifndef AX_PLATFORM_SURFACE_NATIVE_H
#define AX_PLATFORM_SURFACE_NATIVE_H

#include <stdint.h>

#include "../ax_surface.h"

AX_EXTERN_C_BEGIN

typedef enum ax_native_kind { AX_NATIVE_NONE = 0, AX_NATIVE_WAYLAND = 1 } ax_native_kind;

typedef struct ax_surface_native {
    uint32_t struct_size; /* set by the caller: sizeof(ax_surface_native) */
    uint32_t kind;        /* ax_native_kind */
    union {
        struct {
            void *display; /* struct wl_display * */
            void *surface; /* struct wl_surface * */
            /* struct wl_event_queue * the surface's presenter dispatches in
             * ax_surface_wait_frame. Objects whose events the presenter wants (wl_buffer release,
             * for example) are created on this queue, through a proxy wrapper. */
            void *queue;
        } wayland;
        void *reserved[8];
    } u;
} ax_surface_native;

/* Valid until the window is destroyed. thread: any */
AX_API ax_result ax_surface_get_native(ax_surface surface, ax_surface_native *out_native);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_SURFACE_NATIVE_H */
