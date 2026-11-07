/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The only place gpu/ depends on which WebGPU implementation it links (decision 43).
 *
 * Dawn and wgpu-native both implement the standard webgpu.h, and agree on every struct and enum
 * gpu/ uses. What differs is implemented once per implementation, in impl_dawn.c and
 * impl_wgpu_native.c; everything else in gpu/ calls these functions instead of testing
 * AX_WEBGPU_DAWN / AX_WEBGPU_WGPU_NATIVE.
 */
#ifndef AX_GPU_WEBGPU_IMPL_H
#define AX_GPU_WEBGPU_IMPL_H

#include <stdbool.h>
#include <stdint.h>

#include <webgpu/webgpu.h>

/* "dawn" or "wgpu-native". */
extern const char ax__webgpu_name[];

/* Chains implementation-specific options into the instance descriptor (the descriptor's other
 * fields are already filled). `storage` must stay alive until wgpuCreateInstance returns. */
typedef struct ax_webgpu_instance_storage {
    uint64_t words[48];
} ax_webgpu_instance_storage;
void ax__webgpu_chain_instance_options(WGPUInstanceDescriptor *desc,
                                       ax_webgpu_instance_storage *storage);

/* Waits until `*done` becomes true or the timeout passes, running callbacks on this thread.
 * `future` belongs to a callback registered with WGPUCallbackMode_AllowProcessEvents that sets
 * `*done`. `device` may be NULL (adapter and device requests). UINT64_MAX waits without a limit.
 * - Dawn waits on the future (instance feature TimedWaitAny, required at instance creation).
 * - wgpu-native implements neither wgpuInstanceWaitAny nor wgpuHasInstanceFeature (both panic),
 *   so it polls: ProcessEvents, then wgpuDevicePoll, until the callback has run. */
bool ax__webgpu_wait(WGPUInstance instance, WGPUDevice device, WGPUFuture future, const bool *done,
                     uint64_t timeout_ns);

/* True when forceFallbackAdapter can return a CPU adapter such as lavapipe. Dawn treats only
 * SwiftShader as a fallback adapter, and its prebuilt packages do not include it. */
bool ax__webgpu_fallback_includes_cpu_adapters(void);

/* Makes the device lost as a real loss would (not as wgpuDeviceDestroy does). Returns false when
 * the implementation has no way to do it. */
bool ax__webgpu_force_device_loss(WGPUDevice device, const char *message);

/* Creates a render pipeline and reports it through `info` (mode AllowProcessEvents). Dawn compiles
 * it on its own threads and calls back from ax_gpu_poll; wgpu-native has no asynchronous creation
 * (its entry point is not implemented), so the pipeline is built here and the callback runs
 * before this returns. */
WGPUFuture ax__webgpu_create_render_pipeline_async(WGPUDevice device,
                                                   const WGPURenderPipelineDescriptor *desc,
                                                   WGPUCreateRenderPipelineAsyncCallbackInfo info);

#endif
