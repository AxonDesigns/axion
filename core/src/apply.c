/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Applying transactions on the scene thread, each atomically.
 *
 * Commands are applied one by one against the live tree, each validated against the state the
 * previous ones left, and every change is recorded in an undo log first. If a command fails,
 * the log is replayed backwards and the transaction is rejected whole: the tree, the values and
 * the node count are exactly as before. Memory for the log is reserved before the first command,
 * so undoing never needs memory.
 *
 * Removal is deferred: `remove` detaches the node during the transaction and destroys it only
 * once every command has succeeded, so it can be undone. Later commands that name a node inside a
 * removed subtree fail as stale.
 *
 * What a rejection leaves behind (decision 68): values, structure and handles are restored; dirty
 * flags set while applying and undoing stay set, and the layout root list may hold candidates
 * that are no longer live. Both only cost the next frame some redundant work.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_text_node.h>

typedef enum undo_op {
    UNDO_CREATE = 1, /* destroy the created node */
    UNDO_SET = 2,    /* restore a value */
    UNDO_MOVE = 3    /* put a node back under its old parent, before its old next sibling */
} undo_op;

typedef struct undo_entry {
    uint32_t op;
    uint32_t type; /* UNDO_SET: ax_value_type */
    ax_prop prop;  /* UNDO_SET: a longhand */
    uint32_t reserved;
    ax_node node;
    ax_node parent; /* UNDO_MOVE: null when the node was a root */
    ax_node before; /* UNDO_MOVE: null when it was the last child */
    ax_value value; /* UNDO_SET */
} undo_entry;

/* undo_entry.reserved for UNDO_SET of a text property: the node had set it. */
#define UNDO_WAS_SET 1u

/* Per transaction, at most this many undo entries per command (a shorthand set). */
#define UNDO_PER_COMMAND AX_PROP_MAX_LONGHANDS

typedef struct apply_state {
    world_impl *w;
    ax_world handle;
    ax_scene *scene;
    undo_entry *undo;
    uint32_t undo_count;
    ax_node *removed;
    uint32_t removed_count;
    uint32_t command; /* index of the command being applied */
    ax_result error;
    char message[AX_TXN_MESSAGE_SIZE];
} apply_state;

static bool reserve(world_impl *w, void **array, uint32_t *capacity, uint32_t needed, size_t elem,
                    size_t align)
{
    uint32_t cap;
    void *fresh;
    if (needed <= *capacity) {
        return true;
    }
    cap = *capacity < 64u ? 64u : *capacity;
    while (cap < needed) {
        if (cap > 0x7FFFFFFFu) {
            return false;
        }
        cap *= 2u;
    }
    fresh = ax_mem_alloc(&w->allocator, elem * cap, align);
    if (fresh == NULL) {
        return false;
    }
    ax_mem_free(&w->allocator, *array, elem * *capacity); /* nothing to keep between txns */
    *array = fresh;
    *capacity = cap;
    return true;
}

static void failf(apply_state *st, ax_result code, const char *fmt, ...) AX_PRINTF_LIKE(3, 4);

static void failf(apply_state *st, ax_result code, const char *fmt, ...)
{
    va_list args;
    int n;
    st->error = code;
    n = snprintf(st->message, sizeof st->message, "command %u: ", (unsigned)st->command);
    if (n < 0 || (size_t)n >= sizeof st->message) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(st->message + n, sizeof st->message - (size_t)n, fmt, args);
    va_end(args);
}

/* Records a failure from a scene function: its ax_last_error message when it set one. */
static void fail_from(apply_state *st, ax_result code, const char *what)
{
    const ax_error *e = ax_last_error();
    if (e->code == code && e->message.len > 0) {
        failf(st, code, "%s: %s", what, e->message.ptr);
    } else {
        failf(st, code, "%s: %s", what, ax_result_name(code));
    }
}

