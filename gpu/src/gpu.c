/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gpu_internal.h"

#include <stdio.h>
#include <string.h>

#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>

#include "webgpu_impl.h"

/* ---- Helpers ------------------------------------------------------------------------------- */

static size_t view_length(WGPUStringView v)
{
    if (v.data == NULL) {
        return 0;
    }
    return v.length == WGPU_STRLEN ? strlen(v.data) : v.length;
}

static void copy_view(char *dst, size_t cap, WGPUStringView v)
{
    size_t n = view_length(v);
    if (n >= cap) {
        n = cap - 1;
    }
    if (n > 0) {
        memcpy(dst, v.data, n);
    }
    dst[n] = '\0';
}

static WGPUStringView make_view(const char *s)
{
    WGPUStringView v = {s, strlen(s)};
    return v;
}

static WGPUBackendType primary_backend(void)
{
#if defined(__APPLE__)
    return WGPUBackendType_Metal;
#elif defined(_WIN32)
    return WGPUBackendType_D3D12;
#else
    return WGPUBackendType_Vulkan;
#endif
}

/* ---- Callbacks ----------------------------------------------------------------------------- */

typedef struct adapter_request {
    bool done;
    WGPURequestAdapterStatus status;
    WGPUAdapter adapter;
    char message[256];
} adapter_request;

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                       void *userdata1, void *userdata2)
{
    adapter_request *req = userdata1;
    (void)userdata2;
    req->done = true;
    req->status = status;
    req->adapter = adapter;
    copy_view(req->message, sizeof req->message, message);
}

typedef struct device_request {
    bool done;
    WGPURequestDeviceStatus status;
    WGPUDevice device;
    char message[256];
} device_request;

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                      void *userdata1, void *userdata2)
{
    device_request *req = userdata1;
    (void)userdata2;
    req->done = true;
    req->status = status;
    req->device = device;
    copy_view(req->message, sizeof req->message, message);
}

static const char *lost_reason_name(WGPUDeviceLostReason reason)
{
    switch (reason) {
    case WGPUDeviceLostReason_Unknown:
        return "unknown";
    case WGPUDeviceLostReason_Destroyed:
        return "destroyed";
    case WGPUDeviceLostReason_CallbackCancelled:
        return "callback cancelled";
    case WGPUDeviceLostReason_FailedCreation:
        return "failed creation";
    default:
        return "?";
    }
}

/* Runs on the owner thread (AllowProcessEvents), from ax_gpu_poll, ax_gpu_wait and friends.
 * userdata2 carries the generation of the device the callback was registered for, so the loss
 * of a device that was already replaced is ignored. */
static void on_device_lost(WGPUDevice const *device, WGPUDeviceLostReason reason,
                           WGPUStringView message, void *userdata1, void *userdata2)
{
    ax_gpu *gpu = userdata1;
    uint32_t generation = (uint32_t)(uintptr_t)userdata2;
    (void)device;

    /* Releasing the device ourselves (destroy, recreation) is not a loss. */
    if (reason == WGPUDeviceLostReason_Destroyed ||
        reason == WGPUDeviceLostReason_CallbackCancelled ||
        reason == WGPUDeviceLostReason_FailedCreation) {
        return;
    }
    if (generation != gpu->generation) {
        return;
    }
    fprintf(stderr, "ax_gpu: device lost (%s): %.*s\n", lost_reason_name(reason),
            (int)view_length(message), message.data != NULL ? message.data : "");
    gpu->lost = true;
    gpu->lost_reason = reason;
}

static const char *error_type_name(WGPUErrorType type)
{
    switch (type) {
    case WGPUErrorType_Validation:
        return "validation";
    case WGPUErrorType_OutOfMemory:
        return "out of memory";
    case WGPUErrorType_Internal:
        return "internal";
    default:
        return "unknown";
    }
}

/* May run on any thread. */
static void on_uncaptured_error(WGPUDevice const *device, WGPUErrorType type,
                                WGPUStringView message, void *userdata1, void *userdata2)
{
    ax_gpu *gpu = userdata1;
    (void)device;
    (void)userdata2;
    ax_atomic_fetch_add_u32(&gpu->uncaptured_errors, 1, AX_MO_RELAXED);
    fprintf(stderr, "ax_gpu: uncaptured %s error: %.*s\n", error_type_name(type),
            (int)view_length(message), message.data != NULL ? message.data : "");
}

/* ---- Adapter and device -------------------------------------------------------------------- */

