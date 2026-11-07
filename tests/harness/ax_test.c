/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Failures recorded by the case that is running. Cases run one at a time on the main thread;
 * checks from helper threads must be made after joining them. */
static unsigned ax_test_failures;

void ax_test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "%s:%d: check failed: ", file, line);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
    ax_test_failures++;
}

static int run_case(const char *suite, const ax_test_case *tc)
{
    ax_test_failures = 0;
    printf("[ RUN  ] %s.%s\n", suite, tc->name);
    fflush(stdout);
    tc->fn();
    printf("[ %s ] %s.%s\n", ax_test_failures == 0 ? " OK " : "FAIL", suite, tc->name);
    fflush(stdout);
    return ax_test_failures == 0 ? 0 : 1;
}

int ax_test_main(const char *suite, const ax_test_case *cases, size_t count, int argc, char **argv)
{
    size_t i;
    int failed = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [--list | <case>]\n", argv[0]);
        return 2;
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (i = 0; i < count; ++i) {
            printf("%s\n", cases[i].name);
        }
        return 0;
    }
    if (argc == 2) {
        for (i = 0; i < count; ++i) {
            if (strcmp(argv[1], cases[i].name) == 0) {
                return run_case(suite, &cases[i]);
            }
        }
        fprintf(stderr, "%s: no test case named '%s'\n", suite, argv[1]);
        return 2;
    }

    for (i = 0; i < count; ++i) {
        failed += run_case(suite, &cases[i]);
    }
    printf("%s: %zu cases, %d failed\n", suite, count, failed);
    return failed == 0 ? 0 : 1;
}
