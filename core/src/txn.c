/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transactions on the app thread: recording commands into a reusable buffer, sticky errors,
 * handles minted without waiting for the scene, commit and abort.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_math.h>
#include <axion/base/ax_memory.h>

#define MIN_BUFFER 512u

AX_STATIC_ASSERT(sizeof(ax_txn_cmd) == 8, "command header is 8 bytes");
AX_STATIC_ASSERT(sizeof(ax_txn_cmd_create) % 8 == 0, "commands are 8-byte multiples");
AX_STATIC_ASSERT(sizeof(ax_txn_cmd_set) % 8 == 0, "commands are 8-byte multiples");
AX_STATIC_ASSERT(sizeof(ax_txn_cmd_insert) % 8 == 0, "commands are 8-byte multiples");
AX_STATIC_ASSERT(sizeof(ax_txn_cmd_remove) % 8 == 0, "commands are 8-byte multiples");
AX_STATIC_ASSERT(sizeof(ax_txn_cmd_transition) % 8 == 0, "commands are 8-byte multiples");

/* ---- Colors ---------------------------------------------------------------------------------- */

ax_rgba ax_rgba_from_srgb(float r, float g, float b, float a)
{
    ax_color c = ax_color_from_srgb(r, g, b, a);
    ax_rgba out;
    out.r = c.r;
    out.g = c.g;
    out.b = c.b;
    out.a = c.a;
    return out;
}

ax_rgba ax_rgba_from_srgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ax_rgba_from_srgb((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f,
                             (float)a / 255.0f);
}

/* ---- Recording ------------------------------------------------------------------------------- */

static void fail(ax_txn *t, ax_result code, const char *fmt, ...) AX_PRINTF_LIKE(3, 4);

static void fail(ax_txn *t, ax_result code, const char *fmt, ...)
{
    va_list args;
    if (t->error != AX_OK) {
        return; /* the first error wins */
    }
    t->error = code;
    t->error_command = t->command_count;
    va_start(args, fmt);
    vsnprintf(t->error_message, sizeof t->error_message, fmt, args);
    va_end(args);
}

/* Records ax_last_error's message as the transaction's error. */
static void fail_with_last_error(ax_txn *t, ax_result code)
{
    fail(t, code, "%s", ax_last_error()->message.ptr != NULL ? ax_last_error()->message.ptr : "");
}

/* A transaction that is open, on its world's app thread, without a recorded error. */
static bool can_record(ax_txn *t)
{
    AX_ASSERT_MSG(t != NULL && t->open, "ax_txn used after commit or abort");
    if (t == NULL || !t->open) {
        return false;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(t->world), "ax_txn used off the world's app thread");
    return t->error == AX_OK;
}

/* Reserves a command of `size` bytes and fills its header. NULL (with a sticky error) when the
 * buffer cannot grow. */
static void *emit(ax_txn *t, ax_txn_op op, uint32_t size)
{
    ax_txn_cmd *cmd;
    if (t->capacity - t->size < size) {
        uint32_t cap = t->capacity < MIN_BUFFER ? MIN_BUFFER : t->capacity;
        unsigned char *fresh;
        while (cap - t->size < size) {
            if (cap > 0x7FFFFFFFu) {
                fail(t, AX_ERR_OUT_OF_MEMORY, "transaction larger than 2 GiB");
                return NULL;
            }
            cap *= 2u;
        }
        fresh = t->buf == NULL ? (unsigned char *)ax_mem_alloc(&t->world->allocator, cap, 8)
                               : (unsigned char *)ax_mem_resize(&t->world->allocator, t->buf,
                                                                t->capacity, cap, 8);
        if (fresh == NULL) {
            fail(t, AX_ERR_OUT_OF_MEMORY, "out of memory recording the transaction");
            return NULL;
        }
        t->buf = fresh;
        t->capacity = cap;
    }
    cmd = (ax_txn_cmd *)(void *)(t->buf + t->size);
    memset(cmd, 0, size);
    cmd->op = (uint16_t)op;
    cmd->size = size;
    t->size += size;
    t->command_count++;
    return cmd;
}

