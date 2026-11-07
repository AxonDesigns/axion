/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Dawn's side of webgpu_impl.h.
 */
#include "webgpu_impl.h"

#include <string.h>

const char ax__webgpu_name[] = "dawn";

static const WGPUInstanceFeatureName timed_wait_any = WGPUInstanceFeatureName_TimedWaitAny;

void ax__webgpu_chain_instance_options(WGPUInstanceDescriptor *desc,
                                       ax_webgpu_instance_storage *storage)
{
    (void)storage;
    /* Waiting on futures with a timeout (ax__webgpu_wait). */
    desc->requiredFeatureCount = 1;
    desc->requiredFeatures = &timed_wait_any;
}

bool ax__webgpu_wait(WGPUInstance instance, WGPUDevice device, WGPUFuture future, const bool *done,
                     uint64_t timeout_ns)
{
    WGPUFutureWaitInfo info = {future, 0};
    (void)device;
    if (!*done) {
        (void)wgpuInstanceWaitAny(instance, 1, &info, timeout_ns);
    }
    return *done;
}

bool ax__webgpu_fallback_includes_cpu_adapters(void)
{
    return false;
}

bool ax__webgpu_force_device_loss(WGPUDevice device, const char *message)
{
    WGPUStringView msg = {message, strlen(message)};
    wgpuDeviceForceLoss(device, WGPUDeviceLostReason_Unknown, msg);
    return true;
}

WGPUFuture ax__webgpu_create_render_pipeline_async(WGPUDevice device,
                                                   const WGPURenderPipelineDescriptor *desc,
                                                   WGPUCreateRenderPipelineAsyncCallbackInfo info)
{
    return wgpuDeviceCreateRenderPipelineAsync(device, desc, info);
}
