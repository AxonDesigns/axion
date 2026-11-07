/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The property registry: the built-in table and properties registered at run time.
 */
#include <float.h>
#include <stddef.h>
#include <string.h>

#include <axion/base/ax_assert.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_prop_registry.h>

#define ANIM AX_PROP_ANIMATABLE

/* Designated initializers keep each row readable; unnamed fields are zero. */
#define DIM_PROP(id_, name_, field_)                      \
    {.id = (id_),                                         \
     .name = (name_),                                     \
     .type = AX_VALUE_DIMENSION,                          \
     .flags = ANIM,                                       \
     .effect = AX_EFFECT_LAYOUT,                          \
     .storage = AX_PROP_IN_STYLE,                         \
     .offset = (uint32_t)offsetof(ax_node_style, field_), \
     .min_value = -FLT_MAX,                               \
     .max_value = FLT_MAX}

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

/* Sorted by id. Defaults match the store's row defaults (ax_node_store.c, init_row). */
static const ax_prop_desc BUILTINS[] = {
    {.id = AX_PROP_WIDTH,
     .name = "width",
     .type = AX_VALUE_DIMENSION,
     .flags = ANIM,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_IN_STYLE,
     .offset = (uint32_t)offsetof(ax_node_style, width),
     .min_value = 0.0f,
     .max_value = FLT_MAX},
    {.id = AX_PROP_HEIGHT,
     .name = "height",
     .type = AX_VALUE_DIMENSION,
     .flags = ANIM,
     .effect = AX_EFFECT_LAYOUT,
     .storage = AX_PROP_IN_STYLE,
     .offset = (uint32_t)offsetof(ax_node_style, height),
     .min_value = 0.0f,
     .max_value = FLT_MAX},
    DIM_PROP(AX_PROP_LEFT, "left", left),
    DIM_PROP(AX_PROP_TOP, "top", top),
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
};

enum { BUILTIN_COUNT = (int)(sizeof BUILTINS / sizeof BUILTINS[0]) };

/* Built-in ids must stay inside the core range, sorted, for the binary search. */
AX_STATIC_ASSERT(AX_PROP_TRANSFORM < AX_PROP_FIRST_IDL, "built-in ids are core ids");

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
    return t >= AX_VALUE_F32 && t <= AX_VALUE_BOOL;
}

static bool valid_effect(ax_effect_class e)
{
    return e >= AX_EFFECT_LAYOUT && e <= AX_EFFECT_COMPOSITE;
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
