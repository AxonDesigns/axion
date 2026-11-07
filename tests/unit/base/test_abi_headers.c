/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The stable ABI must not depend on the internal SDK: including the umbrella header may not pull
 * in any header from axion/base/. Checked at compile time.
 */
#include "axion/ax_base.h"

#if defined(AX_BASE_COMPILER_H) || defined(AX_BASE_ASSERT_H) || defined(AX_BASE_ATOMIC_H) || \
    defined(AX_BASE_THREAD_H) || defined(AX_BASE_ARENA_H) || defined(AX_BASE_POOL_H) ||      \
    defined(AX_BASE_ARRAY_H) || defined(AX_BASE_MEMORY_H) || defined(AX_BASE_ERROR_SET_H) || \
    defined(AX_BASE_TRACKING_ALLOCATOR_H)
#    error "a stable ABI header includes an internal SDK header"
#endif

#include "ax_test.h"

AX_TEST(umbrella_exposes_the_stable_api)
{
    ax_str s = AX_STR("ok");
    AX_CHECK_EQ_U(s.len, 2);
    AX_CHECK(ax_default_allocator() != NULL);
    AX_CHECK(ax_last_error() != NULL);
    AX_CHECK_EQ_U(ax_version(), AX_VERSION);
    AX_CHECK_EQ_U(ax_handle_slot(ax_handle_make(3, 1)), 3);
}
