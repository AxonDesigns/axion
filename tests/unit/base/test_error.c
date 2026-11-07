/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/base/ax_error_set.h"

AX_TEST(starts_clear)
{
    const ax_error *e = ax_last_error();
    AX_REQUIRE(e != NULL);
    AX_CHECK_RESULT(e->code, AX_OK);
    AX_CHECK_EQ_U(e->message.len, 0);
    AX_CHECK(e->message.ptr != NULL && e->message.ptr[0] == '\0');
}

AX_TEST(set_returns_code_and_formats_message)
{
    ax_result r = ax_set_error(AX_ERR_INVALID_HANDLE, "slot %u generation %u", 7u, 3u);
    const ax_error *e = ax_last_error();

    AX_CHECK_RESULT(r, AX_ERR_INVALID_HANDLE);
    AX_CHECK_RESULT(e->code, AX_ERR_INVALID_HANDLE);
    AX_CHECK_EQ_U(e->message.len, strlen("slot 7 generation 3"));
    AX_CHECK(strcmp(e->message.ptr, "slot 7 generation 3") == 0);
}

AX_TEST(clear_resets)
{
    ax_set_error(AX_ERR_OUT_OF_MEMORY, "out of memory");
    ax_clear_error();
    AX_CHECK_RESULT(ax_last_error()->code, AX_OK);
    AX_CHECK_EQ_U(ax_last_error()->message.len, 0);
}

/* Forwards to ax_set_error_v without a format attribute, so a NULL format can be passed. */
static ax_result set_error_forward(ax_result code, const char *fmt, ...)
{
    ax_result r;
    va_list args;
    va_start(args, fmt);
    r = ax_set_error_v(code, fmt, args);
    va_end(args);
    return r;
}

AX_TEST(null_format_records_code_only)
{
    ax_set_error(AX_ERR_UNSUPPORTED, "previous");
    AX_CHECK_RESULT(set_error_forward(AX_ERR_DEVICE_LOST, NULL), AX_ERR_DEVICE_LOST);
    AX_CHECK_RESULT(ax_last_error()->code, AX_ERR_DEVICE_LOST);
    AX_CHECK_EQ_U(ax_last_error()->message.len, 0);
}

AX_TEST(long_message_truncates)
{
    char big[1000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    ax_set_error(AX_ERR_UNSUPPORTED, "%s", big);
    AX_CHECK_EQ_U(ax_last_error()->message.len, AX_ERROR_MESSAGE_CAPACITY - 1);
    AX_CHECK_EQ_U(strlen(ax_last_error()->message.ptr), AX_ERROR_MESSAGE_CAPACITY - 1);
}

AX_TEST(truncation_keeps_utf8_sequences_whole)
{
    /* 254 ASCII bytes, then a 3-byte sequence (U+20AC) straddling the 255-byte limit. */
    char big[AX_ERROR_MESSAGE_CAPACITY + 8];
    memset(big, 'a', AX_ERROR_MESSAGE_CAPACITY - 2);
    memcpy(big + AX_ERROR_MESSAGE_CAPACITY - 2, "\xE2\x82\xAC", 3);
    big[AX_ERROR_MESSAGE_CAPACITY + 1] = '\0';

    ax_set_error(AX_ERR_UNSUPPORTED, "%s", big);
    AX_CHECK_EQ_U(ax_last_error()->message.len, AX_ERROR_MESSAGE_CAPACITY - 2);

    /* A sequence that ends exactly at the limit is kept. */
    memset(big, 'a', AX_ERROR_MESSAGE_CAPACITY - 4);
    memcpy(big + AX_ERROR_MESSAGE_CAPACITY - 4, "\xE2\x82\xAC", 3);
    memcpy(big + AX_ERROR_MESSAGE_CAPACITY - 1, "zz", 3);
    ax_set_error(AX_ERR_UNSUPPORTED, "%s", big);
    AX_CHECK_EQ_U(ax_last_error()->message.len, AX_ERROR_MESSAGE_CAPACITY - 1);
}
