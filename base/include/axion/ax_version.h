/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: runtime version.
 *
 * A version packs into 32 bits: major in bits 22..31 (10 bits), minor in bits 12..21 (10 bits),
 * patch in bits 0..11 (12 bits). Packed versions compare correctly with < and >.
 *
 * Bindings compare AX_VERSION (the header they were generated against) with ax_version() (the
 * runtime actually loaded): the major must match, and the runtime minor must be >= the header's,
 * since minor versions only append functions and fields.
 *
 * CMake reads AX_VERSION_MAJOR/MINOR/PATCH from this file; keep each on its own #define line.
 */
#ifndef AX_VERSION_H
#define AX_VERSION_H

#include <stdint.h>

#include "ax_platform.h"

#define AX_VERSION_MAJOR 0
#define AX_VERSION_MINOR 1
#define AX_VERSION_PATCH 0

#define AX_MAKE_VERSION(major, minor, patch)                                       \
    ((((uint32_t)(major) & 0x3FFu) << 22) | (((uint32_t)(minor) & 0x3FFu) << 12) | \
     ((uint32_t)(patch) & 0xFFFu))

#define AX_VERSION_GET_MAJOR(v) (((uint32_t)(v) >> 22) & 0x3FFu)
#define AX_VERSION_GET_MINOR(v) (((uint32_t)(v) >> 12) & 0x3FFu)
#define AX_VERSION_GET_PATCH(v) ((uint32_t)(v) & 0xFFFu)

/* The version these headers describe. */
#define AX_VERSION AX_MAKE_VERSION(AX_VERSION_MAJOR, AX_VERSION_MINOR, AX_VERSION_PATCH)

AX_EXTERN_C_BEGIN

/* The version of the runtime that is actually linked or loaded. Callable from any thread. */
AX_API uint32_t ax_version(void);

AX_EXTERN_C_END

#endif /* AX_VERSION_H */
