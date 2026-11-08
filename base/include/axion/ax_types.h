/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: result codes and strings shared by every module.
 */
#ifndef AX_TYPES_H
#define AX_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ax_config.h"

AX_EXTERN_C_BEGIN

/*
 * Result of every fallible AXION call. Detail for the most recent failure on the calling thread
 * is available from ax_last_error().
 *
 * ABI rule: values are fixed forever. New codes are only ever appended at the end.
 */
typedef enum ax_result {
    AX_OK = 0,
    AX_ERR_INVALID_HANDLE = 1,
    AX_ERR_WRONG_THREAD = 2,
    AX_ERR_OUT_OF_MEMORY = 3,
    AX_ERR_DEVICE_LOST = 4,
    AX_ERR_UNSUPPORTED = 5,
    AX_ERR_ABI_MISMATCH = 6,
    AX_ERR_INVALID_ARGUMENT = 7, /* malformed input: a bad value, a command or structure that
                                    breaks the API's rules; ax_last_error() says which */
    AX_ERR_TIMEOUT = 8, /* what was waited for did not happen in time, or a resource is not free
                           now; trying again may succeed */
    AX_ERR_IO = 9       /* a file or stream could not be opened, read or written; ax_last_error()
                           names it and says why */
} ax_result;

/* Stable, NUL-terminated name of a result code ("AX_ERR_OUT_OF_MEMORY"), or "AX_ERR_<unknown>". */
AX_API const char *ax_result_name(ax_result result);

/*
 * A UTF-8 string slice. Not NUL-terminated: always use len. ptr may be NULL only when len is 0.
 * The struct never owns its bytes; whoever hands one out documents how long they stay valid.
 */
typedef struct ax_str {
    const char *ptr;
    size_t len;
} ax_str;

/*
 * AX_STR("literal") makes an ax_str from a string literal; anything else fails to compile.
 * In C it is a compound literal, so it works in expressions and in initializers of automatic
 * variables. For static initializers use AX_STR_INIT("literal").
 */
#define AX_STR_INIT(lit) {"" lit "", sizeof("" lit "") - 1}
#ifdef __cplusplus
#    define AX_STR(lit) (ax_str AX_STR_INIT(lit))
#else
#    define AX_STR(lit) ((ax_str)AX_STR_INIT(lit))
#endif

AX_EXTERN_C_END

#endif /* AX_TYPES_H */