/* True when node is inside a subtree removed earlier in this transaction. */
static bool is_removed(const apply_state *st, ax_node node)
{
    const ax_node_store *s = &st->scene->store;
    uint32_t row = ax_node_store_row(s, node);
    uint32_t slot;
    uint32_t i;
    if (st->removed_count == 0 || row == AX_NODE_NONE) {
        return false;
    }
    slot = ax_handle_slot(node.h);
    while (s->links[s->slots[slot].row].parent != AX_NODE_NONE) {
        slot = s->links[s->slots[slot].row].parent;
    }
    for (i = 0; i < st->removed_count; ++i) {
        if (ax_handle_slot(st->removed[i].h) == slot) {
            return true;
        }
    }
    return false;
}

/* A node a command may use: live and not removed in this transaction. */
static bool check_node(apply_state *st, ax_node node, const char *role)
{
    if (AX_HANDLE_IS_NULL(node) || ax_node_store_row(&st->scene->store, node) == AX_NODE_NONE) {
        failf(st, AX_ERR_INVALID_HANDLE, "%s is stale or unknown", role);
        return false;
    }
    if (is_removed(st, node)) {
        failf(st, AX_ERR_INVALID_HANDLE, "%s was removed earlier in this transaction", role);
        return false;
    }
    return true;
}

static undo_entry *push_undo(apply_state *st, undo_op op, ax_node node)
{
    undo_entry *u = &st->undo[st->undo_count++];
    memset(u, 0, sizeof *u);
    u->op = (uint32_t)op;
    u->node = node;
    return u;
}

/* Where the node is now, to put it back. */
static void push_undo_move(apply_state *st, ax_node node)
{
    const ax_node_store *s = &st->scene->store;
    const ax_node_links *l = &s->links[ax_node_store_row(s, node)];
    undo_entry *u = push_undo(st, UNDO_MOVE, node);
    u->parent = ax_node_store_handle(s, l->parent);
    u->before = ax_node_store_handle(s, l->next_sibling);
}

/* ---- Commands -------------------------------------------------------------------------------- */

static bool apply_create(apply_state *st, const ax_txn_cmd_create *c)
{
    ax_result r;
    if (c->kind != AX_NODE_VIEW) {
        failf(st, AX_ERR_INVALID_ARGUMENT, "create: unknown node kind %u", (unsigned)c->kind);
        return false;
    }
    r = ax_scene_create_at(st->scene, c->node, (ax_node_kind)c->kind);
    if (r != AX_OK) {
        failf(st, r, "create: %s",
              r == AX_ERR_INVALID_HANDLE ? "the handle was not reserved for a new node"
                                         : ax_result_name(r));
        return false;
    }
    push_undo(st, UNDO_CREATE, c->node);
    ax__mirror_touch_slot(st->w, ax_handle_slot(c->node.h));
    ax__a11y_touch(st->w, ax_handle_slot(c->node.h), AX__A11Y_STRUCT);
    return true;
}

/* The old value of a longhand, so the change can be undone. A text property also keeps whether
 * the node had set it (unset again means inheriting again), and a string keeps a reference,
 * dropped once the transaction is done (release_undo). */
static void push_undo_set(apply_state *st, ax_node node, const ax_prop_desc *d)
{
    undo_entry *u = push_undo(st, UNDO_SET, node);
    u->prop = d->id;
    u->type = (uint32_t)d->type;
    if (d->storage == AX_PROP_IN_TEXT || d->storage == AX_PROP_IN_SEMANTICS) {
        bool was_set = false;
        (void)ax_scene_get_own(st->scene, node, d->id, &u->value, &was_set);
        u->reserved = was_set ? UNDO_WAS_SET : 0u;
    } else {
        (void)ax_scene_get(st->scene, node, d->id, &u->value);
    }
    if (d->type == AX_VALUE_STRING) {
        ax_shared_str_retain(u->value.str);
    }
}

static void release_undo(apply_state *st)
{
    uint32_t i;
    for (i = 0; i < st->undo_count; ++i) {
        if (st->undo[i].op == UNDO_SET && st->undo[i].type == AX_VALUE_STRING) {
            ax_shared_str_release(st->undo[i].value.str);
            st->undo[i].value.str = NULL;
        }
    }
}

