/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>

#include "axion/ax_types.h"
#include "axion/ax_version.h"
#include "axion/base/ax_assert.h"
#include "axion/base/ax_compiler.h"

/* The ABI fixes these values; changing one is a breaking change. */
AX_STATIC_ASSERT(AX_OK == 0, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_INVALID_HANDLE == 1, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_WRONG_THREAD == 2, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_OUT_OF_MEMORY == 3, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_DEVICE_LOST == 4, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_UNSUPPORTED == 5, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_ABI_MISMATCH == 6, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_INVALID_ARGUMENT == 7, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_TIMEOUT == 8, "ax_result values are ABI");
AX_STATIC_ASSERT(AX_ERR_IO == 9, "ax_result values are ABI");
AX_STATIC_ASSERT(sizeof(ax_result) == 4, "ax_result must be 32 bits");

const char *ax_result_name(ax_result result)
{
    switch (result) {
    case AX_OK:
        return "AX_OK";
    case AX_ERR_INVALID_HANDLE:
        return "AX_ERR_INVALID_HANDLE";
    case AX_ERR_WRONG_THREAD:
        return "AX_ERR_WRONG_THREAD";
    case AX_ERR_OUT_OF_MEMORY:
        return "AX_ERR_OUT_OF_MEMORY";
    case AX_ERR_DEVICE_LOST:
        return "AX_ERR_DEVICE_LOST";
    case AX_ERR_UNSUPPORTED:
        return "AX_ERR_UNSUPPORTED";
    case AX_ERR_ABI_MISMATCH:
        return "AX_ERR_ABI_MISMATCH";
    case AX_ERR_INVALID_ARGUMENT:
        return "AX_ERR_INVALID_ARGUMENT";
    case AX_ERR_TIMEOUT:
        return "AX_ERR_TIMEOUT";
    case AX_ERR_IO:
        return "AX_ERR_IO";
    }
    return "AX_ERR_<unknown>";
}

uint32_t ax_version(void)
{
    return AX_VERSION;
}

void ax_assert_fail(const char *expr, const char *msg, const char *file, int line)
{
    if (msg != NULL) {
        fprintf(stderr, "%s:%d: assertion failed: %s (%s)\n", file, line, expr, msg);
    } else {
        fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expr);
    }
    fflush(stderr);
    abort();
}