ax_txn *ax_txn_begin(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    ax_txn *t;
    if (w == NULL) {
        (void)ax_set_error(AX_ERR_INVALID_HANDLE, "ax_txn_begin: invalid or destroyed world");
        return NULL;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_txn_begin: not the world's app thread");
    if (!ax__world_on_app_thread(w)) {
        (void)ax_set_error(AX_ERR_WRONG_THREAD, "ax_txn_begin: not the world's app thread");
        return NULL;
    }
    ax__world_reclaim(w);
    t = w->free_txns;
    if (t != NULL) {
        w->free_txns = t->next_free;
    } else {
        t = (ax_txn *)ax_mem_alloc(&w->allocator, sizeof *t, AX_ALIGNOF(ax_txn));
        if (t == NULL) {
            (void)ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_txn_begin: out of memory");
            return NULL;
        }
        memset(t, 0, sizeof *t);
        t->world = w;
        ax_atomic_init_ptr(&t->link.next, NULL);
        t->next_all = w->all_txns;
        w->all_txns = t;
    }
    t->next_free = NULL;
    t->size = 0;
    t->command_count = 0;
    t->create_count = 0;
    t->animated = true;
    t->open = true;
    t->error = AX_OK;
    t->error_command = 0;
    t->error_message[0] = '\0';
    t->seq = 0;
    t->commit_time = 0;
    t->result = AX_OK;
    return t;
}

ax_node ax_txn_create(ax_txn *t, ax_node_kind kind)
{
    ax_node none = AX_HANDLE_NULL_INIT;
    ax_txn_cmd_create *cmd;
    ax_node n;
    if (!can_record(t)) {
        return none;
    }
    if (kind != AX_NODE_VIEW) {
        fail(t, AX_ERR_INVALID_ARGUMENT, "ax_txn_create: unknown node kind %d", (int)kind);
        return none;
    }
    cmd = (ax_txn_cmd_create *)emit(t, AX_TXN_OP_CREATE, sizeof *cmd);
    if (cmd == NULL) {
        return none;
    }
    n = ax__world_mint_handle(t->world);
    if (AX_HANDLE_IS_NULL(n)) {
        /* Every slot handed out: drop the command just reserved. */
        t->size -= (uint32_t)sizeof *cmd;
        t->command_count--;
        fail(t, AX_ERR_OUT_OF_MEMORY, "ax_txn_create: every node slot is in use");
        return none;
    }
    cmd->kind = (uint32_t)kind;
    cmd->node = n;
    t->create_count++;
    return n;
}

/* Records a SET after the checks that need no scene: a node, and for core properties the type
 * and the value. Run-time properties are checked when the scene applies the transaction. */
static void record_set(ax_txn *t, ax_node node, ax_prop prop, ax_value_type type,
                       const ax_value *value)
{
    ax_txn_cmd_set *cmd;
    if (!can_record(t)) {
        return;
    }
    if (AX_HANDLE_IS_NULL(node)) {
        fail(t, AX_ERR_INVALID_HANDLE, "set of property 0x%x on a null node", (unsigned)prop);
        return;
    }
    if (prop < AX_PROP_FIRST_IDL) {
        const ax_prop_desc *d = ax_prop_builtin(prop);
        ax_result r;
        if (d == NULL) {
            fail(t, AX_ERR_INVALID_ARGUMENT, "unknown property 0x%x", (unsigned)prop);
            return;
        }
        r = ax_prop_check_value(d, type, value);
        if (r != AX_OK) {
            fail_with_last_error(t, r);
            return;
        }
    }
    cmd = (ax_txn_cmd_set *)emit(t, AX_TXN_OP_SET, sizeof *cmd);
    if (cmd != NULL) {
        cmd->prop = prop;
        cmd->type = (uint32_t)type;
        cmd->node = node;
        cmd->value = *value;
    }
}

void ax_txn_set_f32(ax_txn *t, ax_node node, ax_prop prop, float value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.f32 = value;
    record_set(t, node, prop, AX_VALUE_F32, &v);
}

void ax_txn_set_dimension(ax_txn *t, ax_node node, ax_prop prop, ax_dimension value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.dimension = value;
    record_set(t, node, prop, AX_VALUE_DIMENSION, &v);
}

void ax_txn_set_vec2(ax_txn *t, ax_node node, ax_prop prop, ax_vec2f value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.vec2.x = value.x;
    v.vec2.y = value.y;
    record_set(t, node, prop, AX_VALUE_VEC2, &v);
}

void ax_txn_set_color(ax_txn *t, ax_node node, ax_prop prop, ax_rgba value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.color = ax_color_make(value.r, value.g, value.b, value.a);
    record_set(t, node, prop, AX_VALUE_COLOR, &v);
}

void ax_txn_set_transform(ax_txn *t, ax_node node, ax_prop prop, ax_transform2d value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.transform2d = ax_affine2_make(value.a, value.b, value.c, value.d, value.tx, value.ty);
    record_set(t, node, prop, AX_VALUE_TRANSFORM2D, &v);
}

void ax_txn_set_u32(ax_txn *t, ax_node node, ax_prop prop, uint32_t value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.u32 = value;
    record_set(t, node, prop, AX_VALUE_U32, &v);
}

void ax_txn_set_bool(ax_txn *t, ax_node node, ax_prop prop, bool value)
{
    ax_value v;
    memset(&v, 0, sizeof v);
    v.boolean = value;
    record_set(t, node, prop, AX_VALUE_BOOL, &v);
}

void ax_txn_insert_before(ax_txn *t, ax_node parent, ax_node child, ax_node before)
{
    ax_txn_cmd_insert *cmd;
    if (!can_record(t)) {
        return;
    }
    if (AX_HANDLE_IS_NULL(parent) || AX_HANDLE_IS_NULL(child)) {
        fail(t, AX_ERR_INVALID_HANDLE, "insert with a null parent or child");
        return;
    }
    if (AX_HANDLE_EQ(parent, child) || AX_HANDLE_EQ(before, child)) {
        fail(t, AX_ERR_INVALID_ARGUMENT, "insert of a node into itself or before itself");
        return;
    }
    cmd = (ax_txn_cmd_insert *)emit(t, AX_TXN_OP_INSERT, sizeof *cmd);
    if (cmd != NULL) {
        cmd->parent = parent;
        cmd->child = child;
        cmd->before = before;
    }
}

void ax_txn_append_child(ax_txn *t, ax_node parent, ax_node child)
{
    ax_node none = AX_HANDLE_NULL_INIT;
    ax_txn_insert_before(t, parent, child, none);
}

void ax_txn_remove(ax_txn *t, ax_node node)
{
    ax_txn_cmd_remove *cmd;
    if (!can_record(t)) {
        return;
    }
    if (AX_HANDLE_IS_NULL(node)) {
        fail(t, AX_ERR_INVALID_HANDLE, "remove of a null node");
        return;
    }
    cmd = (ax_txn_cmd_remove *)emit(t, AX_TXN_OP_REMOVE, sizeof *cmd);
    if (cmd != NULL) {
        cmd->node = node;
    }
}

void ax_txn_set_animated(ax_txn *t, bool animated)
{
    if (can_record(t)) {
        t->animated = animated;
    }
}

static void record_transition(ax_txn *t, ax_node node, ax_prop prop, const ax_curve *c)
{
    ax_txn_cmd_transition *cmd =
        (ax_txn_cmd_transition *)emit(t, AX_TXN_OP_TRANSITION, sizeof *cmd);
    if (cmd != NULL) {
        cmd->prop = prop;
        cmd->node = node;
        cmd->curve = *c;
    }
}

/* Records a transition for a node (or the world's default, with a null node). */
static void transition(ax_txn *t, ax_node node, ax_prop prop, const ax_transition *tr)
{
    ax_curve c;
    ax_result r;
    uint32_t i;
    r = ax_curve_from_transition(tr, &c);
    if (r != AX_OK) {
        fail(t, r, "%s", ax_last_error()->message.ptr);
        return;
    }
    if (prop == AX_PROP_LAYOUT) {
        record_transition(t, node, prop, &c);
        return;
    }
    if (prop < AX_PROP_FIRST_IDL) {
        const ax_prop_desc *d = ax_prop_builtin(prop);
        if (d == NULL || (d->flags & AX_PROP_ANIMATABLE) == 0) {
            fail(t, AX_ERR_INVALID_ARGUMENT, "transition on property 0x%x, which is %s",
                 (unsigned)prop, d == NULL ? "unknown" : "not animatable");
            return;
        }
        if (d->effect == AX_EFFECT_LAYOUT) {
            /* Layout properties animate through the frame (decision 94). */
            record_transition(t, node, AX_PROP_LAYOUT, &c);
            return;
        }
        if (d->storage == AX_PROP_EXPANDS) {
            /* A shorthand's transition is each longhand's. */
            for (i = 0; i < d->longhand_count; ++i) {
                record_transition(t, node, d->longhands[i], &c);
            }
            return;
        }
    }
    record_transition(t, node, prop, &c);
}

void ax_txn_set_transition(ax_txn *t, ax_node node, ax_prop prop, const ax_transition *tr)
{
    if (!can_record(t)) {
        return;
    }
    if (AX_HANDLE_IS_NULL(node)) {
        fail(t, AX_ERR_INVALID_HANDLE, "transition on a null node");
        return;
    }
    transition(t, node, prop, tr);
}

void ax_txn_set_default_transition(ax_txn *t, ax_prop prop, const ax_transition *tr)
{
    ax_node none = AX_HANDLE_NULL_INIT;
    if (can_record(t)) {
        transition(t, none, prop, tr);
    }
}

/* ---- Ending ---------------------------------------------------------------------------------- */

void ax__txn_release(world_impl *w, ax_txn *t, bool created_nodes_exist)
{
    if (!created_nodes_exist && t->create_count > 0) {
        /* The nodes were never created (aborted, failed to record, rejected and rolled back):
         * their handles stay stale, and the slots go back at the next generation. */
        ax_txn_info info;
        const ax_txn_cmd *cmd = NULL;
        ax_txn_get_info(t, &info);
        while ((cmd = ax_txn_next_cmd(&info, cmd)) != NULL) {
            if (cmd->op == AX_TXN_OP_CREATE) {
                ax__world_unmint_handle(w, ((const ax_txn_cmd_create *)(const void *)cmd)->node);
            }
        }
    }
    t->open = false;
    t->next_free = w->free_txns;
    w->free_txns = t;
}

void ax__txn_free(world_impl *w, ax_txn *t)
{
    ax_mem_free(&w->allocator, t->buf, t->capacity);
    ax_mem_free(&w->allocator, t, sizeof *t);
}

ax_result ax_txn_commit(ax_txn *t, uint64_t *out_seq)
{
    world_impl *w;
    if (out_seq != NULL) {
        *out_seq = 0;
    }
    AX_ASSERT_MSG(t != NULL && t->open, "ax_txn_commit on a transaction that is not open");
    if (t == NULL || !t->open) {
        return ax_set_error(AX_ERR_INVALID_ARGUMENT, "ax_txn_commit: not an open transaction");
    }
    w = t->world;
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_txn_commit: not the world's app thread");
    if (!ax__world_on_app_thread(w)) {
        return ax_set_error(AX_ERR_WRONG_THREAD, "ax_txn_commit: not the world's app thread");
    }
    if (t->error != AX_OK) {
        ax_result r = t->error;
        (void)ax_set_error(r, "transaction command %u: %s", (unsigned)t->error_command,
                           t->error_message);
        ax__txn_release(w, t, false);
        return r;
    }
    t->open = false;
    t->seq = ++w->next_seq;
    t->commit_time = ax__world_now(w);
    ax__world_push_txn(w, t);
    ax__world_reclaim(w);
    if (out_seq != NULL) {
        *out_seq = t->seq;
    }
    return AX_OK;
}

void ax_txn_abort(ax_txn *t)
{
    AX_ASSERT_MSG(t != NULL && t->open, "ax_txn_abort on a transaction that is not open");
    if (t == NULL || !t->open) {
        return;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(t->world), "ax_txn_abort: not the world's app thread");
    ax__txn_release(t->world, t, false);
}

/* ---- Reading (scene thread) ------------------------------------------------------------------ */

void ax_txn_get_info(const ax_txn *t, ax_txn_info *out)
{
    out->seq = t->seq;
    out->commit_time = t->commit_time;
    out->command_count = t->command_count;
    out->create_count = t->create_count;
    out->animated = t->animated;
    out->commands = t->buf;
    out->size = t->size;
}

const ax_txn_cmd *ax_txn_next_cmd(const ax_txn_info *info, const ax_txn_cmd *prev)
{
    const unsigned char *p;
    if (prev == NULL) {
        p = info->commands;
    } else {
        p = (const unsigned char *)prev + prev->size;
    }
    if (info->commands == NULL || p >= info->commands + info->size) {
        return NULL;
    }
    return (const ax_txn_cmd *)(const void *)p;
}

static uint32_t op_size(uint16_t op)
{
    switch (op) {
    case AX_TXN_OP_CREATE:
        return sizeof(ax_txn_cmd_create);
    case AX_TXN_OP_SET:
        return sizeof(ax_txn_cmd_set);
    case AX_TXN_OP_INSERT:
        return sizeof(ax_txn_cmd_insert);
    case AX_TXN_OP_REMOVE:
        return sizeof(ax_txn_cmd_remove);
    case AX_TXN_OP_TRANSITION:
        return sizeof(ax_txn_cmd_transition);
    }
    return 0;
}

bool ax_txn_cmd_valid(const ax_txn_info *info, const ax_txn_cmd *cmd)
{
    size_t offset = (size_t)((const unsigned char *)cmd - info->commands);
    uint32_t expected;
    if (offset + sizeof(ax_txn_cmd) > info->size) {
        return false;
    }
    expected = op_size(cmd->op);
    return expected != 0 && cmd->size == expected && offset + cmd->size <= info->size;
}
