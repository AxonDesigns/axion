/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * wgpu-native's side of webgpu_impl.h.
 */
#include "webgpu_impl.h"

#include <stddef.h>

#include <axion/base/ax_thread.h>
#include <webgpu/wgpu.h>

const char ax__webgpu_name[] = "wgpu-native";

_Static_assert(sizeof(WGPUInstanceExtras) <= sizeof(ax_webgpu_instance_storage),
               "ax_webgpu_instance_storage is too small for WGPUInstanceExtras");

void ax__webgpu_chain_instance_options(WGPUInstanceDescriptor *desc,
                                       ax_webgpu_instance_storage *storage)
{
    /* Only the platform's primary backend, as with Dawn: probing the GL backend as well would load
     * EGL drivers for nothing. */
    WGPUInstanceExtras *extras = (WGPUInstanceExtras *)(void *)storage;
    WGPUInstanceExtras zero = {0};
    *extras = zero;
    extras->chain.sType = (WGPUSType)WGPUSType_InstanceExtras;
#if defined(__APPLE__)
    extras->backends = WGPUInstanceBackend_Metal;
#else
    extras->backends = WGPUInstanceBackend_Vulkan;
#endif
    extras->chain.next = desc->nextInChain;
    desc->nextInChain = &extras->chain;
}

bool ax__webgpu_wait(WGPUInstance instance, WGPUDevice device, WGPUFuture future, const bool *done,
                     uint64_t timeout_ns)
{
    uint64_t start = ax_time_monotonic_ns();
    (void)future;
    for (;;) {
        wgpuInstanceProcessEvents(instance);
        if (*done) {
            return true;
        }
        if (timeout_ns != UINT64_MAX && ax_time_monotonic_ns() - start >= timeout_ns) {
            return false;
        }
        if (device != NULL) {
            (void)wgpuDevicePoll(device, 0, NULL);
        }
        ax_thread_yield();
    }
}

bool ax__webgpu_fallback_includes_cpu_adapters(void)
{
    return true;
}

bool ax__webgpu_force_device_loss(WGPUDevice device, const char *message)
{
    (void)device;
    (void)message;
    return false;
}
