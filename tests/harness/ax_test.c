/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

/* Failures recorded by the case that is running. Cases run one at a time on the main thread;
 * checks from helper threads must be made after joining them. */
static unsigned ax_test_failures;
static int ax_test_skipped;

void ax_test_skip(const char *reason)
{
    printf("[ SKIP ] %s\n", reason);
    fflush(stdout);
    ax_test_skipped = 1;
}

/* abort() must end the process quietly: on Windows the CRT and Windows Error Reporting would
 * otherwise show dialogs, which hang unattended runs (death tests abort on purpose). */
static void quiet_abort(void)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#    if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#    endif
#endif
}

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

/* A death test passes by aborting: the handler turns SIGABRT into a successful exit. _Exit is
 * async-signal-safe; nothing else is called from the handler. */
static void death_test_aborted(int sig)
{
    (void)sig;
    _Exit(0);
}

/* 0 passed, 1 failed, AX_TEST_SKIP_CODE skipped. */
static int run_case(const char *suite, const ax_test_case *tc)
{
    ax_test_failures = 0;
    ax_test_skipped = 0;
    printf("[ RUN  ] %s.%s%s\n", suite, tc->name, tc->is_death_test ? " (must abort)" : "");
    fflush(stdout);
    if (tc->is_death_test) {
        signal(SIGABRT, death_test_aborted);
    }
    tc->fn();
    if (ax_test_failures != 0) {
        printf("[ FAIL ] %s.%s\n", suite, tc->name);
        fflush(stdout);
        return 1;
    }
    if (ax_test_skipped) {
        fflush(stdout);
        return AX_TEST_SKIP_CODE;
    }
    if (tc->is_death_test) {
        printf("[ FAIL ] %s.%s returned instead of aborting\n", suite, tc->name);
        fflush(stdout);
        return 1;
    }
    printf("[  OK  ] %s.%s\n", suite, tc->name);
    fflush(stdout);
    return 0;
}

int ax_test_main(const char *suite, const ax_test_case *cases, size_t count, int argc, char **argv)
{
    size_t i;
    int failed = 0;
    int ran = 0;
    int skipped = 0;

    quiet_abort();
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
        int r;
        if (cases[i].is_death_test) {
            continue; /* would end this process; ctest runs them one by one */
        }
        r = run_case(suite, &cases[i]);
        ran++;
        if (r == AX_TEST_SKIP_CODE) {
            skipped++;
        } else {
            failed += r;
        }
    }
    printf("%s: %d cases, %d failed, %d skipped\n", suite, ran, failed, skipped);
    return failed == 0 ? 0 : 1;
}
