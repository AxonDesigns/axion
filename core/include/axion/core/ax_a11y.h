/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: semantics in the node store (decision 108).
 *
 * The semantics component. A node gets it when it first sets a semantic property (AX_PROP_ROLE
 * to AX_PROP_LEVEL, effect SEMANTICS), so a tree without semantics pays nothing. It holds the
 * node's own values and a bit per property it set (`own`): an optional number (VALUE and its
 * range) exists only while its bit is set.
 *
 * thread: scene.
 */
#ifndef AX_CORE_A11Y_H
#define AX_CORE_A11Y_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_prop.h"
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_shared_str.h>

AX_EXTERN_C_BEGIN

struct ax_scene;

/* The bit of a semantic property in ax_node_semantics.own. */
#define AX_SEMANTICS_FIRST        AX_PROP_ROLE
#define AX_SEMANTICS_LAST         AX_PROP_LEVEL
#define AX_SEMANTICS_BIT(prop)    (1u << ((prop) - AX_SEMANTICS_FIRST))
#define AX_IS_SEMANTIC_PROP(prop) ((prop) >= AX_SEMANTICS_FIRST && (prop) <= AX_SEMANTICS_LAST)

typedef struct ax_node_semantics {
    uint32_t own; /* AX_SEMANTICS_BIT of each property the node set */
    uint32_t role;
    uint32_t checked, expanded;
    uint32_t actions;
    uint32_t level;
    /* References the struct holds; NULL is the empty string. */
    const ax_shared_str *label;
    const ax_shared_str *description;
    const ax_shared_str *test_id;
    const ax_shared_str *value_text;
    float value, value_min, value_max, value_step;
    bool disabled, hidden, selected;
} ax_node_semantics;

/* The node's semantics component, or NULL (a node that never set a semantic property). */
AX_API ax_node_semantics *ax_scene_semantics(const struct ax_scene *scene, ax_node node);

AX_EXTERN_C_END

#endif /* AX_CORE_A11Y_H */
