/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The property registry: the built-in table and properties registered at run time.
 */
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <axion/base/ax_assert.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_a11y.h>
#include <axion/core/ax_prop_registry.h>
#include <axion/core/ax_text_node.h>
#include <axion/text/ax_text.h>

#define ANIM AX_PROP_ANIMATABLE

/* Designated initializers keep each row readable; unnamed fields are zero. */
#define STYLE_AT(field_) ((uint32_t)offsetof(ax_node_style, field_))
#define EDGE_AT(array_, edge_) \
    ((uint32_t)(offsetof(ax_node_style, array_) + sizeof(ax_dimension) * (edge_)))

/* A dimension in the style column; min_ is the lowest value in points or percent. */
#define DIM_AT(id_, name_, offset_, min_) \
    {.id = (id_),                         \
     .name = (name_),                     \
     .type = AX_VALUE_DIMENSION,          \
     .flags = ANIM,                       \
     .effect = AX_EFFECT_LAYOUT,          \
     .storage = AX_PROP_IN_STYLE,         \
     .offset = (offset_),                 \
     .min_value = (min_),                 \
     .max_value = FLT_MAX}

/* A dimension whose default is 0 points (margins). */
#define DIM_ZERO_AT(id_, name_, offset_)                     \
    {.id = (id_),                                            \
     .name = (name_),                                        \
     .type = AX_VALUE_DIMENSION,                             \
     .flags = ANIM,                                          \
     .effect = AX_EFFECT_LAYOUT,                             \
     .storage = AX_PROP_IN_STYLE,                            \
     .offset = (offset_),                                    \
     .default_value = {.dimension = {0.0f, AX_UNIT_POINTS}}, \
     .min_value = -FLT_MAX,                                  \
     .max_value = FLT_MAX}

/* An f32 in the style column. */
#define F32_AT(id_, name_, offset_, def_) \
    {.id = (id_),                         \
     .name = (name_),                     \
     .type = AX_VALUE_F32,                \
     .flags = ANIM,                       \
     .effect = AX_EFFECT_LAYOUT,          \
     .storage = AX_PROP_IN_STYLE,         \
     .offset = (offset_),                 \
     .default_value = {.f32 = (def_)},    \
     .min_value = 0.0f,                   \
     .max_value = FLT_MAX}

/* A layout enumeration, one byte in the style column; values 0..max_. */
#define ENUM_AT(id_, name_, field_, def_, max_) \
    {.id = (id_),                               \
     .name = (name_),                           \
     .type = AX_VALUE_U32,                      \
     .effect = AX_EFFECT_LAYOUT,                \
     .storage = AX_PROP_IN_STYLE,               \
     .offset = STYLE_AT(field_),                \
     .default_value = {.u32 = (def_)},          \
     .min_value = 0.0f,                         \
     .max_value = (float)(max_),                \
     .stored_u8 = true}

#define SHORTHAND4(id_, name_, type_, min_, a_, b_, c_, d_) \
    {.id = (id_),                                           \
     .name = (name_),                                       \
     .type = (type_),                                       \
     .flags = ANIM | AX_PROP_SHORTHAND,                     \
     .effect = AX_EFFECT_LAYOUT,                            \
     .storage = AX_PROP_EXPANDS,                            \
     .min_value = (min_),                                   \
     .max_value = FLT_MAX,                                  \
     .longhands = {(a_), (b_), (c_), (d_)},                 \
     .longhand_count = 4}

#define RADIUS_PROP(id_, name_, corner_)                                                       \
    {.id = (id_),                                                                              \
     .name = (name_),                                                                          \
     .type = AX_VALUE_F32,                                                                     \
     .flags = ANIM,                                                                            \
     .effect = AX_EFFECT_PAINT,                                                                \
     .storage = AX_PROP_IN_PAINT,                                                              \
     .offset = (uint32_t)(offsetof(ax_node_paint, corner_radius) + sizeof(float) * (corner_)), \
     .min_value = 0.0f,                                                                        \
     .max_value = FLT_MAX}

