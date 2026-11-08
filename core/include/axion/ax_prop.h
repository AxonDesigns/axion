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

    /* Layout: the box model, common to every layout protocol (decision 89). */
    AX_PROP_WIDTH = 0x0101,        /* dimension */
    AX_PROP_HEIGHT = 0x0102,       /* dimension */
    AX_PROP_LEFT = 0x0103,         /* dimension: inset; AUTO = not set */
    AX_PROP_TOP = 0x0104,          /* dimension: inset */
    AX_PROP_RIGHT = 0x0105,        /* dimension: inset */
    AX_PROP_BOTTOM = 0x0106,       /* dimension: inset */
    AX_PROP_MIN_WIDTH = 0x0107,    /* dimension; AUTO = no minimum */
    AX_PROP_MIN_HEIGHT = 0x0108,   /* dimension */
    AX_PROP_MAX_WIDTH = 0x0109,    /* dimension; AUTO = no maximum */
    AX_PROP_MAX_HEIGHT = 0x010A,   /* dimension */
    AX_PROP_POSITION = 0x010B,     /* u32, ax_position */
    AX_PROP_DISPLAY = 0x010C,      /* u32, ax_display: the layout protocol of the children */
    AX_PROP_OVERFLOW = 0x010D,     /* u32, ax_overflow */
    AX_PROP_ASPECT_RATIO = 0x010E, /* f32, width / height; 0 = none */
    AX_PROP_BOX_SIZING = 0x010F,   /* u32, ax_box_sizing */

    /* Layout: flex (the defaults are Yoga's: column, shrink 0, align-content flex-start). */
    AX_PROP_FLEX_DIRECTION = 0x0110,  /* u32, ax_flex_direction */
    AX_PROP_FLEX_WRAP = 0x0111,       /* u32, ax_flex_wrap */
    AX_PROP_FLEX_GROW = 0x0112,       /* f32, >= 0 */
    AX_PROP_FLEX_SHRINK = 0x0113,     /* f32, >= 0 */
    AX_PROP_FLEX_BASIS = 0x0114,      /* dimension; AUTO = the item's size */
    AX_PROP_JUSTIFY_CONTENT = 0x0115, /* u32, ax_justify */
    AX_PROP_ALIGN_ITEMS = 0x0116,     /* u32, ax_align */
    AX_PROP_ALIGN_SELF = 0x0117,      /* u32, ax_align (AUTO: the parent's align-items) */
    AX_PROP_ALIGN_CONTENT = 0x0118,   /* u32, ax_align */
    AX_PROP_COLUMN_GAP = 0x0119,      /* dimension: between columns (along a row); AUTO = 0 */
    AX_PROP_ROW_GAP = 0x011A,         /* dimension: between rows */
    AX_PROP_GAP = 0x011B,             /* shorthand: column and row gap */
    /* 0x011C-0x011F reserved for grid's box alignment (justify-items, justify-self). */

    /* Layout: margins (AUTO = an auto margin), padding (AUTO = 0) and border widths. */
    AX_PROP_MARGIN_LEFT = 0x0120,         /* dimension */
    AX_PROP_MARGIN_TOP = 0x0121,          /* dimension */
    AX_PROP_MARGIN_RIGHT = 0x0122,        /* dimension */
    AX_PROP_MARGIN_BOTTOM = 0x0123,       /* dimension */
    AX_PROP_MARGIN = 0x0124,              /* shorthand: the four margins */
    AX_PROP_PADDING_LEFT = 0x0128,        /* dimension */
    AX_PROP_PADDING_TOP = 0x0129,         /* dimension */
    AX_PROP_PADDING_RIGHT = 0x012A,       /* dimension */
    AX_PROP_PADDING_BOTTOM = 0x012B,      /* dimension */
    AX_PROP_PADDING = 0x012C,             /* shorthand: the four paddings */
    AX_PROP_BORDER_LEFT_WIDTH = 0x0130,   /* f32, points; not drawn yet */
    AX_PROP_BORDER_TOP_WIDTH = 0x0131,    /* f32 */
    AX_PROP_BORDER_RIGHT_WIDTH = 0x0132,  /* f32 */
    AX_PROP_BORDER_BOTTOM_WIDTH = 0x0133, /* f32 */
    AX_PROP_BORDER_WIDTH = 0x0134,        /* shorthand: the four border widths */
    /* Grid starts at 0x0140. */

    /* Not a value: the frame layout computes, as the target of a layout transition
     * (ax_txn_set_transition; decision 94). It cannot be set. */
    AX_PROP_LAYOUT = 0x01FF,

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
    AX_UNIT_PERCENT = 2 /* of the containing block: its width for widths, insets on the x axis,
                         * margins and paddings (all four, as in CSS), its height otherwise */
} ax_unit;

