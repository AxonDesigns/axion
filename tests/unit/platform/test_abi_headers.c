/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The platform module's stable ABI must not depend on any internal SDK: including its umbrella
 * header may not pull in a header from axion/base/ or axion/platform/, nor a windowing system's
 * headers. Checked at compile time.
 */
#include "axion/ax_platform.h"

#if defined(AX_BASE_COMPILER_H) || defined(AX_BASE_ASSERT_H) || defined(AX_BASE_ATOMIC_H) || \
    defined(AX_BASE_THREAD_H) || defined(AX_BASE_ARENA_H) || defined(AX_BASE_POOL_H) ||      \
    defined(AX_BASE_ARRAY_H) || defined(AX_BASE_MEMORY_H) || defined(AX_BASE_ERROR_SET_H) || \
    defined(AX_BASE_TRACKING_ALLOCATOR_H)
#    error "a stable platform header includes base's internal SDK"
#endif
#if defined(AX_PLATFORM_SURFACE_NATIVE_H) || defined(AX_PLATFORM_PRESENT_H) || \
    defined(AX_PLATFORM_HIT_REGIONS_H)
#    error "a stable platform header includes the platform internal SDK"
#endif
#if defined(WAYLAND_CLIENT_H) || defined(WAYLAND_CLIENT_CORE_H)
#    error "a stable platform header includes Wayland headers"
#endif

#include <string.h>

#include "ax_test.h"

AX_TEST(umbrella_exposes_the_stable_api)
{
    ax_window_desc desc;
    ax_app app = AX_HANDLE_NULL_INIT;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    AX_CHECK_EQ_U(desc.struct_size, sizeof(ax_window_desc));
    AX_CHECK_EQ_U(ax_app_caps(app), 0); /* invalid handle */
    AX_CHECK_EQ_I(ax_app_backend(app), AX_BACKEND_NONE);
}