/* Text properties, in the text component (ax_node_text). */
#define TEXT_AT(field_) ((uint32_t)offsetof(ax_node_text, field_))
#define TEXT_F32(id_, name_, field_, def_, min_, max_) \
    {.id = (id_),                                       \
     .name = (name_),                                   \
     .type = AX_VALUE_F32,                              \
     .flags = AX_PROP_INHERITED,                        \
     .effect = AX_EFFECT_LAYOUT,                        \
     .storage = AX_PROP_IN_TEXT,                        \
     .offset = TEXT_AT(field_),                         \
     .default_value = {.f32 = (def_)},                  \
     .min_value = (min_),                               \
     .max_value = (max_)}
#define TEXT_ENUM(id_, name_, field_, effect_, max_) \
    {.id = (id_),                                     \
     .name = (name_),                                 \
     .type = AX_VALUE_U32,                            \
     .flags = AX_PROP_INHERITED,                      \
     .effect = (effect_),                             \
     .storage = AX_PROP_IN_TEXT,                      \
     .offset = TEXT_AT(field_),                       \
     .min_value = 0.0f,                               \
     .max_value = (float)(max_)}

/* Semantic properties (decision 108), in the semantics component (ax_node_semantics). */
#define SEM_AT(field_) ((uint32_t)offsetof(ax_node_semantics, field_))
#define SEM(id_, name_, type_, field_)  \
    {.id = (id_),                       \
     .name = (name_),                   \
     .type = (type_),                   \
     .effect = AX_EFFECT_SEMANTICS,     \
     .storage = AX_PROP_IN_SEMANTICS,   \
     .offset = SEM_AT(field_)}
#define SEM_RANGE(id_, name_, type_, field_, min_, max_) \
    {.id = (id_),                                        \
     .name = (name_),                                    \
     .type = (type_),                                    \
     .effect = AX_EFFECT_SEMANTICS,                      \
     .storage = AX_PROP_IN_SEMANTICS,                    \
     .offset = SEM_AT(field_),                           \
     .min_value = (min_),                                \
     .max_value = (max_)}

#define BORDER_AT(edge_) ((uint32_t)(offsetof(ax_node_style, border) + sizeof(float) * (edge_)))