/* A length for layout properties (WIDTH, LEFT...). value is ignored for AX_UNIT_AUTO. */
typedef struct ax_dimension {
    float value;
    uint32_t unit; /* ax_unit */
} ax_dimension;

/* ---- Values of the layout enums (AX_VALUE_U32). Fixed forever; new values are appended. ---- */

/* How a node lays out its children. */
typedef enum ax_display {
    AX_DISPLAY_FLEX = 0, /* default */
    AX_DISPLAY_NONE = 1  /* the node and its subtree take no space and are not drawn */
    /* GRID arrives in M2. */
} ax_display;

typedef enum ax_position {
    AX_POSITION_RELATIVE = 0, /* default: in the flow, offset by its insets */
    AX_POSITION_ABSOLUTE = 1, /* out of the flow, placed by its insets in its containing block */
    AX_POSITION_STATIC = 2    /* in the flow, insets ignored; not a containing block */
} ax_position;

typedef enum ax_overflow {
    AX_OVERFLOW_VISIBLE = 0, /* default */
    AX_OVERFLOW_HIDDEN = 1   /* children are clipped to the node's frame and corner radii */
} ax_overflow;

/* What WIDTH, HEIGHT and their minimums and maximums measure. */
typedef enum ax_box_sizing {
    AX_BOX_SIZING_BORDER_BOX = 0, /* default: padding and border included */
    AX_BOX_SIZING_CONTENT_BOX = 1
} ax_box_sizing;

typedef enum ax_flex_direction {
    AX_FLEX_DIRECTION_COLUMN = 0, /* default (Yoga's) */
    AX_FLEX_DIRECTION_COLUMN_REVERSE = 1,
    AX_FLEX_DIRECTION_ROW = 2,
    AX_FLEX_DIRECTION_ROW_REVERSE = 3
} ax_flex_direction;

typedef enum ax_flex_wrap {
    AX_FLEX_WRAP_NO_WRAP = 0, /* default */
    AX_FLEX_WRAP_WRAP = 1,
    AX_FLEX_WRAP_WRAP_REVERSE = 2
} ax_flex_wrap;

/* JUSTIFY_CONTENT: distribution along the main axis. */
typedef enum ax_justify {
    AX_JUSTIFY_FLEX_START = 0, /* default */
    AX_JUSTIFY_CENTER = 1,
    AX_JUSTIFY_FLEX_END = 2,
    AX_JUSTIFY_SPACE_BETWEEN = 3,
    AX_JUSTIFY_SPACE_AROUND = 4,
    AX_JUSTIFY_SPACE_EVENLY = 5
} ax_justify;

/* ALIGN_ITEMS (default STRETCH), ALIGN_SELF (default AUTO), ALIGN_CONTENT (default FLEX_START). */
typedef enum ax_align {
    AX_ALIGN_AUTO = 0,
    AX_ALIGN_FLEX_START = 1,
    AX_ALIGN_CENTER = 2,
    AX_ALIGN_FLEX_END = 3,
    AX_ALIGN_STRETCH = 4,
    AX_ALIGN_BASELINE = 5, /* laid out as FLEX_START until text gives nodes a baseline */
    AX_ALIGN_SPACE_BETWEEN = 6,
    AX_ALIGN_SPACE_AROUND = 7,
    AX_ALIGN_SPACE_EVENLY = 8
} ax_align;

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
