/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the property registry. Describes every property a scene knows: its stable id,
 * name, value type, flags, effect class, where its value lives and its default.
 *
 * Built-in properties (ax_prop.h) are a static table: their values live in the node store's hot
 * columns (style, paint, composite), at a fixed offset. Adding a property group (flex, text) means
 * adding rows to that table and fields to a hot column or a side component.
 *
 * Properties registered at run time (IDL-generated kit properties with a fixed id in
 * 0x8000-0xFFFF, plugin properties that get an id from 0x10000) keep their values in a side
 * component of their own: a node that never set one reads the default.
 *
 * A registry belongs to one scene and its thread.
 */
#ifndef AX_CORE_PROP_REGISTRY_H
#define AX_CORE_PROP_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_prop.h"
#include "ax_node_store.h"
#include <axion/ax_allocator.h>
#include <axion/ax_types.h>
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_math.h>

AX_EXTERN_C_BEGIN

/* A value of any property type. The member is the one its ax_value_type names. */
typedef union ax_value {
    float f32;
    ax_dimension dimension;
    ax_vec2 vec2;
    ax_color color;
    ax_affine2 transform2d;
    uint32_t u32;
    bool boolean;
} ax_value;

/* Where a property's value lives. */
typedef enum ax_prop_storage {
    AX_PROP_IN_STYLE = 1,     /* ax_node_store.style, at `offset` */
    AX_PROP_IN_PAINT = 2,     /* ax_node_store.paint */
    AX_PROP_IN_COMPOSITE = 3, /* ax_node_store.composite */
    AX_PROP_IN_COMPONENT = 4, /* a side component holding one ax_value */
    AX_PROP_EXPANDS = 5       /* a shorthand: no storage, sets its longhands */
} ax_prop_storage;

#define AX_PROP_MAX_LONGHANDS 4u

typedef struct ax_prop_desc {
    ax_prop id;
    const char *name; /* static, NUL-terminated, unique ("width", "background-color") */
    ax_value_type type;
    uint32_t flags; /* AX_PROP_INHERITED | AX_PROP_ANIMATABLE | AX_PROP_SHORTHAND */
    ax_effect_class effect;
    ax_prop_storage storage;
    uint32_t offset;             /* in the hot column's struct */
    ax_component_type component; /* AX_PROP_IN_COMPONENT */
    ax_value default_value;
    /* Accepted range of F32 values and of DIMENSION values in points (inclusive). */
    float min_value;
    float max_value;
    ax_prop longhands[AX_PROP_MAX_LONGHANDS]; /* AX_PROP_EXPANDS */
    uint32_t longhand_count;
} ax_prop_desc;

typedef struct ax_prop_registry {
    ax_allocator allocator;
    ax_prop_desc *dynamic; /* registered at run time, sorted by id */
    uint32_t dynamic_count;
    uint32_t dynamic_capacity;
    ax_prop next_dynamic_id;
} ax_prop_registry;

/* Never fails: the built-in properties need no memory. */
AX_API void ax_prop_registry_init(ax_prop_registry *reg, const ax_allocator *allocator);
AX_API void ax_prop_registry_deinit(ax_prop_registry *reg);

/* The descriptor of a property, or NULL when the id is unknown. */
AX_API const ax_prop_desc *ax_prop_registry_find(const ax_prop_registry *reg, ax_prop id);

/* By name (exact match), or NULL. */
AX_API const ax_prop_desc *ax_prop_registry_find_by_name(const ax_prop_registry *reg, ax_str name);

/* A built-in property's descriptor, or NULL. Needs no registry: callable from any thread. */
AX_API const ax_prop_desc *ax_prop_builtin(ax_prop id);

/*
 * Checks a value against a descriptor: the type must be the property's, numbers finite and in
 * the property's range, units known. AX_ERR_INVALID_ARGUMENT, or AX_ERR_UNSUPPORTED for a
 * percentage until layout exists; records ax_last_error with the property's name. Pure: callable
 * from any thread.
 */
AX_API ax_result ax_prop_check_value(const ax_prop_desc *desc, ax_value_type type,
                                     const ax_value *value);

/* The built-in descriptors, sorted by id (for tests and tools). */
AX_API const ax_prop_desc *ax_prop_registry_builtins(uint32_t *out_count);

/*
 * Registers a property whose values live in a new side component of `store`. desc->id is 0 for a
 * plugin property (an id from AX_PROP_FIRST_DYNAMIC is assigned) or an id in
 * [AX_PROP_FIRST_IDL, AX_PROP_FIRST_DYNAMIC) for an IDL property. storage, offset, component and
 * the longhands are ignored. desc->name must stay valid while the registry lives.
 * Errors: AX_ERR_INVALID_ARGUMENT (no name, a name or id already used, an id outside the IDL
 * range, an unknown type or effect, a shorthand), AX_ERR_UNSUPPORTED (no side component type
 * left), AX_ERR_OUT_OF_MEMORY.
 */
AX_API ax_result ax_prop_registry_register(ax_prop_registry *reg, ax_node_store *store,
                                           const ax_prop_desc *desc, ax_prop *out_id);

AX_EXTERN_C_END

#endif /* AX_CORE_PROP_REGISTRY_H */
