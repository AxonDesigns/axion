/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The accessibility adapter through AccessKit's C API (decision 108): on Linux, its AT-SPI adapter
 * (accesskit_unix), which talks D-Bus from threads of its own. Updates are turned into
 * accesskit_tree_update values inside accesskit_unix_adapter_update_if_active's factory, so
 * nothing is built while no assistive technology reads the tree.
 *
 * AccessKit's conventions followed here:
 *   - a Label's text is its value (AT-SPI names it from the value); other roles carry a label;
 *   - a text run's text is its value, with character lengths, positions, widths and word starts;
 *   - geometry: bounds in the node's space and a transform to its parent's (kurbo's affine has
 *     CSS matrix() order, as ours);
 *   - the first update of a tree carries its tree info (the root), later ones do not.
 * Under Wayland a client cannot know its window's position, so screen coordinates are the
 * window's own (decision 108 records it).
 */
#include <stdlib.h>
#include <string.h>

#include <accesskit.h>

#include <axion/ax_error.h>
#include <axion/base/ax_atomic.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/platform/ax_a11y_adapter.h>

#include "a11y_values.h"

struct ax_a11y_adapter {
    struct accesskit_unix_adapter *ak;
    ax_a11y_adapter_desc desc;
    char *root_name;
    /* The tree info went with an update since the last activation: written by AccessKit's
     * thread (activation) and by the updating thread inside update_if_active, which AccessKit
     * serializes with its own lock; atomic for the threads' sake. No lock of ours is held while
     * AccessKit runs: its threads call back into us. */
    ax_atomic_u32 sent_tree;
};

/* ax_role to AccessKit's roles. */
static accesskit_role role_of(uint32_t role)
{
    switch (role) {
    case PA_ROLE_WINDOW:
        return ACCESSKIT_ROLE_WINDOW;
    case PA_ROLE_GROUP:
        return ACCESSKIT_ROLE_GROUP;
    case PA_ROLE_LABEL:
        return ACCESSKIT_ROLE_LABEL;
    case PA_ROLE_HEADING:
        return ACCESSKIT_ROLE_HEADING;
    case PA_ROLE_PARAGRAPH:
        return ACCESSKIT_ROLE_PARAGRAPH;
    case PA_ROLE_BUTTON:
        return ACCESSKIT_ROLE_BUTTON;
    case PA_ROLE_LINK:
        return ACCESSKIT_ROLE_LINK;
    case PA_ROLE_IMAGE:
        return ACCESSKIT_ROLE_IMAGE;
    case PA_ROLE_CHECKBOX:
        return ACCESSKIT_ROLE_CHECK_BOX;
    case PA_ROLE_SWITCH:
        return ACCESSKIT_ROLE_SWITCH;
    case PA_ROLE_RADIO:
        return ACCESSKIT_ROLE_RADIO_BUTTON;
    case PA_ROLE_SLIDER:
        return ACCESSKIT_ROLE_SLIDER;
    case PA_ROLE_PROGRESS:
        return ACCESSKIT_ROLE_PROGRESS_INDICATOR;
    case PA_ROLE_TEXT_INPUT:
        return ACCESSKIT_ROLE_TEXT_INPUT;
    case PA_ROLE_LIST:
        return ACCESSKIT_ROLE_LIST;
    case PA_ROLE_LIST_ITEM:
        return ACCESSKIT_ROLE_LIST_ITEM;
    case PA_ROLE_ARTICLE:
        return ACCESSKIT_ROLE_ARTICLE;
    case PA_ROLE_DIALOG:
        return ACCESSKIT_ROLE_DIALOG;
    case PA_ROLE_TOOLBAR:
        return ACCESSKIT_ROLE_TOOLBAR;
    case PA_ROLE_TAB_LIST:
        return ACCESSKIT_ROLE_TAB_LIST;
    case PA_ROLE_TAB:
        return ACCESSKIT_ROLE_TAB;
    case PA_ROLE_TAB_PANEL:
        return ACCESSKIT_ROLE_TAB_PANEL;
    case PA_ROLE_MENU:
        return ACCESSKIT_ROLE_MENU;
    case PA_ROLE_MENU_ITEM:
        return ACCESSKIT_ROLE_MENU_ITEM;
    case PA_ROLE_SCROLL_VIEW:
        return ACCESSKIT_ROLE_SCROLL_VIEW;
    case PA_ROLE_SEPARATOR:
        return ACCESSKIT_ROLE_SPLITTER;
    case PA_ROLE_TEXT_RUN:
        return ACCESSKIT_ROLE_TEXT_RUN;
    default:
        return ACCESSKIT_ROLE_GENERIC_CONTAINER;
    }
}

