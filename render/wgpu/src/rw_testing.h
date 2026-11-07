/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test hooks. Not part of any SDK: only AXION's own tests include this header.
 */
#ifndef AX_RENDER_WGPU_TESTING_H
#define AX_RENDER_WGPU_TESTING_H

#include <stdint.h>

#include <axion/render/ax_render_wgpu.h>

/* Pipelines are created but never reported ready: every frame is clear-only. */
#define RW_TEST_HOLD_PIPELINES 0x1u
/* Instances take the pixel footprint from dpdxFine/dpdyFine instead of the CPU's inverse. */
#define RW_TEST_FOOTPRINT_FROM_DERIVATIVES 0x2u

void ax__render_wgpu_test_set_flags(ax_render_wgpu *r, uint32_t flags);

/* Waits (polling the device) until the pipeline for `format` is ready; false on timeout. */
bool ax__render_wgpu_test_wait_ready(ax_render_wgpu *r, WGPUTextureFormat format,
                                     uint64_t timeout_ns);

#endif
