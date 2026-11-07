/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: detail for the most recent failure on the calling thread.
 *
 * AXION functions return ax_result. When one returns something other than AX_OK, it may also
 * record a human-readable message, which ax_last_error() returns. The record is per thread and is
 * only meaningful right after a call fails: successful calls do not clear it.
 */
#ifndef AX_ERROR_H
#define AX_ERROR_H

#include "ax_platform.h"
#include "ax_types.h"

AX_EXTERN_C_BEGIN

typedef struct ax_error {
    ax_result code; /* AX_OK when nothing has been recorded on this thread */
    ax_str message; /* UTF-8; also NUL-terminated for convenience. Empty when none. */
} ax_error;

/*
 * The calling thread's last recorded error. Never NULL. The pointer and the message bytes belong
 * to the calling thread and stay valid until the next AXION call on that thread.
 * Callable from any thread.
 */
AX_API const ax_error *ax_last_error(void);

AX_EXTERN_C_END

#endif /* AX_ERROR_H */
