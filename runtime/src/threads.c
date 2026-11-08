/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The scene and render threads of the threaded mode. Not built yet: the next step of M1 adds
 * them; until then the threaded mode is refused.
 */
#include "rt_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>

ax_result ax__rt_start_threads(rt_impl *r)
{
    (void)r;
    return ax_set_error(AX_ERR_UNSUPPORTED,
                        "ax_runtime_create: the threaded mode is not built yet");
}

void ax__rt_stop_threads(rt_impl *r)
{
    (void)r;
}