static bool apply_set_str(apply_state *st, const ax_txn_cmd_set_str *c)
{
    const ax_prop_desc *d = ax_prop_registry_find(&st->scene->props, c->prop);
    ax_value v;
    ax_result r;
    if (!check_node(st, c->node, "the node")) {
        return false;
    }
    if (d == NULL || d->type != AX_VALUE_STRING) {
        failf(st, AX_ERR_INVALID_ARGUMENT, "set: property 0x%x %s", (unsigned)c->prop,
              d == NULL ? "is unknown" : "does not take a string");
        return false;
    }
    memset(&v, 0, sizeof v);
    v.str = ax_shared_str_create(&st->w->allocator, ax_txn_cmd_str_bytes(c), c->length);
    if (v.str == NULL) {
        failf(st, AX_ERR_OUT_OF_MEMORY, "set: out of memory for a string of %u bytes",
              (unsigned)c->length);
        return false;
    }
    push_undo_set(st, c->node, d);
    ax_clear_error();
    r = ax_scene_set(st->scene, c->node, c->prop, AX_VALUE_STRING, &v);
    ax_shared_str_release(v.str);
    if (r != AX_OK) {
        fail_from(st, r, "set");
        return false;
    }
    ax__mirror_touch_slot(st->w, ax_handle_slot(c->node.h));
    ax__a11y_touch_prop(st->w, ax_handle_slot(c->node.h), c->prop);
    return true;
}

static bool apply_set(apply_state *st, const ax_txn_cmd_set *c)
{
    const ax_prop_desc *d = ax_prop_registry_find(&st->scene->props, c->prop);
    ax_result r;
    uint32_t i;
    if (!check_node(st, c->node, "the node")) {
        return false;
    }
    if (d == NULL) {
        failf(st, AX_ERR_INVALID_ARGUMENT, "set: unknown property 0x%x", (unsigned)c->prop);
        return false;
    }
    /* Old values first (each longhand of a shorthand), so the change can be undone. */
    if (d->storage == AX_PROP_EXPANDS) {
        for (i = 0; i < d->longhand_count; ++i) {
            push_undo_set(st, c->node, ax_prop_registry_find(&st->scene->props, d->longhands[i]));
        }
    } else {
        push_undo_set(st, c->node, d);
    }
    ax_clear_error();
    r = ax_scene_set(st->scene, c->node, c->prop, (ax_value_type)c->type, &c->value);
    if (r != AX_OK) {
        fail_from(st, r, "set");
        return false;
    }
    ax__mirror_touch_slot(st->w, ax_handle_slot(c->node.h));
    ax__a11y_touch_prop(st->w, ax_handle_slot(c->node.h), c->prop);
    return true;
}

static bool apply_insert(apply_state *st, const ax_txn_cmd_insert *c)
{
    ax_result r;
    if (!check_node(st, c->parent, "the parent") || !check_node(st, c->child, "the child") ||
        (!AX_HANDLE_IS_NULL(c->before) && !check_node(st, c->before, "the anchor"))) {
        return false;
    }
    push_undo_move(st, c->child);
    r = ax_scene_insert_before(st->scene, c->parent, c->child, c->before);
    if (r != AX_OK) {
        st->undo_count--; /* nothing moved */
        failf(st, r, "insert: %s",
              r == AX_ERR_INVALID_ARGUMENT
                  ? "the parent is the child or inside it, or the anchor is not the parent's child"
                  : ax_result_name(r));
        return false;
    }
    ax__mirror_touch_slot(st->w, ax_handle_slot(c->child.h)); /* its parent changed */
    ax__a11y_touch(st->w, ax_handle_slot(c->child.h), AX__A11Y_STRUCT);
    return true;
}

