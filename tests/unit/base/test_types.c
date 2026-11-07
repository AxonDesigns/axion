/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/ax_types.h"

static const ax_str static_str = AX_STR_INIT("static");

AX_TEST(str_literal_length_excludes_nul)
{
    ax_str s = AX_STR("Studio");
    AX_CHECK_EQ_U(s.len, 6);
    AX_CHECK(memcmp(s.ptr, "Studio", 6) == 0);

    ax_str empty = AX_STR("");
    AX_CHECK_EQ_U(empty.len, 0);

    AX_CHECK_EQ_U(static_str.len, 6);
}

AX_TEST(str_counts_utf8_bytes)
{
    ax_str s = AX_STR("\xC3\xA1rbol"); /* "árbol": 'á' is two bytes */
    AX_CHECK_EQ_U(s.len, 6);
}

AX_TEST(result_values_are_stable)
{
    AX_CHECK_EQ_I(AX_OK, 0);
    AX_CHECK_EQ_I(AX_ERR_INVALID_HANDLE, 1);
    AX_CHECK_EQ_I(AX_ERR_WRONG_THREAD, 2);
    AX_CHECK_EQ_I(AX_ERR_OUT_OF_MEMORY, 3);
    AX_CHECK_EQ_I(AX_ERR_DEVICE_LOST, 4);
    AX_CHECK_EQ_I(AX_ERR_UNSUPPORTED, 5);
    AX_CHECK_EQ_I(AX_ERR_ABI_MISMATCH, 6);
    AX_CHECK_EQ_I(AX_ERR_INVALID_ARGUMENT, 7);
}

AX_TEST(result_names)
{
    AX_CHECK(strcmp(ax_result_name(AX_OK), "AX_OK") == 0);
    AX_CHECK(strcmp(ax_result_name(AX_ERR_INVALID_HANDLE), "AX_ERR_INVALID_HANDLE") == 0);
    AX_CHECK(strcmp(ax_result_name(AX_ERR_ABI_MISMATCH), "AX_ERR_ABI_MISMATCH") == 0);
    AX_CHECK(strcmp(ax_result_name(AX_ERR_INVALID_ARGUMENT), "AX_ERR_INVALID_ARGUMENT") == 0);
    AX_CHECK(strcmp(ax_result_name((ax_result)1000), "AX_ERR_<unknown>") == 0);
}
