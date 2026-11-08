/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: presenting CPU-drawn pixels into a surface (decision 84), for the software
 * renderer when no GPU is usable. The renderer draws into memory the windowing system can show
 * (Wayland: wl_shm buffers) without knowing which system it is:
 *
 *     ax_frame_target t = {sizeof t};
 *     ax_surface_begin_frame(s, &t);
 *     if (t.ready) {
 *         ax_cpu_buffer b = {sizeof b};
 *         if (ax_surface_cpu_acquire(s, &t, &b) == AX_OK) {
 *             draw b.width x b.height pixels into b.pixels;
 *             ax_surface_cpu_present(s, &t, &b);   // prepare_commit, attach, damage, commit
 *         }
 *     }
 *     ax_surface_wait_frame(s, timeout, &reason);
 *
 * Buffers are premultiplied, 8 bits per channel, in the byte order `format` gives, of the frame's
 * pixel size. A buffer belongs to the windowing system once presented, until it releases it; the
 * surface keeps a few and reuses those released. Their content is undefined when acquired (an
 * older frame): draw every pixel.
 *
 * thread: the surface's presenter, like ax_present.h.
 */
#ifndef AX_PLATFORM_PRESENT_CPU_H
#define AX_PLATFORM_PRESENT_CPU_H

#include <stddef.h>
#include <stdint.h>

#include "ax_present.h"

AX_EXTERN_C_BEGIN

/* ax_cpu_buffer.format: the byte order in memory. */
#define AX_CPU_FORMAT_BGRA8 1u /* B, G, R, A: Wayland's ARGB8888 on little-endian hosts */

#define AX_CPU_MAX_BUFFERS 4u /* per surface */

typedef struct ax_cpu_buffer {
    uint32_t struct_size; /* set by the caller */
    void *pixels;
    uint32_t width, height; /* the frame's pixel size */
    size_t stride;          /* bytes between rows */
    uint32_t format;        /* AX_CPU_FORMAT_* */
    uint32_t slot;          /* private: which of the surface's buffers */
} ax_cpu_buffer;

/*
 * A free buffer of the frame's pixel size (frame from ax_surface_begin_frame, ready).
 * Errors: AX_ERR_UNSUPPORTED (the backend cannot present CPU buffers, or the frame is not ready),
 * AX_ERR_OUT_OF_MEMORY (no memory, or every buffer is still held by the windowing system: skip
 * this frame and try again at the next one).
 */
AX_API ax_result ax_surface_cpu_acquire(ax_surface surface, const ax_frame_target *frame,
                                        ax_cpu_buffer *out);

/*
 * Prepares the commit for the frame (ax_surface_prepare_commit: the configure's acknowledgement,
 * the scale, the frame callback) and commits the buffer, the whole of it damaged. The buffer
 * must come from ax_surface_cpu_acquire with the same frame.
 * Errors: those of ax_surface_prepare_commit; AX_ERR_INVALID_ARGUMENT for a buffer that is not
 * the surface's.
 */
AX_API ax_result ax_surface_cpu_present(ax_surface surface, const ax_frame_target *frame,
                                        const ax_cpu_buffer *buffer);

/* Frees the surface's CPU buffers (a renderer detaching; the window stays). Buffers the windowing
 * system still shows stay valid on its side. */
AX_API void ax_surface_cpu_release(ax_surface surface);

AX_EXTERN_C_END

#endif