static bool apply_remove(apply_state *st, const ax_txn_cmd_remove *c)
{
    if (!check_node(st, c->node, "the node")) {
        return false;
    }
    push_undo_move(st, c->node);
    (void)ax_scene_detach(st->scene, c->node);
    ax__mirror_touch_slot(st->w, ax_handle_slot(c->node.h));
    ax__a11y_touch(st->w, ax_handle_slot(c->node.h), AX__A11Y_STRUCT);
    st->removed[st->removed_count++] = c->node;
    return true;
}

static bool apply_transition(apply_state *st, const ax_txn_cmd_transition *c)
{
    const ax_prop_desc *d = ax_prop_registry_find(&st->scene->props, c->prop);
    if (!AX_HANDLE_IS_NULL(c->node) && !check_node(st, c->node, "the node")) {
        return false;
    }
    if (c->prop != AX_PROP_LAYOUT && (d == NULL || (d->flags & AX_PROP_ANIMATABLE) == 0)) {
        failf(st, AX_ERR_INVALID_ARGUMENT, "transition: property 0x%x is %s", (unsigned)c->prop,
              d == NULL ? "unknown" : "not animatable");
        return false;
    }
    /* Recorded in the transaction; no effect until animations exist. */
    return true;
}

static void undo_all(apply_state *st)
{
    ax_scene *scene = st->scene;
    st->w->suppress_recycle = true; /* the app thread recycles a rejected txn's creates */
    while (st->undo_count > 0) {
        const undo_entry *u = &st->undo[--st->undo_count];
        ax_result r = AX_OK;
        switch ((undo_op)u->op) {
        case UNDO_CREATE:
            r = ax_node_store_destroy(&scene->store, u->node);
            break;
        case UNDO_SET:
            if (ax_prop_builtin(u->prop) != NULL &&
                (ax_prop_builtin(u->prop)->storage == AX_PROP_IN_TEXT ||
                 ax_prop_builtin(u->prop)->storage == AX_PROP_IN_SEMANTICS) &&
                (u->reserved & UNDO_WAS_SET) == 0) {
                r = ax_scene_unset(scene, u->node, u->prop);
            } else {
                r = ax_scene_set(scene, u->node, u->prop, (ax_value_type)u->type, &u->value);
            }
            if (u->type == AX_VALUE_STRING) {
                ax_shared_str_release(u->value.str); /* the undo log's reference */
            }
            break;
        case UNDO_MOVE:
            if (AX_HANDLE_IS_NULL(u->parent)) {
                r = ax_scene_detach(scene, u->node);
            } else {
                r = ax_scene_insert_before(scene, u->parent, u->node, u->before);
            }
            break;
        }
        AX_ASSERT_MSG(r == AX_OK, "undoing a rejected transaction failed");
        (void)r;
    }
    st->w->suppress_recycle = false;
}

