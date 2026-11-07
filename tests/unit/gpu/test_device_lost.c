/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Device-lost recovery: ax_gpu_poll notices the loss, creates a new adapter and device, and
 * advances the generation; the new device renders.
 */
#include "ax_test.h"
#include "gpu_test_util.h"
#include "gpu_testing.h"

/* A real loss, delivered through the implementation's device-lost callback. Only Dawn can force
 * one (wgpuDeviceForceLoss). */
AX_TEST(a_forced_loss_is_detected_and_the_device_recreated)
{
    ax_gpu *gpu = NULL;
    WGPUDevice old_device;

    AXT_GPU_CREATE(NULL, &gpu);
    old_device = ax_gpu_device(gpu);
    if (!ax__gpu_test_force_loss(gpu)) {
        ax_gpu_destroy(gpu);
        AX_SKIP("this WebGPU implementation cannot force a device loss");
    }

    AX_CHECK_EQ_U(ax_gpu_poll(gpu), AX_OK);
    AX_CHECK_EQ_U(ax_gpu_device_generation(gpu), 2);
    AX_CHECK(ax_gpu_device(gpu) != NULL);
    AX_CHECK(ax_gpu_device(gpu) != old_device);
    AX_CHECK(axt_gpu_clear_and_read_back(gpu));

    /* Nothing further is pending: the old device's loss is not reported twice. */
    AX_CHECK_EQ_U(ax_gpu_poll(gpu), AX_OK);
    AX_CHECK_EQ_U(ax_gpu_device_generation(gpu), 2);
    ax_gpu_destroy(gpu);
}

/* The recovery path itself, on every implementation. */
AX_TEST(recovery_creates_a_working_device_each_time)
{
    ax_gpu *gpu = NULL;
    uint32_t i;

    AXT_GPU_CREATE(NULL, &gpu);
    for (i = 0; i < 3; ++i) {
        ax__gpu_test_simulate_loss(gpu);
        AX_REQUIRE(ax_gpu_poll(gpu) == AX_OK);
        AX_CHECK_EQ_U(ax_gpu_device_generation(gpu), i + 2);
        AX_CHECK(ax_gpu_queue(gpu) != NULL);
        AX_CHECK(axt_gpu_clear_and_read_back(gpu));
    }
    AX_CHECK_EQ_U(ax_gpu_uncaptured_error_count(gpu), 0);
    ax_gpu_destroy(gpu);
}

/* Destroying the ax_gpu releases its device; that is not a loss and must not touch freed memory
 * (ASan checks the second half). */
AX_TEST(destroying_is_not_a_loss)
{
    ax_gpu *gpu = NULL;

    AXT_GPU_CREATE(NULL, &gpu);
    AX_CHECK_EQ_U(ax_gpu_poll(gpu), AX_OK);
    AX_CHECK_EQ_U(ax_gpu_device_generation(gpu), 1);
    ax_gpu_destroy(gpu);
}
