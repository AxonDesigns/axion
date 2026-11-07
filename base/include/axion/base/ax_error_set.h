/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: recording the thread-local error that ax_last_error() reports.
 *
 * Typical use at a public entry point:
 *
 *     if (!ok) {
 *         return ax_set_error(AX_ERR_INVALID_HANDLE, "stale ax_node (slot %u)", slot);
 *     }
 *
 * Messages are formatted into a fixed per-thread buffer (AX_ERROR_MESSAGE_CAPACITY bytes,
 * including the NUL) and truncated if longer. Recording never allocates and never fails.
 */
#ifndef AX_BASE_ERROR_SET_H
#define AX_BASE_ERROR_SET_H

#include <stdarg.h>

#include "../ax_error.h"
#include "ax_compiler.h"

#define AX_ERROR_MESSAGE_CAPACITY 256

AX_EXTERN_C_BEGIN

/* Records code and a printf-style message for this thread, and returns code. fmt may be NULL. */
AX_API ax_result ax_set_error(ax_result code, const char *fmt, ...) AX_PRINTF_LIKE(2, 3);
AX_API ax_result ax_set_error_v(ax_result code, const char *fmt, va_list args);

/* Resets this thread's record to AX_OK with an empty message. */
AX_API void ax_clear_error(void);

AX_EXTERN_C_END

#endif /* AX_BASE_ERROR_SET_H */