static ax_result request_adapter(ax_gpu *gpu, bool force_fallback, WGPUAdapter *out_adapter)
{
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_request req;
    WGPUFuture future;

    memset(&req, 0, sizeof req);
    options.featureLevel = WGPUFeatureLevel_Core;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    options.backendType = primary_backend();
    options.forceFallbackAdapter = force_fallback;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_adapter;
    cb.userdata1 = &req;

    future = wgpuInstanceRequestAdapter(gpu->instance, &options, cb);
    if (!ax__webgpu_wait(gpu->instance, NULL, future, &req.done, UINT64_MAX) ||
        req.status != WGPURequestAdapterStatus_Success || req.adapter == NULL) {
        if (req.adapter != NULL) {
            wgpuAdapterRelease(req.adapter);
        }
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_gpu: no %s adapter (%s): %s",
                            force_fallback ? "fallback" : "WebGPU", ax__webgpu_name,
                            req.message[0] != '\0' ? req.message : "no message");
    }
    *out_adapter = req.adapter;
    return AX_OK;
}

static WGPUAdapterType adapter_type(WGPUAdapter adapter)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    WGPUAdapterType type = WGPUAdapterType_Unknown;
    if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
        type = info.adapterType;
        wgpuAdapterInfoFreeMembers(info);
    }
    return type;
}

static ax_result pick_adapter(ax_gpu *gpu)
{
    WGPUAdapter adapter = NULL;
    ax_result r;

    if (gpu->adapter_kind == AX_GPU_ADAPTER_DEFAULT) {
        r = request_adapter(gpu, false, &adapter);
        if (r != AX_OK) {
            return r;
        }
    } else {
        /* Software: the standard fallback request first. Dawn's fallback adapter is SwiftShader
         * only, which its packages lack; there, a CPU adapter (lavapipe) is accepted when it is
         * the one the default request returns, which is the case when no GPU driver is
         * installed or the Vulkan loader is restricted to the software driver (decision 45). */
        r = AX_ERR_UNSUPPORTED;
        if (ax__webgpu_fallback_includes_cpu_adapters()) {
            r = request_adapter(gpu, true, &adapter);
        }
        if (r != AX_OK) {
            r = request_adapter(gpu, false, &adapter);
        }
        if (r != AX_OK) {
            return r;
        }
        if (adapter_type(adapter) != WGPUAdapterType_CPU) {
            wgpuAdapterRelease(adapter);
            return ax_set_error(
                AX_ERR_UNSUPPORTED,
                "ax_gpu: no software adapter (%s). With Dawn, restrict the Vulkan loader to a "
                "software driver, e.g. VK_DRIVER_FILES=<path to lvp_icd json>",
                ax__webgpu_name);
        }
    }
    gpu->adapter = adapter;
    return AX_OK;
}

static ax_result create_device(ax_gpu *gpu)
{
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_request req;
    WGPUFuture future;
    uint32_t generation = gpu->generation + 1;

    memset(&req, 0, sizeof req);
    desc.label = make_view("ax_gpu");
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    desc.deviceLostCallbackInfo.callback = on_device_lost;
    desc.deviceLostCallbackInfo.userdata1 = gpu;
    desc.deviceLostCallbackInfo.userdata2 = (void *)(uintptr_t)generation;
    desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    desc.uncapturedErrorCallbackInfo.userdata1 = gpu;
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_device;
    cb.userdata1 = &req;

    future = wgpuAdapterRequestDevice(gpu->adapter, &desc, cb);
    if (!ax__webgpu_wait(gpu->instance, NULL, future, &req.done, UINT64_MAX) ||
        req.status != WGPURequestDeviceStatus_Success || req.device == NULL) {
        if (req.device != NULL) {
            wgpuDeviceRelease(req.device);
        }
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu: device creation failed (%s): %s",
                            ax__webgpu_name, req.message[0] != '\0' ? req.message : "no message");
    }
    gpu->device = req.device;
    gpu->queue = wgpuDeviceGetQueue(req.device);
    gpu->generation = generation;
    gpu->lost = false;
    return AX_OK;
}

static void release_device(ax_gpu *gpu)
{
    if (gpu->queue != NULL) {
        wgpuQueueRelease(gpu->queue);
        gpu->queue = NULL;
    }
    if (gpu->device != NULL) {
        wgpuDeviceRelease(gpu->device);
        gpu->device = NULL;
    }
}

/* ---- API ------------------------------------------------------------------------------------ */

ax_result ax_gpu_create(const ax_gpu_desc *desc, ax_gpu **out_gpu)
{
    const ax_allocator *allocator;
    WGPUInstanceDescriptor idesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    ax_webgpu_instance_storage storage;
    ax_gpu *gpu;
    ax_result r;

    AX_ASSERT(desc != NULL && out_gpu != NULL);
    AX_ASSERT(desc->struct_size >= sizeof(ax_gpu_desc));
    *out_gpu = NULL;

    allocator = desc->allocator != NULL ? desc->allocator : ax_default_allocator();
    gpu = AX_MEM_NEW(allocator, ax_gpu);
    if (gpu == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_gpu: out of memory");
    }
    memset(gpu, 0, sizeof *gpu);
    gpu->allocator = allocator;
    gpu->adapter_kind = desc->adapter;
    gpu->owner_thread = ax_thread_current_id();
    ax_atomic_init_u32(&gpu->uncaptured_errors, 0);

    ax__webgpu_chain_instance_options(&idesc, &storage);
    gpu->instance = wgpuCreateInstance(&idesc);
    if (gpu->instance == NULL) {
        AX_MEM_DELETE(allocator, gpu);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_gpu: cannot create a %s instance",
                            ax__webgpu_name);
    }

    r = pick_adapter(gpu);
    if (r == AX_OK) {
        r = create_device(gpu);
    }
    if (r != AX_OK) {
        ax_gpu_destroy(gpu);
        return r;
    }
    *out_gpu = gpu;
    return AX_OK;
}

