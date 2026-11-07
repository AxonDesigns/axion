/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_gpu_upload_ring: per-frame data through wgpuQueueWriteBuffer (ax_gpu_upload.h).
 */
#include <string.h>

#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/gpu/ax_gpu_upload.h>

#include "gpu_internal.h"

enum { MAX_RETIRED = 8 };

struct ax_gpu_upload_ring {
    ax_gpu *gpu;
    WGPUBufferUsage usage;
    WGPUBuffer buffer;
    uint32_t generation; /* device generation the buffer belongs to */
    uint64_t capacity;
    uint64_t head;                   /* bytes used this frame */
    WGPUBuffer retired[MAX_RETIRED]; /* replaced this frame; slices may still point at them */
    uint32_t retired_count;
    ax_gpu_upload_stats stats;
};

static ax_result create_buffer(ax_gpu_upload_ring *r, uint64_t capacity)
{
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    WGPUBuffer b;
    desc.usage = r->usage | WGPUBufferUsage_CopyDst;
    desc.size = capacity;
    b = wgpuDeviceCreateBuffer(r->gpu->device, &desc);
    if (b == NULL) {
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu_upload_ring: cannot create a buffer");
    }
    r->buffer = b;
    r->capacity = capacity;
    r->generation = r->gpu->generation;
    r->stats.capacity = capacity;
    return AX_OK;
}

ax_result ax_gpu_upload_ring_create(ax_gpu *gpu, WGPUBufferUsage usage, uint64_t initial_size,
                                    ax_gpu_upload_ring **out_ring)
{
    ax_gpu_upload_ring *r;
    ax_result res;
    AX_GPU_ASSERT_THREAD(gpu);
    *out_ring = NULL;
    r = AX_MEM_NEW(gpu->allocator, ax_gpu_upload_ring);
    if (r == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_gpu_upload_ring: out of memory");
    }
    memset(r, 0, sizeof *r);
    r->gpu = gpu;
    r->usage = usage;
    r->stats.struct_size = sizeof r->stats;
    res = create_buffer(r, initial_size != 0 ? (initial_size + 3u) & ~(uint64_t)3u : 65536u);
    if (res != AX_OK) {
        AX_MEM_DELETE(gpu->allocator, r);
        return res;
    }
    *out_ring = r;
    return AX_OK;
}

static void release_retired(ax_gpu_upload_ring *r)
{
    uint32_t i;
    for (i = 0; i < r->retired_count; ++i) {
        wgpuBufferRelease(r->retired[i]);
    }
    r->retired_count = 0;
}

void ax_gpu_upload_ring_destroy(ax_gpu_upload_ring *r)
{
    if (r == NULL) {
        return;
    }
    AX_GPU_ASSERT_THREAD(r->gpu);
    release_retired(r);
    if (r->buffer != NULL) {
        wgpuBufferRelease(r->buffer);
    }
    AX_MEM_DELETE(r->gpu->allocator, r);
}

ax_result ax_gpu_upload_ring_begin_frame(ax_gpu_upload_ring *r)
{
    AX_GPU_ASSERT_THREAD(r->gpu);
    release_retired(r);
    r->head = 0;
    r->stats.frame_bytes = 0;
    if (r->buffer == NULL || r->generation != r->gpu->generation) {
        if (r->buffer != NULL) {
            wgpuBufferRelease(r->buffer);
            r->buffer = NULL;
            r->stats.recreations++;
        }
        return create_buffer(r, r->capacity);
    }
    return AX_OK;
}

ax_result ax_gpu_upload_ring_push(ax_gpu_upload_ring *r, const void *data, uint64_t size,
                                  uint64_t align, ax_gpu_upload_slice *out)
{
    uint64_t offset;
    AX_GPU_ASSERT_THREAD(r->gpu);
    AX_ASSERT(align >= 4 && (align & (align - 1)) == 0);
    AX_ASSERT(size % 4 == 0);
    if (r->buffer == NULL) {
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu_upload_ring: no buffer");
    }
    offset = (r->head + align - 1) & ~(align - 1);
    if (offset + size > r->capacity) {
        /* Grow: the old buffer keeps the slices already handed out until the next frame. */
        uint64_t cap = r->capacity * 2u;
        WGPUBuffer old = r->buffer;
        ax_result res;
        while (cap < size) {
            cap *= 2u;
        }
        if (r->retired_count == MAX_RETIRED) {
            return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_gpu_upload_ring: frame too large");
        }
        res = create_buffer(r, cap);
        if (res != AX_OK) {
            r->buffer = old;
            return res;
        }
        r->retired[r->retired_count++] = old;
        r->stats.grows++;
        offset = 0;
    }
    if (size != 0) {
        wgpuQueueWriteBuffer(r->gpu->queue, r->buffer, offset, data, (size_t)size);
    }
    r->head = offset + size;
    r->stats.frame_bytes += size;
    if (r->stats.frame_bytes > r->stats.peak_bytes) {
        r->stats.peak_bytes = r->stats.frame_bytes;
    }
    out->buffer = r->buffer;
    out->offset = offset;
    out->size = size;
    return AX_OK;
}

void ax_gpu_upload_ring_get_stats(ax_gpu_upload_ring *r, ax_gpu_upload_stats *out)
{
    uint32_t size;
    AX_GPU_ASSERT_THREAD(r->gpu);
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_gpu_upload_stats));
    size = out->struct_size;
    *out = r->stats;
    out->struct_size = size;
}
