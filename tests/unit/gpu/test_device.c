/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_gpu without a window: device creation, rendering into a texture and reading it back.
 * Run on whatever adapter exists; CI sets AX_TEST_GPU_ADAPTER=software (lavapipe).
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_thread.h"
#include "axion/base/ax_tracking_allocator.h"
#include "gpu_test_util.h"

AX_TEST(creates_a_device_and_reports_its_adapter)
{
    ax_tracking_allocator tracker;
    ax_allocator alloc;
    ax_gpu_info info;
    ax_gpu *gpu = NULL;

    ax_tracking_allocator_init(&tracker, ax_default_allocator());
    alloc = ax_tracking_allocator_get(&tracker);
    AXT_GPU_CREATE(&alloc, &gpu);

    AX_CHECK(ax_gpu_instance(gpu) != NULL);
    AX_CHECK(ax_gpu_adapter(gpu) != NULL);
    AX_CHECK(ax_gpu_device(gpu) != NULL);
    AX_CHECK(ax_gpu_queue(gpu) != NULL);
    AX_CHECK_EQ_U(ax_gpu_device_generation(gpu), 1);

    memset(&info, 0xAB, sizeof info);
    info.struct_size = sizeof info;
    ax_gpu_get_info(gpu, &info);
    AX_CHECK(info.implementation != NULL);
#if defined(AX_WEBGPU_DAWN)
    AX_CHECK(strcmp(info.implementation, "dawn") == 0);
#else
    AX_CHECK(strcmp(info.implementation, "wgpu-native") == 0);
#endif
    AX_CHECK(info.adapter_name[0] != '\0');
    AX_CHECK(memchr(info.adapter_name, '\0', sizeof info.adapter_name) != NULL);
    AX_CHECK(memchr(info.driver, '\0', sizeof info.driver) != NULL);
    AX_CHECK(info.backend != WGPUBackendType_Undefined);
    AX_CHECK_EQ_U(info.device_generation, 1);
    AX_CHECK_EQ_U(ax_gpu_poll(gpu), AX_OK);
    AX_CHECK_EQ_U(ax_gpu_wait_idle(gpu), AX_OK);
    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(gpu), 0);

    ax_gpu_destroy(gpu);
    AX_CHECK_EQ_U(ax_tracking_allocator_deinit(&tracker), 0);
}

AX_TEST(clears_a_texture_and_reads_it_back)
{
    ax_gpu *gpu = NULL;

    AXT_GPU_CREATE(NULL, &gpu);
    AX_CHECK(axt_gpu_clear_and_read_back(gpu));
    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(gpu), 0);
    ax_gpu_destroy(gpu);
}

AX_TEST(software_adapter_is_a_cpu_adapter_or_unsupported)
{
    ax_gpu_desc desc = {sizeof desc, NULL, AX_GPU_ADAPTER_SOFTWARE};
    ax_gpu *gpu = NULL;
    ax_result r;

    axt_gpu_pin_drivers();
    r = ax_gpu_create(&desc, &gpu);
    if (r == AX_ERR_UNSUPPORTED) {
        AX_CHECK(gpu == NULL);
        AX_SKIP("no software adapter on this machine");
    }
    AX_REQUIRE(r == AX_OK);
    {
        ax_gpu_info info;
        info.struct_size = sizeof info;
        ax_gpu_get_info(gpu, &info);
        AX_CHECK(info.adapter_type == WGPUAdapterType_CPU);
    }
    ax_gpu_destroy(gpu);
}

static int touch_device(void *arg)
{
    (void)ax_gpu_device((ax_gpu *)arg); /* not the ax_gpu's thread: asserts */
    return 0;
}

AX_DEATH_TEST(use_from_another_thread_asserts)
{
    ax_gpu *gpu = NULL;
    ax_thread thread;

    AXT_GPU_CREATE(NULL, &gpu);
    AX_REQUIRE(ax_thread_create(&thread, touch_device, gpu) == AX_OK);
    (void)ax_thread_join(&thread);
    ax_gpu_destroy(gpu); /* not reached in debug builds */
}
