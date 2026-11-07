/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef AX_GPU_INTERNAL_H
#define AX_GPU_INTERNAL_H

#include <axion/base/ax_assert.h>
#include <axion/base/ax_atomic.h>
#include <axion/base/ax_thread.h>
#include <axion/gpu/ax_gpu.h>

struct ax_gpu {
    const ax_allocator *allocator;
    ax_gpu_adapter_kind adapter_kind;
    uint64_t owner_thread;

    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    uint32_t generation;

    /* Set by the device-lost callback, which runs on the owner thread (AllowProcessEvents). */
    bool lost;
    WGPUDeviceLostReason lost_reason;

    ax_atomic_u32 uncaptured_errors; /* the uncaptured-error callback may run on any thread */
};

/* The thread rule (decision 32's "thread: presenter" tag, enforced for gpu/): an ax_gpu and its
 * surfaces are used only by the thread that created the ax_gpu. Checked in debug builds. */
#define AX_GPU_ASSERT_THREAD(gpu)                                \
    AX_ASSERT_MSG(ax_thread_current_id() == (gpu)->owner_thread, \
                  "ax_gpu used from a thread other than the one that created it")

#endif
