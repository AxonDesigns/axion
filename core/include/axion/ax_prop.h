/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: properties of nodes, their ids and their value types.
 *
 * Every property has a stable id, a value type, flags (inherited, animatable) and an effect class
 * that decides how much of the frame pipeline a change touches:
 *   - LAYOUT: style, layout, paint record, composite (width, padding, flex, font size...);
 *   - PAINT: the node's paint record, composite (background, border color, corner radius...);
 *   - COMPOSITE: composite only (transform, opacity...).
 *
 * Ids are fixed forever:
 *   0x00000001 - 0x00007FFF  core properties, declared here; new ones are appended
 *   0x00008000 - 0x0000FFFF  properties generated from the IDL (kit components)
 *   0x00010000 and above     properties registered at run time by name (plugins)
 */
#ifndef AX_PROP_H
#define AX_PROP_H

#include <stdint.h>

#include <axion/ax_config.h>

AX_EXTERN_C_BEGIN

typedef uint32_t ax_prop;

/* Core property ids. The high byte groups them by area; it carries no meaning at run time. */
enum {
    AX_PROP_INVALID = 0,

    /* Layout: size and position (absolute insets). Flex arrives at 0x0110, text at 0x0400. */
    AX_PROP_WIDTH = 0x0101,  /* dimension */
    AX_PROP_HEIGHT = 0x0102, /* dimension */
    AX_PROP_LEFT = 0x0103,   /* dimension */
    AX_PROP_TOP = 0x0104,    /* dimension */

    /* Paint. */
    AX_PROP_BACKGROUND_COLOR = 0x0201,           /* color */
    AX_PROP_CORNER_RADIUS_TOP_LEFT = 0x0202,     /* f32, logical units */
    AX_PROP_CORNER_RADIUS_TOP_RIGHT = 0x0203,    /* f32 */
    AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT = 0x0204, /* f32 */
    AX_PROP_CORNER_RADIUS_BOTTOM_LEFT = 0x0205,  /* f32 */
    /* Shorthand: setting it sets the four corners. Reading it gives their value when the four
     * are equal, and fails with AX_ERR_INVALID_ARGUMENT when they differ. */
    AX_PROP_CORNER_RADIUS = 0x0206, /* f32 */

    /* Composite. */
    AX_PROP_OPACITY = 0x0301, /* f32, 0..1 */
    /* transform2d, applied about the center of the node's frame (CSS's default transform
     * origin): the frame is translated so its center is at the origin, transformed, and
     * translated back. A TRANSFORM_ORIGIN property will move that point later. */
    AX_PROP_TRANSFORM = 0x0302
};

#define AX_PROP_FIRST_IDL     0x00008000u
#define AX_PROP_FIRST_DYNAMIC 0x00010000u

/* Value types. Fixed forever; new types are appended. */
typedef enum ax_value_type {
    AX_VALUE_F32 = 1,
    AX_VALUE_DIMENSION = 2,
    AX_VALUE_VEC2 = 3,
    AX_VALUE_COLOR = 4,
    AX_VALUE_TRANSFORM2D = 5,
    AX_VALUE_U32 = 6,
    AX_VALUE_BOOL = 7
    /* 8 is reserved for strings (class names, text content). */
} ax_value_type;

typedef enum ax_effect_class {
    AX_EFFECT_LAYOUT = 1,
    AX_EFFECT_PAINT = 2,
    AX_EFFECT_COMPOSITE = 3
} ax_effect_class;

/* Property flags. */
#define AX_PROP_INHERITED  0x1u /* descendants take the value unless they set their own */
#define AX_PROP_ANIMATABLE 0x2u /* changes animate implicitly (once animations exist) */
#define AX_PROP_SHORTHAND  0x4u /* sets several longhand properties; has no storage of its own */

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

typedef struct ax_vec2f {
    float x, y;
} ax_vec2f;

/*
 * A color as AXION stores it: linear light, premultiplied alpha, extended range (components may
 * be below 0 or above 1), in the sRGB / Rec. 709 primaries. Convert sRGB values with
 * ax_rgba_from_srgb (once the transaction API lands).
 */
typedef struct ax_rgba {
    float r, g, b, a;
} ax_rgba;

/* A 2D affine transform: x' = a*x + c*y + tx, y' = b*x + d*y + ty (CSS matrix(a, b, c, d, tx, ty)).
 * With y pointing down, a positive rotation turns clockwise on screen. */
typedef struct ax_transform2d {
    float a, b, c, d, tx, ty;
} ax_transform2d;

AX_EXTERN_C_END

#endif /* AX_PROP_H */
