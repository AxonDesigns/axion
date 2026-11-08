/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: text in the node store (decision 99).
 *
 * The text component. A node gets it when it first sets a text property (AX_PROP_TEXT and the
 * inherited font, line and color properties), so a tree without text pays nothing. It holds:
 *   - the node's own values, with a bit per property it set (`own`): an inherited property the
 *     node did not set takes its parent's computed value;
 *   - the computed values, resolved by the style pass (ax_scene_resolve_styles) for every node
 *     marked STYLE that has the component: its own value, or the nearest ancestor's computed one,
 *     or the default;
 *   - for a node with TEXT (a text leaf), the paragraph text builds from its content and computed
 *     style, which layout measures (decision 90's measure function) and recording draws.
 *
 * thread: scene.
 */
#ifndef AX_CORE_TEXT_NODE_H
#define AX_CORE_TEXT_NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_node.h"
#include "../ax_prop.h"
#include "ax_prop_registry.h"
#include <axion/base/ax_compiler.h>
#include <axion/base/ax_math.h>
#include <axion/base/ax_shared_str.h>

AX_EXTERN_C_BEGIN

struct ax_scene;
struct ax_text_paragraph;

/* The bit of a text property in ax_node_text.own. */
#define AX_TEXT_PROP_BIT(prop) (1u << ((prop) - AX_PROP_TEXT))

/* Inherited text values, as the style pass resolves them. Strings are references the struct
 * holds. */
typedef struct ax_text_computed {
    const ax_shared_str *family;   /* NULL: the system's interface font */
    const ax_shared_str *features; /* NULL: none */
    float size, weight, width, line_height;
    uint32_t style, align;
    ax_color color; /* premultiplied linear light, as stored */
} ax_text_computed;

typedef struct ax_node_text {
    uint32_t own; /* AX_TEXT_PROP_BIT of each property the node set */
    /* Own values (meaningful where `own` has the bit). Strings are references it holds. */
    const ax_shared_str *content;
    const ax_shared_str *family;
    const ax_shared_str *features;
    float size, weight, width, line_height;
    uint32_t style, align;
    ax_color color;
    /* The style pass. */
    ax_text_computed computed;
    uint32_t style_pass; /* the pass that last resolved it (0: never) */
    bool layout_changed; /* the last resolution changed what the paragraph is built from */
    /* The paragraph of a text leaf (text's, decision 99), and what it was built from. */
    struct ax_text_paragraph *paragraph;
    bool paragraph_stale;
} ax_node_text;

/*
 * The style pass: resolves the computed text values of every node marked STYLE that has the text
 * component (each ancestor's first, so a chain resolves top-down once per pass). A node whose
 * computed values or content changed in a way that changes its paragraph gets
 * `paragraph_stale`. Returns the number of nodes resolved. Call it after applying transactions
 * and before layout.
 */
AX_API uint32_t ax_scene_resolve_styles(struct ax_scene *scene);

/* The node's text component, or NULL. */
AX_API ax_node_text *ax_scene_text(const struct ax_scene *scene, ax_node node);

/* A text property's own value and whether the node set it (an inherited property it did not set
 * reads as unset with the default). String values are borrowed. For undoing a change. */
AX_API ax_result ax_scene_get_own(const struct ax_scene *scene, ax_node node, ax_prop prop,
                                  ax_value *out, bool *out_set);

/* Clears the node's own value of a text property (it inherits again), marking as a set would. */
AX_API ax_result ax_scene_unset(struct ax_scene *scene, ax_node node, ax_prop prop);

/* As ax_scene_set for a string property, from bytes (copied into a shared string). */
AX_API ax_result ax_scene_set_str(struct ax_scene *scene, ax_node node, ax_prop prop, ax_str value);

AX_EXTERN_C_END

#endif /* AX_CORE_TEXT_NODE_H */
