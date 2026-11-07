/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: debug assertions.
 *
 * AX_ASSERT(expr) checks an invariant. When assertions are enabled, a false expr prints the
 * expression and location to stderr and aborts. When disabled it does not exist: expr is not
 * evaluated (it only appears inside sizeof, which also keeps variables used solely by
 * assertions from triggering unused warnings).
 *
 * Assertions are enabled unless NDEBUG is defined; define AX_ENABLE_ASSERTS to 0 or 1 to override.
 * Never put side effects in an assertion, and never use one in place of an error path that a
 * release build needs: release builds return ax_result codes instead.
 */
#ifndef AX_BASE_ASSERT_H
#define AX_BASE_ASSERT_H

#include "../ax_platform.h"
#include "ax_compiler.h"

#ifndef AX_ENABLE_ASSERTS
#    ifdef NDEBUG
#        define AX_ENABLE_ASSERTS 0
#    else
#        define AX_ENABLE_ASSERTS 1
#    endif
#endif

AX_EXTERN_C_BEGIN

AX_NORETURN AX_API void ax_assert_fail(const char *expr, const char *msg, const char *file,
                                       int line);

AX_EXTERN_C_END

#if AX_ENABLE_ASSERTS
#    define AX_ASSERT(expr) \
        ((expr) ? (void)0 : ax_assert_fail(#expr, (const char *)0, __FILE__, __LINE__))
#    define AX_ASSERT_MSG(expr, msg) \
        ((expr) ? (void)0 : ax_assert_fail(#expr, (msg), __FILE__, __LINE__))
#else
#    define AX_ASSERT(expr)          ((void)sizeof((expr) ? 1 : 0))
#    define AX_ASSERT_MSG(expr, msg) ((void)sizeof((expr) ? 1 : 0))
#endif

#endif /* AX_BASE_ASSERT_H */
