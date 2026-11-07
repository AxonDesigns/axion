/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The core module's stable ABI must not depend on any internal SDK: including its umbrella header
 * may not pull in a header from axion/base/ or axion/core/. Checked at compile time.
 */
#include "axion/ax_core.h"

#if defined(AX_BASE_COMPILER_H) || defined(AX_BASE_ASSERT_H) || defined(AX_BASE_ATOMIC_H) || \
    defined(AX_BASE_THREAD_H) || defined(AX_BASE_ARENA_H) || defined(AX_BASE_POOL_H) ||      \
    defined(AX_BASE_ARRAY_H) || defined(AX_BASE_MEMORY_H) || defined(AX_BASE_MATH_H) ||      \
    defined(AX_BASE_ERROR_SET_H) || defined(AX_CORE_NODE_STORE_H)
#    error "a stable ABI header of core includes an internal SDK header"
#endif

#include "ax_test.h"

AX_TEST(umbrella_exposes_the_stable_api)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    ax_dimension d = {0.0f, AX_UNIT_AUTO};
    AX_CHECK(AX_HANDLE_IS_NULL(n));
    AX_CHECK_EQ_U(d.unit, 0);
    AX_CHECK_EQ_I(AX_NODE_VIEW, 1);
    AX_CHECK_EQ_I(AX_UNIT_POINTS, 1);
    AX_CHECK_EQ_I(AX_UNIT_PERCENT, 2);
}
