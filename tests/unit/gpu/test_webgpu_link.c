/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "gpu_test_util.h"

#include <webgpu/webgpu.h>

/* The pinned WebGPU package links into a strict C11 program and its instance comes up. Needs no
 * adapter, so it runs on machines without a GPU or a software Vulkan driver. */
AX_TEST(instance_is_created_and_released)
{
    WGPUInstanceDescriptor desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    WGPUInstance instance;

    axt_gpu_pin_drivers();
    instance = wgpuCreateInstance(&desc);
    AX_CHECK(instance != NULL);
    wgpuInstanceRelease(instance);
}