void ax_gpu_destroy(ax_gpu *gpu)
{
    if (gpu == NULL) {
        return;
    }
    AX_GPU_ASSERT_THREAD(gpu);
    if (gpu->device != NULL && !gpu->lost) {
        (void)ax_gpu_wait_idle(gpu);
    }
    release_device(gpu);
    if (gpu->adapter != NULL) {
        wgpuAdapterRelease(gpu->adapter);
    }
    /* Callbacks still pending (the device-lost one, with "cancelled") run before the instance
     * goes away, so none can reach the ax_gpu after it is freed. */
    wgpuInstanceProcessEvents(gpu->instance);
    wgpuInstanceRelease(gpu->instance);
    AX_MEM_DELETE(gpu->allocator, gpu);
}

WGPUInstance ax_gpu_instance(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return gpu->instance;
}

WGPUAdapter ax_gpu_adapter(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return gpu->adapter;
}

WGPUDevice ax_gpu_device(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return gpu->device;
}

WGPUQueue ax_gpu_queue(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return gpu->queue;
}

uint32_t ax_gpu_device_generation(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return gpu->generation;
}

void ax_gpu_get_info(ax_gpu *gpu, ax_gpu_info *out_info)
{
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;

    AX_GPU_ASSERT_THREAD(gpu);
    AX_ASSERT(out_info != NULL && out_info->struct_size >= sizeof(ax_gpu_info));
    out_info->implementation = ax__webgpu_name;
    out_info->adapter_name[0] = '\0';
    out_info->driver[0] = '\0';
    out_info->backend = WGPUBackendType_Undefined;
    out_info->adapter_type = WGPUAdapterType_Unknown;
    out_info->vendor_id = 0;
    out_info->device_id = 0;
    out_info->device_generation = gpu->generation;
    if (wgpuAdapterGetInfo(gpu->adapter, &info) == WGPUStatus_Success) {
        copy_view(out_info->adapter_name, sizeof out_info->adapter_name, info.device);
        copy_view(out_info->driver, sizeof out_info->driver, info.description);
        out_info->backend = info.backendType;
        out_info->adapter_type = info.adapterType;
        out_info->vendor_id = info.vendorID;
        out_info->device_id = info.deviceID;
        wgpuAdapterInfoFreeMembers(info);
    }
}

ax_result ax_gpu_poll(ax_gpu *gpu)
{
    AX_GPU_ASSERT_THREAD(gpu);
    wgpuInstanceProcessEvents(gpu->instance);
    if (gpu->lost) {
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu: the device was lost (%s)",
                            lost_reason_name(gpu->lost_reason));
    }
    return AX_OK;
}

bool ax_gpu_wait(ax_gpu *gpu, WGPUFuture future, const bool *done, uint64_t timeout_ns)
{
    AX_GPU_ASSERT_THREAD(gpu);
    return ax__webgpu_wait(gpu->instance, gpu->device, future, done, timeout_ns);
}

typedef struct work_done {
    bool done;
    WGPUQueueWorkDoneStatus status;
} work_done;

static void on_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1,
                         void *userdata2)
{
    work_done *w = userdata1;
    (void)message;
    (void)userdata2;
    w->done = true;
    w->status = status;
}

ax_result ax_gpu_wait_idle(ax_gpu *gpu)
{
    WGPUQueueWorkDoneCallbackInfo cb = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    work_done w = {false, WGPUQueueWorkDoneStatus_Error};
    WGPUFuture future;

    AX_GPU_ASSERT_THREAD(gpu);
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.callback = on_work_done;
    cb.userdata1 = &w;
    future = wgpuQueueOnSubmittedWorkDone(gpu->queue, cb);
    if (!ax_gpu_wait(gpu, future, &w.done, UINT64_MAX) ||
        w.status != WGPUQueueWorkDoneStatus_Success) {
        return ax_set_error(AX_ERR_DEVICE_LOST, "ax_gpu: waiting for the queue failed");
    }
    return AX_OK;
}

uint32_t ax_gpu_uncaptured_error_count(ax_gpu *gpu)
{
    return ax_atomic_load_u32(&gpu->uncaptured_errors, AX_MO_RELAXED);
}
