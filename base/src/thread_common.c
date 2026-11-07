/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thread facilities shared by every threading implementation.
 */
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_thread.h"

static ax_atomic_u64 ax_next_thread_id = AX_ATOMIC_INIT(1);
static AX_THREAD_LOCAL uint64_t ax_tls_thread_id;

uint64_t ax_thread_current_id(void)
{
    uint64_t id = ax_tls_thread_id;
    if (id == 0) {
        id = ax_atomic_fetch_add_u64(&ax_next_thread_id, 1, AX_MO_RELAXED);
        ax_tls_thread_id = id;
    }
    return id;
}
