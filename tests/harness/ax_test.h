/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal unit-test harness, no external dependencies.
 *
 *   AX_TEST(stale_handle)
 *   {
 *       AX_CHECK(x == 1);                      // records a failure and continues
 *       AX_REQUIRE(ptr != NULL);               // records a failure and leaves the test
 *       AX_CHECK_EQ_U(count, 3);               // prints both values on failure
 *       AX_CHECK_RESULT(ax_pool_free(&p, h), AX_ERR_INVALID_HANDLE);
 *   }
 *
 * Registration is automatic: CMake scans each test file for lines starting with AX_TEST(name),
 * generates the executable's main(), and registers every case as its own ctest test named
 * <module>.<file>.<case> (tests/unit/base/test_pool.c -> base.pool.stale_handle).
 *
 * Executables run all cases with no arguments, one case when given its name, and print the
 * case names with --list. Checks are active in every build type, independent of NDEBUG.
 *
 * Death tests check that misuse aborts (a debug assertion, an error-checking mutex):
 *
 *   AX_DEATH_TEST(relock_aborts) { ax_mutex_lock(&m); ax_mutex_lock(&m); }
 *
 * A death test passes when it aborts (SIGABRT becomes a successful exit) and fails if it returns.
 * They are registered only in builds where assertions are enabled, and are never run by
 * "run all"; ctest runs each one by name in its own process.
 *
 * A case that cannot run in the current environment calls AX_SKIP("why"); it exits with
 * AX_TEST_SKIP_CODE (77), which ctest reports as skipped.
 */
#ifndef AX_TEST_H
#define AX_TEST_H

#include <stddef.h>
#include <stdint.h>

#include "axion/ax_types.h"

typedef void (*ax_test_fn)(void);

typedef struct ax_test_case {
    const char *name;
    ax_test_fn fn;
    int is_death_test; /* expected to abort; only run by name */
} ax_test_case;

#define AX_TEST_SKIP_CODE 77

/* Defines a test case. The prototype keeps -Wmissing-prototypes quiet. */
#define AX_TEST(name)           \
    void ax_test__##name(void); \
    void ax_test__##name(void)

/* Defines a case that must abort. Same shape as AX_TEST; CMake registers it differently. */
#define AX_DEATH_TEST(name) AX_TEST(name)

/* Marks the running case as skipped and leaves it. */
#define AX_SKIP(reason)       \
    do {                      \
        ax_test_skip(reason); \
        return;               \
    } while (0)

void ax_test_skip(const char *reason);

void ax_test_fail(const char *file, int line, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

int ax_test_main(const char *suite, const ax_test_case *cases, size_t count, int argc, char **argv);

#define AX_CHECK(cond)                                     \
    do {                                                   \
        if (!(cond)) {                                     \
            ax_test_fail(__FILE__, __LINE__, "%s", #cond); \
        }                                                  \
    } while (0)

#define AX_REQUIRE(cond)                                   \
    do {                                                   \
        if (!(cond)) {                                     \
            ax_test_fail(__FILE__, __LINE__, "%s", #cond); \
            return;                                        \
        }                                                  \
    } while (0)

#define AX_CHECK_EQ_U(actual, expected)                                                     \
    do {                                                                                    \
        unsigned long long ax__a = (unsigned long long)(actual);                            \
        unsigned long long ax__e = (unsigned long long)(expected);                          \
        if (ax__a != ax__e) {                                                               \
            ax_test_fail(__FILE__, __LINE__, "%s == %s (got %llu, expected %llu)", #actual, \
                         #expected, ax__a, ax__e);                                          \
        }                                                                                   \
    } while (0)

#define AX_CHECK_EQ_I(actual, expected)                                                     \
    do {                                                                                    \
        long long ax__a = (long long)(actual);                                              \
        long long ax__e = (long long)(expected);                                            \
        if (ax__a != ax__e) {                                                               \
            ax_test_fail(__FILE__, __LINE__, "%s == %s (got %lld, expected %lld)", #actual, \
                         #expected, ax__a, ax__e);                                          \
        }                                                                                   \
    } while (0)

#define AX_CHECK_EQ_P(actual, expected)                                                            \
    do {                                                                                           \
        const void *ax__a = (const void *)(actual);                                                \
        const void *ax__e = (const void *)(expected);                                              \
        if (ax__a != ax__e) {                                                                      \
            ax_test_fail(__FILE__, __LINE__, "%s == %s (got %p, expected %p)", #actual, #expected, \
                         ax__a, ax__e);                                                            \
        }                                                                                          \
    } while (0)

#define AX_CHECK_RESULT(actual, expected)                                             \
    do {                                                                              \
        ax_result ax__a = (actual);                                                   \
        ax_result ax__e = (expected);                                                 \
        if (ax__a != ax__e) {                                                         \
            ax_test_fail(__FILE__, __LINE__, "%s == %s (got %s)", #actual, #expected, \
                         ax_result_name(ax__a));                                      \
        }                                                                             \
    } while (0)

/* True when ptr is a multiple of align. */
#define AX_IS_ALIGNED(ptr, align) ((((uintptr_t)(ptr)) & ((uintptr_t)(align) - 1u)) == 0u)

#endif /* AX_TEST_H */
