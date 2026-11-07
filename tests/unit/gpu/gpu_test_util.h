/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef AXT_GPU_TEST_UTIL_H
#define AXT_GPU_TEST_UTIL_H

#include <stdbool.h>

#include "ax_test.h"
#include "axion/gpu/ax_gpu.h"

/* Call first in every gpu/ test that touches a WebGPU instance.
 *
 * On Linux, keeps one Vulkan instance alive until the process exits, so the Vulkan loader never
 * unloads the drivers (ICDs) when the code under test destroys its last instance. LeakSanitizer
 * then sees driver frames with their module names and tools/sanitizers/lsan-gpu-drivers.supp can
 * match them; once a driver is unloaded its frames become "<unknown module>" and its
 * process-lifetime allocations would be reported as AXION leaks. Elsewhere it does nothing.
 * Idempotent; failures (no Vulkan loader) are ignored, since tests then skip for lack of an
 * adapter anyway. */
void axt_gpu_pin_drivers(void);

/* Creates an ax_gpu for a test.
 * AX_TEST_GPU_ADAPTER=software selects AX_GPU_ADAPTER_SOFTWARE (CI, which has no GPU), anything
 * else the default adapter. Returns 1 on success; 0 when no adapter exists, so the test should
 * skip; -1 when AX_REQUIRE_GPU_TESTS=1 (set in CI) turns that into a failure, or on any other
 * error. The failure is already reported. */
int axt_gpu_create(const ax_allocator *allocator, ax_gpu **out_gpu);

#define AXT_GPU_CREATE(allocator, out_gpu)                                     \
    do {                                                                       \
        int axt__r = axt_gpu_create((allocator), (out_gpu));                   \
        if (axt__r == 0) {                                                     \
            AX_SKIP("no WebGPU adapter (set AX_TEST_GPU_ADAPTER=software and " \
                    "install a software Vulkan driver to run gpu/ tests)");    \
        }                                                                      \
        if (axt__r < 0) {                                                      \
            return;                                                            \
        }                                                                      \
    } while (0)

/* Clears a 64x64 RGBA8 texture to (0.25, 0.5, 0.75, 1), copies it to a buffer and checks every
 * pixel. Reports failures itself; returns true when the device rendered correctly. */
bool axt_gpu_clear_and_read_back(ax_gpu *gpu);

#endif
