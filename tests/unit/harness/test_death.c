/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Self-tests of the harness's death tests (decision 41). They hold whatever sanitizer is active,
 * or none: they check the registration and the harness's exit-code rules, not a sanitizer.
 */
#include <stdlib.h>
#include <string.h>

#include "ax_test.h"

static int has_option(const char *var, const char *option)
{
    const char *value = getenv(var);
    return value != NULL && strstr(value, option) != NULL;
}

/* Death tests run with every sanitizer told not to abort on a report. This case aborts (and so
 * passes) only if ctest gave it that environment; if the registration lost it, it returns, which
 * fails a death test. */
AX_DEATH_TEST(sanitizers_are_told_not_to_abort)
{
    if (has_option("ASAN_OPTIONS", "abort_on_error=0") &&
        has_option("UBSAN_OPTIONS", "abort_on_error=0") &&
        has_option("TSAN_OPTIONS", "abort_on_error=0")) {
        abort();
    }
}

/* What a sanitizer report looks like with abort_on_error=0: the process exits with a non-zero
 * code, without SIGABRT. Such a death test must fail; CMake registers this case with WILL_FAIL,
 * so the suite passes only while that rule holds. */
AX_DEATH_TEST(exiting_without_abort_fails)
{
    _Exit(1);
}

/* And returning normally fails too (inverted with WILL_FAIL as well). */
AX_DEATH_TEST(returning_fails)
{
}
