/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: a minimal upload ring for per-frame data (instances, uniforms).
 *
 *     ax_gpu_upload_ring_begin_frame(ring);              // once per frame, before any push
 *     ax_gpu_upload_slice s;
 *     ax_gpu_upload_ring_push(ring, data, size, 16, &s); // copy data into a GPU buffer
 *     wgpuRenderPassEncoderSetVertexBuffer(pass, 0, s.buffer, s.offset, s.size);
 *
 * Data reaches the buffer through wgpuQueueWriteBuffer, which the queue orders after the work
 * submitted before it. A frame can therefore reuse the bytes the previous frame used, as soon as
 * that frame was submitted: each frame starts again at offset 0, and nothing waits for the GPU.
 * When a frame needs more than the buffer holds, a buffer twice as large replaces it; slices
 * already handed out stay valid until the next begin_frame. After a device loss the buffer is
 * recreated on the new device at the next begin_frame.
 *
 * A mapped staging ring (no extra copy inside the implementation) comes with the render graph;
 * this one only avoids creating buffers every frame. Thread: the ax_gpu's ("presenter").
 */
#ifndef AX_GPU_UPLOAD_H
#define AX_GPU_UPLOAD_H

#include <stdint.h>

#include "ax_gpu.h"

AX_EXTERN_C_BEGIN

typedef struct ax_gpu_upload_ring ax_gpu_upload_ring;

typedef struct ax_gpu_upload_slice {
    WGPUBuffer buffer; /* owned by the ring; valid until the next begin_frame */
    uint64_t offset;
    uint64_t size;
} ax_gpu_upload_slice;

typedef struct ax_gpu_upload_stats {
    uint32_t struct_size; /* set by the caller */
    uint64_t capacity;    /* bytes in the current buffer */
    uint64_t frame_bytes; /* bytes pushed since the last begin_frame */
    uint64_t peak_bytes;  /* largest frame so far */
    uint32_t grows;       /* buffers replaced by larger ones */
    uint32_t recreations; /* buffers recreated after a device loss */
} ax_gpu_upload_stats;

/*
 * `usage` is what the slices are bound as (WGPUBufferUsage_Vertex, _Uniform, _Storage...);
 * CopyDst is added. `initial_size` 0 selects 64 KiB.
 * Errors: AX_ERR_OUT_OF_MEMORY, AX_ERR_DEVICE_LOST (the buffer could not be created).
 * thread: presenter
 */
AX_API ax_result ax_gpu_upload_ring_create(ax_gpu *gpu, WGPUBufferUsage usage,
                                           uint64_t initial_size, ax_gpu_upload_ring **out_ring);

/* NULL is ignored. thread: presenter */
AX_API void ax_gpu_upload_ring_destroy(ax_gpu_upload_ring *ring);

/* Starts a frame: releases buffers retired by growth, and recreates the buffer if the device
 * changed. Errors: AX_ERR_DEVICE_LOST (no buffer on the current device). thread: presenter */
AX_API ax_result ax_gpu_upload_ring_begin_frame(ax_gpu_upload_ring *ring);

/*
 * Copies `size` bytes (a multiple of 4, as wgpuQueueWriteBuffer requires) to an offset aligned
 * to `align` (a power of two, at least 4) and describes where they went.
 * Errors: AX_ERR_OUT_OF_MEMORY or AX_ERR_DEVICE_LOST when a larger buffer cannot be created.
 * thread: presenter
 */
AX_API ax_result ax_gpu_upload_ring_push(ax_gpu_upload_ring *ring, const void *data, uint64_t size,
                                         uint64_t align, ax_gpu_upload_slice *out_slice);

/* thread: presenter */
AX_API void ax_gpu_upload_ring_get_stats(ax_gpu_upload_ring *ring, ax_gpu_upload_stats *out);

AX_EXTERN_C_END

#endif