/* Sorted by id. Defaults match the store's row defaults (ax_node_store.c, init_row). */
static const ax_prop_desc BUILTINS[] = {
    DIM_AT(AX_PROP_WIDTH, "width", STYLE_AT(width), 0.0f),
    DIM_AT(AX_PROP_HEIGHT, "height", STYLE_AT(height), 0.0f),
    DIM_AT(AX_PROP_LEFT, "left", EDGE_AT(inset, AX_EDGE_LEFT), -FLT_MAX),
    DIM_AT(AX_PROP_TOP, "top", EDGE_AT(inset, AX_EDGE_TOP), -FLT_MAX),
    DIM_AT(AX_PROP_RIGHT, "right", EDGE_AT(inset, AX_EDGE_RIGHT), -FLT_MAX),
    DIM_AT(AX_PROP_BOTTOM, "bottom", EDGE_AT(inset, AX_EDGE_BOTTOM), -FLT_MAX),
    DIM_AT(AX_PROP_MIN_WIDTH, "min-width", STYLE_AT(min_width), 0.0f),
    DIM_AT(AX_PROP_MIN_HEIGHT, "min-height", STYLE_AT(min_height), 0.0f),
    DIM_AT(AX_PROP_MAX_WIDTH, "max-width", STYLE_AT(max_width), 0.0f),
    DIM_AT(AX_PROP_MAX_HEIGHT, "max-height", STYLE_AT(max_height), 0.0f),
    ENUM_AT(AX_PROP_POSITION, "position", position, AX_POSITION_RELATIVE, AX_POSITION_STATIC),
    ENUM_AT(AX_PROP_DISPLAY, "display", display, AX_DISPLAY_FLEX, AX_DISPLAY_NONE),
    ENUM_AT(AX_PROP_OVERFLOW, "overflow", overflow, AX_OVERFLOW_VISIBLE, AX_OVERFLOW_HIDDEN),
    F32_AT(AX_PROP_ASPECT_RATIO, "aspect-ratio", STYLE_AT(aspect_ratio), 0.0f),
    ENUM_AT(AX_PROP_BOX_SIZING, "box-sizing", box_sizing, AX_BOX_SIZING_BORDER_BOX,
            AX_BOX_SIZING_CONTENT_BOX),
    ENUM_AT(AX_PROP_FLEX_DIRECTION, "flex-direction", flex_direction, AX_FLEX_DIRECTION_COLUMN,
            AX_FLEX_DIRECTION_ROW_REVERSE),
    ENUM_AT(AX_PROP_FLEX_WRAP, "flex-wrap", flex_wrap, AX_FLEX_WRAP_NO_WRAP,
            AX_FLEX_WRAP_WRAP_REVERSE),
    F32_AT(AX_PROP_FLEX_GROW, "flex-grow", STYLE_AT(flex_grow), 0.0f),
    F32_AT(AX_PROP_FLEX_SHRINK, "flex-shrink", STYLE_AT(flex_shrink), 0.0f),
    DIM_AT(AX_PROP_FLEX_BASIS, "flex-basis", STYLE_AT(flex_basis), 0.0f),
    ENUM_AT(AX_PROP_JUSTIFY_CONTENT, "justify-content", justify_content, AX_JUSTIFY_FLEX_START,
            AX_JUSTIFY_SPACE_EVENLY),
    ENUM_AT(AX_PROP_ALIGN_ITEMS, "align-items", align_items, AX_ALIGN_STRETCH,
            AX_ALIGN_SPACE_EVENLY),
    ENUM_AT(AX_PROP_ALIGN_SELF, "align-self", align_self, AX_ALIGN_AUTO, AX_ALIGN_SPACE_EVENLY),
    ENUM_AT(AX_PROP_ALIGN_CONTENT, "align-content", align_content, AX_ALIGN_FLEX_START,
            AX_ALIGN_SPACE_EVENLY),
    DIM_AT(AX_PROP_COLUMN_GAP, "column-gap", EDGE_AT(gap, 0), 0.0f),
    DIM_AT(AX_PROP_ROW_GAP, "row-gap", EDGE_AT(gap, 1), 0.0f),
    {.id = AX_PROP_GAP,
     .name = "gap",
     .type = AX_VALUE_DIMENSION,
     .flags = ANIM | AX_PROP_SHORTHAND,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_EXPANDS,
     .min_value = 0.0f,
     .max_value = FLT_MAX,
     .longhands = {AX_PROP_COLUMN_GAP, AX_PROP_ROW_GAP},
     .longhand_count = 2},
    DIM_ZERO_AT(AX_PROP_MARGIN_LEFT, "margin-left", EDGE_AT(margin, AX_EDGE_LEFT)),
    DIM_ZERO_AT(AX_PROP_MARGIN_TOP, "margin-top", EDGE_AT(margin, AX_EDGE_TOP)),
    DIM_ZERO_AT(AX_PROP_MARGIN_RIGHT, "margin-right", EDGE_AT(margin, AX_EDGE_RIGHT)),
    DIM_ZERO_AT(AX_PROP_MARGIN_BOTTOM, "margin-bottom", EDGE_AT(margin, AX_EDGE_BOTTOM)),
    SHORTHAND4(AX_PROP_MARGIN, "margin", AX_VALUE_DIMENSION, -FLT_MAX, AX_PROP_MARGIN_LEFT,
               AX_PROP_MARGIN_TOP, AX_PROP_MARGIN_RIGHT, AX_PROP_MARGIN_BOTTOM),
    DIM_AT(AX_PROP_PADDING_LEFT, "padding-left", EDGE_AT(padding, AX_EDGE_LEFT), 0.0f),
    DIM_AT(AX_PROP_PADDING_TOP, "padding-top", EDGE_AT(padding, AX_EDGE_TOP), 0.0f),
    DIM_AT(AX_PROP_PADDING_RIGHT, "padding-right", EDGE_AT(padding, AX_EDGE_RIGHT), 0.0f),
    DIM_AT(AX_PROP_PADDING_BOTTOM, "padding-bottom", EDGE_AT(padding, AX_EDGE_BOTTOM), 0.0f),
    SHORTHAND4(AX_PROP_PADDING, "padding", AX_VALUE_DIMENSION, 0.0f, AX_PROP_PADDING_LEFT,
               AX_PROP_PADDING_TOP, AX_PROP_PADDING_RIGHT, AX_PROP_PADDING_BOTTOM),
    F32_AT(AX_PROP_BORDER_LEFT_WIDTH, "border-left-width", BORDER_AT(AX_EDGE_LEFT), 0.0f),
    F32_AT(AX_PROP_BORDER_TOP_WIDTH, "border-top-width", BORDER_AT(AX_EDGE_TOP), 0.0f),
    F32_AT(AX_PROP_BORDER_RIGHT_WIDTH, "border-right-width", BORDER_AT(AX_EDGE_RIGHT), 0.0f),
    F32_AT(AX_PROP_BORDER_BOTTOM_WIDTH, "border-bottom-width", BORDER_AT(AX_EDGE_BOTTOM), 0.0f),
    SHORTHAND4(AX_PROP_BORDER_WIDTH, "border-width", AX_VALUE_F32, 0.0f, AX_PROP_BORDER_LEFT_WIDTH,
               AX_PROP_BORDER_TOP_WIDTH, AX_PROP_BORDER_RIGHT_WIDTH, AX_PROP_BORDER_BOTTOM_WIDTH),
    {.id = AX_PROP_BACKGROUND_COLOR,
     .name = "background-color",
     .type = AX_VALUE_COLOR,
     .flags = ANIM,
     .effect = AX_EFFECT_PAINT,
     .storage = AX_PROP_IN_PAINT,
     .offset = (uint32_t)offsetof(ax_node_paint, background)},
    RADIUS_PROP(AX_PROP_CORNER_RADIUS_TOP_LEFT, "corner-radius-top-left", 0),
    RADIUS_PROP(AX_PROP_CORNER_RADIUS_TOP_RIGHT, "corner-radius-top-right", 1),
    RADIUS_PROP(AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, "corner-radius-bottom-right", 2),
    RADIUS_PROP(AX_PROP_CORNER_RADIUS_BOTTOM_LEFT, "corner-radius-bottom-left", 3),
    {.id = AX_PROP_CORNER_RADIUS,
     .name = "corner-radius",
     .type = AX_VALUE_F32,
     .flags = ANIM | AX_PROP_SHORTHAND,
     .effect = AX_EFFECT_PAINT,
     .storage = AX_PROP_EXPANDS,
     .min_value = 0.0f,
     .max_value = FLT_MAX,
     .longhands = {AX_PROP_CORNER_RADIUS_TOP_LEFT, AX_PROP_CORNER_RADIUS_TOP_RIGHT,
                   AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, AX_PROP_CORNER_RADIUS_BOTTOM_LEFT},
     .longhand_count = 4},
    {.id = AX_PROP_OPACITY,
     .name = "opacity",
     .type = AX_VALUE_F32,
     .flags = ANIM,
     .effect = AX_EFFECT_COMPOSITE,
     .storage = AX_PROP_IN_COMPOSITE,
     .offset = (uint32_t)offsetof(ax_node_composite, opacity),
     .default_value = {.f32 = 1.0f},
     .min_value = 0.0f,
     .max_value = 1.0f},
    {.id = AX_PROP_TRANSFORM,
     .name = "transform",
     .type = AX_VALUE_TRANSFORM2D,
     .flags = ANIM,
     .effect = AX_EFFECT_COMPOSITE,
     .storage = AX_PROP_IN_COMPOSITE,
     .offset = (uint32_t)offsetof(ax_node_composite, transform),
     .default_value = {.transform2d = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}},
    /* Text (decision 99): in the text component. */
    {.id = AX_PROP_TEXT,
     .name = "text",
     .type = AX_VALUE_STRING,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_IN_TEXT,
     .offset = TEXT_AT(content)},
    {.id = AX_PROP_FONT_FAMILY,
     .name = "font-family",
     .type = AX_VALUE_STRING,
     .flags = AX_PROP_INHERITED,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_IN_TEXT,
     .offset = TEXT_AT(family)},
    TEXT_F32(AX_PROP_FONT_SIZE, "font-size", size, 14.0f, 0.01f, 10000.0f),
    TEXT_F32(AX_PROP_FONT_WEIGHT, "font-weight", weight, 400.0f, 1.0f, 1000.0f),
    TEXT_ENUM(AX_PROP_FONT_STYLE, "font-style", style, AX_EFFECT_LAYOUT,
              AX_FONT_STYLE_VALUE_ITALIC),
    TEXT_F32(AX_PROP_FONT_WIDTH, "font-width", width, 100.0f, 50.0f, 200.0f),
    TEXT_F32(AX_PROP_LINE_HEIGHT, "line-height", line_height, 0.0f, 0.0f, 100.0f),
    {.id = AX_PROP_FONT_FEATURES,
     .name = "font-feature-settings",
     .type = AX_VALUE_STRING,
     .flags = AX_PROP_INHERITED,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_IN_TEXT,
     .offset = TEXT_AT(features)},
    TEXT_ENUM(AX_PROP_TEXT_ALIGN, "text-align", align, AX_EFFECT_PAINT, AX_TEXT_ALIGN_VALUE_END),
    {.id = AX_PROP_COLOR,
     .name = "color",
     .type = AX_VALUE_COLOR,
     .flags = AX_PROP_INHERITED | ANIM,
     .effect = AX_EFFECT_PAINT,
     .storage = AX_PROP_IN_TEXT,
     .offset = TEXT_AT(color),
     .default_value = {.color = {0.0f, 0.0f, 0.0f, 1.0f}}},
    /* Semantics (decision 108): in the semantics component. TEXT_RUN is the tree's own role. */
    SEM_RANGE(AX_PROP_ROLE, "role", AX_VALUE_U32, role, 0.0f, (float)(AX_ROLE_LAST - 1)),
    SEM(AX_PROP_LABEL, "label", AX_VALUE_STRING, label),
    SEM(AX_PROP_DESCRIPTION, "description", AX_VALUE_STRING, description),
    SEM(AX_PROP_TEST_ID, "test-id", AX_VALUE_STRING, test_id),
    SEM(AX_PROP_VALUE_TEXT, "value-text", AX_VALUE_STRING, value_text),
    SEM_RANGE(AX_PROP_VALUE, "value", AX_VALUE_F32, value, -FLT_MAX, FLT_MAX),
    SEM_RANGE(AX_PROP_VALUE_MIN, "value-min", AX_VALUE_F32, value_min, -FLT_MAX, FLT_MAX),
    SEM_RANGE(AX_PROP_VALUE_MAX, "value-max", AX_VALUE_F32, value_max, -FLT_MAX, FLT_MAX),
    SEM_RANGE(AX_PROP_VALUE_STEP, "value-step", AX_VALUE_F32, value_step, 0.0f, FLT_MAX),
    SEM(AX_PROP_DISABLED, "disabled", AX_VALUE_BOOL, disabled),
    SEM(AX_PROP_HIDDEN, "hidden", AX_VALUE_BOOL, hidden),
    SEM(AX_PROP_SELECTED, "selected", AX_VALUE_BOOL, selected),
    SEM_RANGE(AX_PROP_CHECKED, "checked", AX_VALUE_U32, checked, 0.0f, (float)AX_TOGGLED_MIXED),
    SEM_RANGE(AX_PROP_EXPANDED, "expanded", AX_VALUE_U32, expanded, 0.0f,
              (float)AX_EXPANDED_EXPANDED),
    SEM_RANGE(AX_PROP_ACTIONS, "actions", AX_VALUE_U32, actions, 0.0f, (float)AX_ACTION_ALL),
    SEM_RANGE(AX_PROP_LEVEL, "level", AX_VALUE_U32, level, 0.0f, 9.0f),
};