static void apply_one(apply_state *st, ax_txn *t)
{
    world_impl *w = st->w;
    ax_txn_info info;
    const ax_txn_cmd *cmd = NULL;
    bool ok = true;
    uint32_t i;

    ax_txn_get_info(t, &info);
    st->undo_count = 0;
    st->removed_count = 0;
    st->command = 0;
    st->error = AX_OK;
    st->message[0] = '\0';
    if (!reserve(w, (void **)&w->undo, &w->undo_capacity, info.command_count * UNDO_PER_COMMAND,
                 sizeof(undo_entry), AX_ALIGNOF(undo_entry)) ||
        !reserve(w, (void **)&w->removed, &w->removed_capacity, info.command_count, sizeof(ax_node),
                 AX_ALIGNOF(ax_node))) {
        ax_world_finish_txn(st->handle, t, AX_ERR_OUT_OF_MEMORY, 0,
                            "out of memory before applying the transaction");
        return;
    }
    st->undo = (undo_entry *)w->undo;
    st->removed = w->removed;

    for (i = 0; ok && i < info.command_count; ++i) {
        cmd = ax_txn_next_cmd(&info, cmd);
        st->command = i;
        if (cmd == NULL || !ax_txn_cmd_valid(&info, cmd)) {
            failf(st, AX_ERR_INVALID_ARGUMENT, "malformed command");
            ok = false;
            break;
        }
        switch ((ax_txn_op)cmd->op) {
        case AX_TXN_OP_CREATE:
            ok = apply_create(st, (const ax_txn_cmd_create *)(const void *)cmd);
            break;
        case AX_TXN_OP_SET:
            ok = apply_set(st, (const ax_txn_cmd_set *)(const void *)cmd);
            break;
        case AX_TXN_OP_INSERT:
            ok = apply_insert(st, (const ax_txn_cmd_insert *)(const void *)cmd);
            break;
        case AX_TXN_OP_REMOVE:
            ok = apply_remove(st, (const ax_txn_cmd_remove *)(const void *)cmd);
            break;
        case AX_TXN_OP_TRANSITION:
            ok = apply_transition(st, (const ax_txn_cmd_transition *)(const void *)cmd);
            break;
        case AX_TXN_OP_SET_STR:
            ok = apply_set_str(st, (const ax_txn_cmd_set_str *)(const void *)cmd);
            break;
        }
    }
    if (ok && ax_txn_next_cmd(&info, cmd) != NULL) {
        st->command = info.command_count;
        failf(st, AX_ERR_INVALID_ARGUMENT, "malformed transaction: bytes after the last command");
        ok = false;
    }

    if (!ok) {
        undo_all(st); /* drops the undo log's string references as it goes */
        ax_world_finish_txn(st->handle, t, st->error, st->command, st->message);
        return;
    }
    /* Animations (decision 94): this transaction's transitions first, so its own changes use
     * them, then its changes, from the values the undo log kept. */
    {
        const ax_txn_cmd *c2 = NULL;
        for (i = 0; i < info.command_count; ++i) {
            c2 = ax_txn_next_cmd(&info, c2);
            if (c2 != NULL && c2->op == AX_TXN_OP_TRANSITION) {
                const ax_txn_cmd_transition *tc = (const ax_txn_cmd_transition *)(const void *)c2;
                ax__anim_set_transition(w, tc->node, tc->prop, &tc->curve);
            }
        }
        for (i = 0; i < st->undo_count; ++i) {
            const undo_entry *u = &st->undo[i];
            if (u->op == UNDO_SET) {
                ax__anim_on_set(w, info.seq, u->node, u->prop, &u->value, info.animated);
            }
        }
        ax__anim_txn_applied(w, info.animated);
        release_undo(st);
    }
    /* Every command succeeded: now the removed subtrees go (their slots are recycled). */
    for (i = 0; i < st->removed_count; ++i) {
        ax_result r = ax_scene_destroy(st->scene, st->removed[i]);
        AX_ASSERT(r == AX_OK);
        (void)r;
    }
    ax_world_finish_txn(st->handle, t, AX_OK, 0, NULL);
}

uint32_t ax_world_apply_pending(ax_world world, uint64_t deadline_ns)
{
    world_impl *w = ax__world_from_handle(world);
    apply_state st;
    uint32_t applied = 0;
    if (w == NULL) {
        (void)ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_apply_pending: invalid world");
        return 0;
    }
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_apply_pending: not the scene thread");
    memset(&st, 0, sizeof st);
    st.w = w;
    st.handle = world;
    st.scene = &w->scene;
    ax__anim_txn_begin(w, deadline_ns); /* animations start at the frame's latch point */
    for (;;) {
        const ax_txn *next = ax_world_peek_txn(world);
        ax_txn *t;
        if (next == NULL || next->commit_time > deadline_ns) {
            break; /* later commits wait for the next frame */
        }
        t = ax_world_take_txn(world);
        apply_one(&st, t);
        applied++;
    }
    return applied;
}

void ax__apply_free(world_impl *w)
{
    ax_mem_free(&w->allocator, w->undo, sizeof(undo_entry) * w->undo_capacity);
    ax_mem_free(&w->allocator, w->removed, sizeof(ax_node) * w->removed_capacity);
    w->undo = NULL;
    w->removed = NULL;
    w->undo_capacity = 0;
    w->removed_capacity = 0;
}
