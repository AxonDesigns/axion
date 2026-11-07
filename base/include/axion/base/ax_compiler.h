/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: portable wrappers over the C11 features MSVC spells differently and over the few
 * compiler extensions AXION uses. Extensions appear only through these macros.
 */
#ifndef AX_BASE_COMPILER_H
#define AX_BASE_COMPILER_H

#include "../ax_platform.h"

/* Thread-local storage duration. MSVC's C mode does not accept _Thread_local. */
#if AX_COMPILER_MSVC
#    define AX_THREAD_LOCAL __declspec(thread)
#elif defined(__cplusplus)
#    define AX_THREAD_LOCAL thread_local
#else
#    define AX_THREAD_LOCAL _Thread_local
#endif

/* C11 alignment and static assertions, also usable from C++ glue. */
#ifdef __cplusplus
#    define AX_ALIGNAS(n)            alignas(n)
#    define AX_ALIGNOF(type)         alignof(type)
#    define AX_STATIC_ASSERT(c, msg) static_assert(c, msg)
#    define AX_NORETURN              [[noreturn]]
#else
#    define AX_ALIGNAS(n)            _Alignas(n)
#    define AX_ALIGNOF(type)         _Alignof(type)
#    define AX_STATIC_ASSERT(c, msg) _Static_assert(c, msg)
#    define AX_NORETURN              _Noreturn
#endif

/* printf-style format checking. */
#if defined(__GNUC__) || defined(__clang__)
#    define AX_PRINTF_LIKE(fmt_index, args_index) \
        __attribute__((format(printf, fmt_index, args_index)))
#else
#    define AX_PRINTF_LIKE(fmt_index, args_index)
#endif

/* Branch hints. */
#if defined(__GNUC__) || defined(__clang__)
#    define AX_LIKELY(x)   __builtin_expect(!!(x), 1)
#    define AX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define AX_LIKELY(x)   (x)
#    define AX_UNLIKELY(x) (x)
#endif

/* Silences "unused" warnings for helpers a translation unit may not call. */
#if defined(__GNUC__) || defined(__clang__)
#    define AX_MAYBE_UNUSED __attribute__((unused))
#else
#    define AX_MAYBE_UNUSED
#endif

#define AX_COUNTOF(array) (sizeof(array) / sizeof((array)[0]))

#endif /* AX_BASE_COMPILER_H */