enum { BUILTIN_COUNT = (int)(sizeof BUILTINS / sizeof BUILTINS[0]) };

/* Built-in ids must stay inside the core range, sorted, for the binary search. */
AX_STATIC_ASSERT(AX_PROP_LEVEL < AX_PROP_FIRST_IDL, "built-in ids are core ids");
AX_STATIC_ASSERT(AX_SEMANTICS_LAST - AX_SEMANTICS_FIRST < 32, "one own bit per semantic property");

const ax_prop_desc *ax_prop_registry_builtins(uint32_t *out_count)
{
    if (out_count != NULL) {
        *out_count = BUILTIN_COUNT;
    }
    return BUILTINS;
}

void ax_prop_registry_init(ax_prop_registry *reg, const ax_allocator *allocator)
{
    int i;
    memset(reg, 0, sizeof *reg);
    reg->allocator = *(allocator != NULL ? allocator : ax_default_allocator());
    reg->next_dynamic_id = AX_PROP_FIRST_DYNAMIC;
    for (i = 1; i < BUILTIN_COUNT; ++i) {
        AX_ASSERT_MSG(BUILTINS[i - 1].id < BUILTINS[i].id, "built-in properties must be sorted");
    }
}

void ax_prop_registry_deinit(ax_prop_registry *reg)
{
    ax_mem_free(&reg->allocator, reg->dynamic, sizeof(ax_prop_desc) * reg->dynamic_capacity);
    memset(reg, 0, sizeof *reg);
}