static void set_actions(struct accesskit_node *n, uint32_t actions)
{
    if (actions & PA_ACTION_FOCUS) {
        accesskit_node_add_action(n, ACCESSKIT_ACTION_FOCUS);
    }
    if (actions & PA_ACTION_DEFAULT) {
        accesskit_node_add_action(n, ACCESSKIT_ACTION_CLICK);
    }
    if (actions & PA_ACTION_SET_VALUE) {
        accesskit_node_add_action(n, ACCESSKIT_ACTION_SET_VALUE);
    }
    if (actions & PA_ACTION_INCREMENT) {
        accesskit_node_add_action(n, ACCESSKIT_ACTION_INCREMENT);
    }
    if (actions & PA_ACTION_DECREMENT) {
        accesskit_node_add_action(n, ACCESSKIT_ACTION_DECREMENT);
    }
}

static struct accesskit_node *make_node(const ax_a11y_tree_node *s)
{
    struct accesskit_node *n = accesskit_node_new(role_of(s->role));
    struct accesskit_rect r;
    struct accesskit_affine m;
    uint32_t i;
    if (s->name.len > 0) {
        if (s->role == PA_ROLE_LABEL) {
            accesskit_node_set_value_with_length(n, s->name.ptr, s->name.len);
        } else {
            accesskit_node_set_label_with_length(n, s->name.ptr, s->name.len);
        }
    }
    if (s->description.len > 0) {
        accesskit_node_set_description_with_length(n, s->description.ptr, s->description.len);
    }
    if (s->value_text.len > 0 && s->role != PA_ROLE_LABEL) {
        accesskit_node_set_value_with_length(n, s->value_text.ptr, s->value_text.len);
    }
    if (s->flags & AX_A11Y_TREE_HAS_VALUE) {
        accesskit_node_set_numeric_value(n, (double)s->value);
    }
    if (s->flags & AX_A11Y_TREE_HAS_MIN) {
        accesskit_node_set_min_numeric_value(n, (double)s->value_min);
    }
    if (s->flags & AX_A11Y_TREE_HAS_MAX) {
        accesskit_node_set_max_numeric_value(n, (double)s->value_max);
    }
    if (s->flags & AX_A11Y_TREE_HAS_STEP) {
        accesskit_node_set_numeric_value_step(n, (double)s->value_step);
    }
    if (s->flags & AX_A11Y_TREE_DISABLED) {
        accesskit_node_set_disabled(n);
    }
    if (s->flags & AX_A11Y_TREE_SELECTED) {
        accesskit_node_set_selected(n, true);
    }
    if (s->checked != PA_TOGGLED_NONE) {
        accesskit_node_set_toggled(n, s->checked == PA_TOGGLED_TRUE    ? ACCESSKIT_TOGGLED_TRUE
                                      : s->checked == PA_TOGGLED_MIXED ? ACCESSKIT_TOGGLED_MIXED
                                                                       : ACCESSKIT_TOGGLED_FALSE);
    }
    if (s->expanded != PA_EXPANDED_NONE) {
        accesskit_node_set_expanded(n, s->expanded == PA_EXPANDED_EXPANDED);
    }
    if (s->level > 0) {
        accesskit_node_set_level(n, s->level);
    }
    set_actions(n, s->actions);
    r.x0 = (double)s->bounds[0];
    r.y0 = (double)s->bounds[1];
    r.x1 = (double)(s->bounds[0] + s->bounds[2]);
    r.y1 = (double)(s->bounds[1] + s->bounds[3]);
    accesskit_node_set_bounds(n, r);
    for (i = 0; i < 6; ++i) {
        m._0[i] = (double)s->transform[i];
    }
    if (!(m._0[0] == 1.0 && m._0[1] == 0.0 && m._0[2] == 0.0 && m._0[3] == 1.0 && m._0[4] == 0.0 &&
          m._0[5] == 0.0)) {
        accesskit_node_set_transform(n, m);
    }
    if (s->child_count > 0) {
        accesskit_node_set_children(n, s->child_count, (const accesskit_node_id *)s->children);
    }
    if (s->role == PA_ROLE_TEXT_RUN) {
        uint8_t starts[256];
        uint32_t w, at = 0, nstarts = 0;
        accesskit_node_set_value_with_length(n, s->text.ptr, s->text.len);
        if (s->char_count > 0) {
            accesskit_node_set_character_lengths(n, s->char_count, s->char_lengths);
            accesskit_node_set_character_positions(n, s->char_count, s->char_positions);
            accesskit_node_set_character_widths(n, s->char_count, s->char_widths);
        }
        /* Word starts as character indices (a byte each, as AccessKit stores them). */
        for (w = 0; w < s->word_count && at < 256u && nstarts < 256u; ++w) {
            starts[nstarts++] = (uint8_t)at;
            at += s->word_lengths[w];
        }
        if (nstarts > 0) {
            accesskit_node_set_word_starts(n, nstarts, starts);
        }
    }
    return n;
}

