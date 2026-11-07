/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: property value types.
 */
#ifndef AX_PROP_H
#define AX_PROP_H

#include <stdint.h>

#include <axion/ax_config.h>

AX_EXTERN_C_BEGIN

/* Unit of an ax_dimension. Values are fixed forever. The zero value is AUTO, so a zeroed
 * dimension means "not set": layout decides. */
typedef enum ax_unit {
    AX_UNIT_AUTO = 0,
    AX_UNIT_POINTS = 1, /* logical units */
    AX_UNIT_PERCENT = 2 /* of the parent's matching size (once layout exists) */
} ax_unit;

/* A length for layout properties (WIDTH, LEFT...). value is ignored for AX_UNIT_AUTO. */
typedef struct ax_dimension {
    float value;
    uint32_t unit; /* ax_unit */
} ax_dimension;

AX_EXTERN_C_END

#endif /* AX_PROP_H */