static const ax_prop_desc *search(const ax_prop_desc *descs, uint32_t count, ax_prop id)
{
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (descs[mid].id == id) {
            return &descs[mid];
        }
        if (descs[mid].id < id) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

const ax_prop_desc *ax_prop_registry_find(const ax_prop_registry *reg, ax_prop id)
{
    if (id < AX_PROP_FIRST_IDL) {
        return search(BUILTINS, BUILTIN_COUNT, id);
    }
    return search(reg->dynamic, reg->dynamic_count, id);
}

static bool name_is(const ax_prop_desc *d, ax_str name)
{
    return strlen(d->name) == name.len &&
           (name.len == 0 || memcmp(d->name, name.ptr, name.len) == 0);
}

const ax_prop_desc *ax_prop_registry_find_by_name(const ax_prop_registry *reg, ax_str name)
{
    uint32_t i;
    for (i = 0; i < (uint32_t)BUILTIN_COUNT; ++i) {
        if (name_is(&BUILTINS[i], name)) {
            return &BUILTINS[i];
        }
    }
    for (i = 0; i < reg->dynamic_count; ++i) {
        if (name_is(&reg->dynamic[i], name)) {
            return &reg->dynamic[i];
        }
    }
    return NULL;
}

static bool valid_type(ax_value_type t)
{
    return t >= AX_VALUE_F32 && t <= AX_VALUE_BOOL; /* strings: core properties only, for now */
}

static bool valid_effect(ax_effect_class e)
{
    return e >= AX_EFFECT_LAYOUT && e <= AX_EFFECT_SEMANTICS;
}

ax_result ax_prop_registry_register(ax_prop_registry *reg, ax_node_store *store,
                                    const ax_prop_desc *desc, ax_prop *out_id)
{
    ax_prop_desc d;
    ax_component_desc cd;
    ax_component_type component;
    ax_result r;
    uint32_t at;
    ax_str name;

    AX_ASSERT(reg != NULL && store != NULL && desc != NULL);
    if (out_id != NULL) {
        *out_id = AX_PROP_INVALID;
    }
    if (desc->name == NULL || desc->name[0] == '\0' || !valid_type(desc->type) ||
        !valid_effect(desc->effect) || (desc->flags & AX_PROP_SHORTHAND) != 0 ||
        (desc->flags & ~(AX_PROP_INHERITED | AX_PROP_ANIMATABLE)) != 0) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    if (desc->id != AX_PROP_INVALID &&
        (desc->id < AX_PROP_FIRST_IDL || desc->id >= AX_PROP_FIRST_DYNAMIC ||
         ax_prop_registry_find(reg, desc->id) != NULL)) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    name.ptr = desc->name;
    name.len = strlen(desc->name);
    if (ax_prop_registry_find_by_name(reg, name) != NULL) {
        return AX_ERR_INVALID_ARGUMENT;
    }
    if (desc->id == AX_PROP_INVALID && reg->next_dynamic_id == UINT32_MAX) {
        return AX_ERR_UNSUPPORTED;
    }

    if (reg->dynamic_count == reg->dynamic_capacity) {
        uint32_t cap = reg->dynamic_capacity < 8u ? 8u : reg->dynamic_capacity * 2u;
        ax_prop_desc *fresh = (ax_prop_desc *)ax_mem_alloc(
            &reg->allocator, sizeof(ax_prop_desc) * cap, AX_ALIGNOF(ax_prop_desc));
        if (fresh == NULL) {
            return AX_ERR_OUT_OF_MEMORY;
        }
        if (reg->dynamic != NULL) {
            memcpy(fresh, reg->dynamic, sizeof(ax_prop_desc) * reg->dynamic_count);
            ax_mem_free(&reg->allocator, reg->dynamic,
                        sizeof(ax_prop_desc) * reg->dynamic_capacity);
        }
        reg->dynamic = fresh;
        reg->dynamic_capacity = cap;
    }

    memset(&cd, 0, sizeof cd);
    cd.size = sizeof(ax_value);
    cd.align = AX_ALIGNOF(ax_value);
    r = ax_node_store_register_component(store, &cd, &component);
    if (r != AX_OK) {
        return r;
    }

    d = *desc;
    d.id = desc->id != AX_PROP_INVALID ? desc->id : reg->next_dynamic_id++;
    d.storage = AX_PROP_IN_COMPONENT;
    d.offset = 0;
    d.component = component;
    memset(d.longhands, 0, sizeof d.longhands);
    d.longhand_count = 0;
    if (d.min_value == 0.0f && d.max_value == 0.0f) {
        d.min_value = -FLT_MAX; /* no range given */
        d.max_value = FLT_MAX;
    }

    /* Keep the array sorted: IDL ids sort before dynamic ones. */
    at = reg->dynamic_count;
    while (at > 0 && reg->dynamic[at - 1u].id > d.id) {
        reg->dynamic[at] = reg->dynamic[at - 1u];
        at--;
    }
    reg->dynamic[at] = d;
    reg->dynamic_count++;
    if (out_id != NULL) {
        *out_id = d.id;
    }
    return AX_OK;
}

const ax_prop_desc *ax_prop_builtin(ax_prop id)
{
    return search(BUILTINS, BUILTIN_COUNT, id);
}

/* ---- Validation ---- */

static bool all_finite(const float *f, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        if (!isfinite(f[i])) {
            return false;
        }
    }
    return true;
}

