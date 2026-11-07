/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef AXT_GPU_TEST_UTIL_H
#define AXT_GPU_TEST_UTIL_H

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

#endif
