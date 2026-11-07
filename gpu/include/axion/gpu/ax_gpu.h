/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the WebGPU device AXION renders with.
 *
 * An ax_gpu owns one WebGPU instance, adapter, device and queue, and recreates the device when it
 * is lost (ax_gpu_poll). Renderers use the webgpu.h handles it hands out directly; nothing in
 * AXION's stable ABI exposes webgpu.h types.
 *
 * Threads: an ax_gpu, its device and its surfaces belong to the thread that created it, the
 * render thread ("presenter" in the platform's thread tags). Every function is tagged
 * "thread: presenter" and asserts in debug builds that it runs on that thread.
 */
#ifndef AX_GPU_GPU_H
#define AX_GPU_GPU_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_types.h>

#include <webgpu/webgpu.h>

AX_EXTERN_C_BEGIN

typedef struct ax_gpu ax_gpu;

typedef enum ax_gpu_adapter_kind {
    /* The high-performance adapter: the discrete GPU on machines with two. */
    AX_GPU_ADAPTER_DEFAULT = 0,
    /* A CPU (software) adapter, such as Mesa's lavapipe: for CI and tests without a GPU. Fails
     * with AX_ERR_UNSUPPORTED when the implementation finds none. */
    AX_GPU_ADAPTER_SOFTWARE = 1
} ax_gpu_adapter_kind;

typedef struct ax_gpu_desc {
    uint32_t struct_size;          /* sizeof(ax_gpu_desc) */
    const ax_allocator *allocator; /* NULL: ax_default_allocator() */
    ax_gpu_adapter_kind adapter;
} ax_gpu_desc;

typedef struct ax_gpu_info {
    uint32_t struct_size;       /* set by the caller: sizeof(ax_gpu_info) */
    const char *implementation; /* "dawn" or "wgpu-native" */
    char adapter_name[128];     /* the adapter's device name, truncated */
    char driver[128];           /* the adapter's description (driver and version), truncated */
    WGPUBackendType backend;
    WGPUAdapterType adapter_type;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t device_generation; /* see ax_gpu_device_generation */
} ax_gpu_info;

/*
 * Creates the instance, picks the adapter and creates the device and queue. The calling thread
 * becomes the ax_gpu's thread.
 * Errors: AX_ERR_UNSUPPORTED when no adapter matches (no GPU and no software driver, for
 * example), AX_ERR_DEVICE_LOST when the device cannot be created, AX_ERR_OUT_OF_MEMORY.
 * thread: presenter
 */
AX_API ax_result ax_gpu_create(const ax_gpu_desc *desc, ax_gpu **out_gpu);

/* Waits for submitted work, then releases everything. NULL is ignored. thread: presenter */
AX_API void ax_gpu_destroy(ax_gpu *gpu);

/* The webgpu.h handles, owned by the ax_gpu (do not release them). The device and queue change
 * when the device is recreated; fetch them again after ax_gpu_device_generation changes.
 * thread: presenter */
AX_API WGPUInstance ax_gpu_instance(ax_gpu *gpu);
AX_API WGPUAdapter ax_gpu_adapter(ax_gpu *gpu);
AX_API WGPUDevice ax_gpu_device(ax_gpu *gpu);
AX_API WGPUQueue ax_gpu_queue(ax_gpu *gpu);

/* Counts device creations, starting at 1. Resources made from an older device are dead and must
 * be recreated from their descriptors. thread: presenter */
AX_API uint32_t ax_gpu_device_generation(ax_gpu *gpu);

/* thread: presenter */
AX_API void ax_gpu_get_info(ax_gpu *gpu, ax_gpu_info *out_info);

/*
 * Runs pending WebGPU callbacks on this thread, and recreates the device if it was lost.
 * Errors: AX_ERR_DEVICE_LOST when the device was lost and could not be recreated (the ax_gpu
 * retries on the next call).
 * thread: presenter
 */
AX_API ax_result ax_gpu_poll(ax_gpu *gpu);

/*
 * Waits until `*done` becomes true, running WebGPU callbacks on this thread. `future` belongs to
 * a callback registered with WGPUCallbackMode_AllowProcessEvents that sets `*done`: not every
 * implementation can wait on a future alone (wgpu-native polls until the callback has run).
 * Returns false on timeout. UINT64_MAX waits without a limit.
 * thread: presenter
 */
AX_API bool ax_gpu_wait(ax_gpu *gpu, WGPUFuture future, const bool *done, uint64_t timeout_ns);

/* Waits until the queue has finished all work submitted so far. thread: presenter */
AX_API ax_result ax_gpu_wait_idle(ax_gpu *gpu);

/*
 * Creates a render pipeline without blocking the calling thread, so a first frame never waits for
 * shaders to compile. `info.mode` must be WGPUCallbackMode_AllowProcessEvents: the callback runs
 * on this thread, from ax_gpu_poll or ax_gpu_wait, and owns the pipeline it receives (NULL on
 * failure). With implementations that cannot compile asynchronously (wgpu-native) the pipeline is
 * built at once and the callback runs before this returns. The future is for ax_gpu_wait.
 * thread: presenter
 */
AX_API WGPUFuture
ax_gpu_create_render_pipeline_async(ax_gpu *gpu, const WGPURenderPipelineDescriptor *desc,
                                    WGPUCreateRenderPipelineAsyncCallbackInfo info);

/* Number of validation and out-of-memory errors the device reported without an error scope
 * (uncaptured errors) since the ax_gpu was created. Each one is also printed to stderr.
 * thread: any */
AX_API uint32_t ax_gpu_uncaptured_error_count(ax_gpu *gpu);

AX_EXTERN_C_END

#endif