ax_result ax_prop_check_value(const ax_prop_desc *d, ax_value_type type, const ax_value *v)
{
    if (type != d->type) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "property %s: wrong value type %d (expects %d)", d->name, (int)type,
                            (int)d->type);
    }
    switch (type) {
    case AX_VALUE_F32:
        if (!isfinite(v->f32) || v->f32 < d->min_value || v->f32 > d->max_value) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "property %s: %g is not finite or outside [%g, %g]", d->name,
                                (double)v->f32, (double)d->min_value, (double)d->max_value);
        }
        break;
    case AX_VALUE_DIMENSION:
        switch (v->dimension.unit) {
        case AX_UNIT_AUTO:
            break;
        case AX_UNIT_POINTS:
        case AX_UNIT_PERCENT:
            if (!isfinite(v->dimension.value) || v->dimension.value < d->min_value ||
                v->dimension.value > d->max_value) {
                return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                    "property %s: %g %s is not finite or outside [%g, %g]", d->name,
                                    (double)v->dimension.value,
                                    v->dimension.unit == AX_UNIT_POINTS ? "points" : "percent",
                                    (double)d->min_value, (double)d->max_value);
            }
            break;
        default:
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: unknown unit %u", d->name,
                                (unsigned)v->dimension.unit);
        }
        break;
    case AX_VALUE_VEC2:
        if (!all_finite(&v->vec2.x, 2)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: not finite", d->name);
        }
        break;
    case AX_VALUE_COLOR:
        if (!all_finite(&v->color.r, 4)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: color not finite", d->name);
        }
        break;
    case AX_VALUE_TRANSFORM2D:
        if (!all_finite(&v->transform2d.a, 6)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: transform not finite",
                                d->name);
        }
        break;
    case AX_VALUE_U32:
        if (d->min_value != d->max_value &&
            ((double)v->u32 < (double)d->min_value || (double)v->u32 > (double)d->max_value)) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: unknown value %u", d->name,
                                (unsigned)v->u32);
        }
        break;
    case AX_VALUE_BOOL:
        break;
    case AX_VALUE_STRING:
        return ax_prop_check_string(d, ax_shared_str_view(v->str));
    }
    return AX_OK;
}