typedef struct build_ctx {
    ax_a11y_adapter *a;
    const ax_a11y_tree_update *u;
} build_ctx;

static struct accesskit_tree_update *build_update(void *user)
{
    build_ctx *c = (build_ctx *)user;
    const ax_a11y_tree_update *u = c->u;
    struct accesskit_tree_update *t =
        accesskit_tree_update_with_capacity_and_focus(u->count, u->focus);
    uint32_t i;
    if (u->full || ax_atomic_load_u32(&c->a->sent_tree, AX_MO_ACQUIRE) == 0) {
        struct accesskit_tree_info *info = accesskit_tree_info_new(u->root);
        accesskit_tree_info_set_toolkit_name(info, "AXION");
        accesskit_tree_update_set_tree_info(t, info);
        ax_atomic_store_u32(&c->a->sent_tree, 1u, AX_MO_RELEASE);
    }
    for (i = 0; i < u->count; ++i) {
        accesskit_tree_update_push_node(t, u->nodes[i].id, make_node(&u->nodes[i]));
    }
    return t;
}

/* AccessKit asks for a tree when an assistive technology appears: the root alone (the full tree
 * comes with the next frame, which the activation callback asks for). AccessKit's thread. */
static struct accesskit_tree_update *on_activation(void *user)
{
    ax_a11y_adapter *a = (ax_a11y_adapter *)user;
    struct accesskit_tree_update *t =
        accesskit_tree_update_with_capacity_and_focus(1, a->desc.root_id);
    struct accesskit_node *root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
    struct accesskit_tree_info *info = accesskit_tree_info_new(a->desc.root_id);
    if (a->root_name != NULL) {
        accesskit_node_set_label(root, a->root_name);
    }
    accesskit_tree_info_set_toolkit_name(info, "AXION");
    accesskit_tree_update_set_tree_info(t, info);
    accesskit_tree_update_push_node(t, a->desc.root_id, root);
    ax_atomic_store_u32(&a->sent_tree, 1u, AX_MO_RELEASE);
    if (a->desc.activated != NULL) {
        a->desc.activated(a->desc.user, true);
    }
    return t;
}

