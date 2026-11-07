/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "axion/ax_error.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_error_set.h"

typedef struct ax_error_slot {
    ax_error error;
    char buffer[AX_ERROR_MESSAGE_CAPACITY];
} ax_error_slot;

/* Zero-initialized: code AX_OK, empty message. The message pointer is fixed up on each write and
 * read, since a thread-local's address is only known on the owning thread. */
static AX_THREAD_LOCAL ax_error_slot ax_tls_error;

const ax_error *ax_last_error(void)
{
    ax_error_slot *slot = &ax_tls_error;
    slot->error.message.ptr = slot->buffer;
    return &slot->error;
}

/* Length of the longest prefix of buf[0..len) that does not end inside a UTF-8 sequence. */
static size_t utf8_complete_prefix(const char *buf, size_t len)
{
    size_t lead = len;
    size_t back = 0;
    size_t need;
    unsigned char c;

    /* Step back over at most three continuation bytes (10xxxxxx) to the lead byte. */
    while (lead > 0 && back < 3 && ((unsigned char)buf[lead - 1] & 0xC0u) == 0x80u) {
        --lead;
        ++back;
    }
    if (lead == 0) {
        return back == 0 ? len : 0;
    }
    c = (unsigned char)buf[lead - 1];
    if (c < 0x80u) {
        need = 1;
    } else if ((c & 0xE0u) == 0xC0u) {
        need = 2;
    } else if ((c & 0xF0u) == 0xE0u) {
        need = 3;
    } else if ((c & 0xF8u) == 0xF0u) {
        need = 4;
    } else {
        return len; /* not valid UTF-8 to begin with; leave it alone */
    }
    return (back + 1 < need) ? lead - 1 : len;
}

ax_result ax_set_error_v(ax_result code, const char *fmt, va_list args)
{
    ax_error_slot *slot = &ax_tls_error;
    size_t len = 0;

    if (fmt != NULL) {
        int n = vsnprintf(slot->buffer, sizeof slot->buffer, fmt, args);
        if (n < 0) {
            n = 0; /* encoding error: keep an empty message rather than garbage */
        }
        if ((size_t)n < sizeof slot->buffer) {
            len = (size_t)n;
        } else {
            /* Truncated: never end the message in the middle of a UTF-8 sequence. */
            len = utf8_complete_prefix(slot->buffer, sizeof slot->buffer - 1);
        }
    }
    slot->buffer[len] = '\0';
    slot->error.code = code;
    slot->error.message.ptr = slot->buffer;
    slot->error.message.len = len;
    return code;
}

ax_result ax_set_error(ax_result code, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ax_set_error_v(code, fmt, args);
    va_end(args);
    return code;
}

void ax_clear_error(void)
{
    ax_error_slot *slot = &ax_tls_error;
    slot->buffer[0] = '\0';
    slot->error.code = AX_OK;
    slot->error.message.ptr = slot->buffer;
    slot->error.message.len = 0;
}