/* Well-formed UTF-8: no overlongs, no surrogates, nothing above U+10FFFF. */
static bool valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        uint32_t cp;
        size_t k, len;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xC2u && c <= 0xDFu) {
            len = 2;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            len = 3;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            len = 4;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + len > n) {
            return false;
        }
        for (k = 1; k < len; ++k) {
            if ((s[i + k] & 0xC0u) != 0x80u) {
                return false;
            }
            cp = (cp << 6) | (s[i + k] & 0x3Fu);
        }
        if ((len == 3 && cp < 0x800u) || (len == 4 && (cp < 0x10000u || cp > 0x10FFFFu)) ||
            (cp >= 0xD800u && cp <= 0xDFFFu)) {
            return false;
        }
        i += len;
    }
    return true;
}

ax_result ax_prop_check_string(const ax_prop_desc *d, ax_str v)
{
    if (d->type != AX_VALUE_STRING) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                            "property %s: wrong value type %d (expects %d)", d->name,
                            (int)AX_VALUE_STRING, (int)d->type);
    }
    if (v.len > AX_PROP_MAX_STRING) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: %zu bytes (at most %u)",
                            d->name, v.len, (unsigned)AX_PROP_MAX_STRING);
    }
    if (v.len > 0 && (v.ptr == NULL || !valid_utf8((const unsigned char *)v.ptr, v.len))) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "property %s: not valid UTF-8", d->name);
    }
    if (d->id == AX_PROP_FONT_FEATURES) {
        ax_text_feature f[AX_TEXT_MAX_FEATURES];
        ax_result r = AX_OK;
        (void)ax_text_parse_features(v, f, AX_TEXT_MAX_FEATURES, &r);
        if (r != AX_OK) {
            return ax_set_error(AX_ERR_INVALID_ARGUMENT,
                                "property %s: not a list of at most %u features (\"tnum\", "
                                "\"liga\" 0)",
                                d->name, (unsigned)AX_TEXT_MAX_FEATURES);
        }
    }
    return AX_OK;
}