static void on_deactivation(void *user)
{
    ax_a11y_adapter *a = (ax_a11y_adapter *)user;
    if (a->desc.activated != NULL) {
        a->desc.activated(a->desc.user, false);
    }
}

static void on_action(struct accesskit_action_request *request, void *user)
{
    ax_a11y_adapter *a = (ax_a11y_adapter *)user;
    uint32_t action = 0;
    float value = 0.0f;
    switch (request->action) {
    case ACCESSKIT_ACTION_FOCUS:
        action = PA_ACTION_FOCUS;
        break;
    case ACCESSKIT_ACTION_CLICK:
        action = PA_ACTION_DEFAULT;
        break;
    case ACCESSKIT_ACTION_INCREMENT:
        action = PA_ACTION_INCREMENT;
        break;
    case ACCESSKIT_ACTION_DECREMENT:
        action = PA_ACTION_DECREMENT;
        break;
    case ACCESSKIT_ACTION_SET_VALUE:
        if (request->data.has_value &&
            request->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE) {
            action = PA_ACTION_SET_VALUE;
            value = (float)request->data.value.numeric_value;
        }
        break;
    default:
        break; /* scrolling, text selection, tooltips: with the components that have them */
    }
    if (action != 0 && a->desc.action != NULL) {
        a->desc.action(a->desc.user, request->target_node, action, value);
    }
    accesskit_action_request_free(request);
}

bool ax_a11y_adapter_available(void)
{
    return true;
}

ax_result ax_a11y_adapter_create(ax_window window, const ax_a11y_adapter_desc *desc,
                                 ax_a11y_adapter **out)
{
    ax_a11y_adapter *a;
    const ax_allocator *alloc = ax_default_allocator();
    (void)window; /* AT-SPI's tree is the app's; one adapter per window root */
    *out = NULL;
    if (desc == NULL || desc->struct_size < sizeof(ax_a11y_adapter_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_a11y_adapter_create: struct_size");
    }
    a = (ax_a11y_adapter *)ax_mem_alloc(alloc, sizeof *a, AX_ALIGNOF(ax_a11y_adapter));
    if (a == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_a11y_adapter_create: out of memory");
    }
    memset(a, 0, sizeof *a);
    a->desc = *desc;
    if (desc->root_name != NULL) {
        const size_t n = strlen(desc->root_name);
        a->root_name = (char *)ax_mem_alloc(alloc, n + 1u, 1);
        if (a->root_name != NULL) {
            memcpy(a->root_name, desc->root_name, n + 1u);
        }
    }
    ax_atomic_init_u32(&a->sent_tree, 0);
    a->ak = accesskit_unix_adapter_new(on_activation, a, on_action, a, on_deactivation, a);
    if (a->ak == NULL) {
        ax_a11y_adapter_destroy(a);
        return ax_set_error(AX_ERR_UNSUPPORTED, "ax_a11y_adapter_create: AccessKit failed");
    }
    *out = a;
    return AX_OK;
}

void ax_a11y_adapter_destroy(ax_a11y_adapter *a)
{
    const ax_allocator *alloc = ax_default_allocator();
    if (a == NULL) {
        return;
    }
    if (a->ak != NULL) {
        accesskit_unix_adapter_free(a->ak); /* its threads stop: no callback after this */
    }
    if (a->root_name != NULL) {
        ax_mem_free(alloc, a->root_name, strlen(a->root_name) + 1u);
    }
    ax_mem_free(alloc, a, sizeof *a);
}

void ax_a11y_adapter_update(ax_a11y_adapter *a, const ax_a11y_tree_update *u)
{
    build_ctx c;
    if (a == NULL || u == NULL) {
        return;
    }
    c.a = a;
    c.u = u;
    accesskit_unix_adapter_update_if_active(a->ak, build_update, &c);
}

void ax_a11y_adapter_set_window_focused(ax_a11y_adapter *a, bool focused)
{
    if (a != NULL) {
        accesskit_unix_adapter_update_window_focus_state(a->ak, focused);
    }
}
