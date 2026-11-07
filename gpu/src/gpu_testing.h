/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test hooks. Not part of any SDK: only AXION's own tests include this header.
 */
#ifndef AX_GPU_TESTING_H
#define AX_GPU_TESTING_H

#include <stdbool.h>

#include <axion/gpu/ax_gpu.h>

/* Marks the device lost exactly as the device-lost callback does for a real loss, so the next
 * ax_gpu_poll runs the recovery path on any implementation. thread: presenter */
void ax__gpu_test_simulate_loss(ax_gpu *gpu);

/* Makes the implementation lose the device for real (Dawn's wgpuDeviceForceLoss); the loss then
 * arrives through the device-lost callback. Returns false when the implementation cannot.
 * thread: presenter */
bool ax__gpu_test_force_loss(ax_gpu *gpu);

#endif
