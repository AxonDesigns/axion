/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: build configuration. Platform and compiler detection, the AX_API export macro and
 * C++ linkage guards.
 * Every AX_OS_* / AX_COMPILER_* macro is always defined, as 0 or 1, so code tests them with #if.
 */
#ifndef AX_CONFIG_H
#define AX_CONFIG_H

/* ---- Operating system ------------------------------------------------------------------------ */

#if defined(_WIN32)
#    define AX_OS_WINDOWS 1
#else
#    define AX_OS_WINDOWS 0
#endif

#if defined(__APPLE__)
#    include <TargetConditionals.h>
#    if TARGET_OS_IPHONE
#        define AX_OS_IOS   1
#        define AX_OS_MACOS 0
#    else
#        define AX_OS_IOS   0
#        define AX_OS_MACOS 1
#    endif
#else
#    define AX_OS_IOS   0
#    define AX_OS_MACOS 0
#endif

#if defined(__ANDROID__)
#    define AX_OS_ANDROID 1
#else
#    define AX_OS_ANDROID 0
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#    define AX_OS_LINUX 1
#else
#    define AX_OS_LINUX 0
#endif

#if defined(__EMSCRIPTEN__)
#    define AX_OS_WEB 1
#else
#    define AX_OS_WEB 0
#endif

#define AX_OS_APPLE (AX_OS_MACOS || AX_OS_IOS)
#define AX_OS_POSIX (AX_OS_APPLE || AX_OS_LINUX || AX_OS_ANDROID || AX_OS_WEB)

/* ---- Compiler -------------------------------------------------------------------------------- */
/* clang-cl defines both _MSC_VER and __clang__; it counts as Clang here. */

#if defined(__clang__)
#    define AX_COMPILER_CLANG 1
#else
#    define AX_COMPILER_CLANG 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#    define AX_COMPILER_MSVC 1
#else
#    define AX_COMPILER_MSVC 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#    define AX_COMPILER_GCC 1
#else
#    define AX_COMPILER_GCC 0
#endif

/* ---- Pointer width --------------------------------------------------------------------------- */

#if defined(_WIN64) || defined(__LP64__) || defined(__wasm64__) || \
    (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8)
#    define AX_PTR_64 1
#else
#    define AX_PTR_64 0
#endif

/* ---- Symbol export --------------------------------------------------------------------------- */
/*
 * AX_API marks every function of the public C API.
 *   - Static library (today's only build): expands to nothing.
 *   - Shared library: define AX_SHARED when building and when consuming; define AX_BUILDING_DLL
 *     additionally while building it.
 */
#if defined(AX_SHARED)
#    if AX_OS_WINDOWS
#        if defined(AX_BUILDING_DLL)
#            define AX_API __declspec(dllexport)
#        else
#            define AX_API __declspec(dllimport)
#        endif
#    elif defined(__GNUC__) || defined(__clang__)
#        define AX_API __attribute__((visibility("default")))
#    else
#        define AX_API
#    endif
#else
#    define AX_API
#endif

/* ---- C++ linkage ----------------------------------------------------------------------------- */

#ifdef __cplusplus
#    define AX_EXTERN_C_BEGIN extern "C" {
#    define AX_EXTERN_C_END   }
#else
#    define AX_EXTERN_C_BEGIN
#    define AX_EXTERN_C_END
#endif

#endif /* AX_CONFIG_H */
