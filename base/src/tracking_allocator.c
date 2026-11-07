/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Each block is laid out as
 *
 *     base (from parent)                      user (returned)
 *     |<-- padding -->|<-- ax_tracking_block -->|<-- size bytes -->|
 *     |<------------- prefix bytes ------------>|
 *
 * where prefix is sizeof(ax_tracking_block) rounded up to the block's alignment, so user keeps
 * the requested alignment and the record sits right below it.
 */
#include <stdio.h>
#include <stdlib.h>

#include "axion/base/ax_compiler.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_tracking_allocator.h"

struct ax_tracking_block {
    ax_tracking_block *prev;
    ax_tracking_block *next;
    size_t size;
    size_t align; /* effective alignment of the parent allocation */
};

#define AX_TRACKING_BLOCK_ALIGN AX_ALIGNOF(ax_tracking_block)

AX_STATIC_ASSERT(sizeof(ax_tracking_block) % AX_ALIGNOF(ax_tracking_block) == 0,
                 "record size keeps it aligned right below the user pointer");

static size_t effective_align(size_t align)
{
    return align > AX_TRACKING_BLOCK_ALIGN ? align : AX_TRACKING_BLOCK_ALIGN;
}

static size_t prefix_size(size_t ea)
{
    return ax_align_up(sizeof(ax_tracking_block), ea);
}

static ax_tracking_block *block_of(void *user)
{
    return (ax_tracking_block *)((unsigned char *)user - sizeof(ax_tracking_block));
}

static void *user_of(ax_tracking_block *b)
{
    return (unsigned char *)b + sizeof(ax_tracking_block);
}

static void *base_of(ax_tracking_block *b)
{
    return (unsigned char *)user_of(b) - prefix_size(b->align);
}

/* Reports a contract violation. Returns only when abort_on_misuse is false. */
static void misuse(ax_tracking_allocator *t, const char *what, const void *ptr)
{
    fprintf(stderr, "ax_tracking_allocator: %s (ptr %p)\n", what, ptr);
    fflush(stderr);
    if (t->abort_on_misuse) {
        abort();
    }
    t->misuse_count++;
}

static bool is_live(const ax_tracking_allocator *t, const ax_tracking_block *b)
{
    const ax_tracking_block *it;
    for (it = t->live; it != NULL; it = it->next) {
        if (it == b) {
            return true;
        }
    }
    return false;
}

static void link_front(ax_tracking_allocator *t, ax_tracking_block *b)
{
    b->prev = NULL;
    b->next = t->live;
    if (t->live != NULL) {
        t->live->prev = b;
    }
    t->live = b;
}

static void unlink_block(ax_tracking_allocator *t, ax_tracking_block *b)
{
    if (b->prev != NULL) {
        b->prev->next = b->next;
    } else {
        t->live = b->next;
    }
    if (b->next != NULL) {
        b->next->prev = b->prev;
    }
}

static void note_live_bytes(ax_tracking_allocator *t)
{
    if (t->live_bytes > t->peak_bytes) {
        t->peak_bytes = t->live_bytes;
    }
}

static void *tracking_alloc(void *ctx, size_t size, size_t align)
{
    ax_tracking_allocator *t = (ax_tracking_allocator *)ctx;
    size_t ea;
    size_t total;
    unsigned char *base;
    ax_tracking_block *b;

    if (size == 0 || !ax_is_pow2(align)) {
        misuse(t, size == 0 ? "alloc with size 0" : "alloc with non-power-of-two align", NULL);
        return NULL;
    }
    ea = effective_align(align);
    if (!ax_size_add(prefix_size(ea), size, &total)) {
        return NULL;
    }
    base = (unsigned char *)t->parent.alloc(t->parent.ctx, total, ea);
    if (base == NULL) {
        return NULL;
    }
    b = block_of(base + prefix_size(ea));
    b->size = size;
    b->align = ea;
    link_front(t, b);

    t->alloc_count++;
    t->live_count++;
    t->live_bytes += size;
    note_live_bytes(t);
    return user_of(b);
}

static void *tracking_resize(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align)
{
    ax_tracking_allocator *t = (ax_tracking_allocator *)ctx;
    ax_tracking_block *b;
    ax_tracking_block saved;
    size_t total_old;
    size_t total_new;
    unsigned char *base;

    if (ptr == NULL) {
        misuse(t, "resize of NULL", ptr);
        return NULL;
    }
    b = block_of(ptr);
    if (!is_live(t, b)) {
        misuse(t, "resize of a pointer that is not a live block (double free or foreign)", ptr);
        return NULL;
    }
    if (new_size == 0 || !ax_is_pow2(align)) {
        misuse(t, new_size == 0 ? "resize to size 0" : "resize with non-power-of-two align", ptr);
        return NULL;
    }
    if (effective_align(align) != b->align) {
        misuse(t, "resize align differs from the block's", ptr);
        return NULL;
    }
    if (old_size != b->size) {
        misuse(t, "resize old_size differs from the block's size", ptr);
    }

    if (!ax_size_add(prefix_size(b->align), new_size, &total_new)) {
        return NULL;
    }
    total_old = prefix_size(b->align) + b->size;

    /* The parent may move the block, record included; neighbours must be relinked to it. */
    saved = *b;
    unlink_block(t, b);
    base = (unsigned char *)t->parent.resize(t->parent.ctx, base_of(b), total_old, total_new,
                                             saved.align);
    if (base == NULL) {
        link_front(t, b); /* unchanged on failure */
        return NULL;
    }
    b = block_of(base + prefix_size(saved.align));
    b->size = new_size;
    b->align = saved.align;
    link_front(t, b);

    t->resize_count++;
    t->live_bytes = t->live_bytes - saved.size + new_size;
    note_live_bytes(t);
    return user_of(b);
}

static void tracking_free(void *ctx, void *ptr, size_t size)
{
    ax_tracking_allocator *t = (ax_tracking_allocator *)ctx;
    ax_tracking_block *b;

    if (ptr == NULL) {
        return;
    }
    b = block_of(ptr);
    if (!is_live(t, b)) {
        misuse(t, "free of a pointer that is not a live block (double free or foreign)", ptr);
        return;
    }
    if (size != b->size) {
        misuse(t, "free size differs from the block's size", ptr);
    }
    unlink_block(t, b);
    t->free_count++;
    t->live_count--;
    t->live_bytes -= b->size;
    t->parent.free(t->parent.ctx, base_of(b), prefix_size(b->align) + b->size);
}

void ax_tracking_allocator_init(ax_tracking_allocator *t, const ax_allocator *parent)
{
    AX_ASSERT(t != NULL && parent != NULL);
    *t = (ax_tracking_allocator){0};
    t->parent = *parent;
    t->abort_on_misuse = true;
}

ax_allocator ax_tracking_allocator_get(ax_tracking_allocator *t)
{
    ax_allocator a;
    a.alloc = tracking_alloc;
    a.resize = tracking_resize;
    a.free = tracking_free;
    a.ctx = t;
    return a;
}

size_t ax_tracking_allocator_deinit(ax_tracking_allocator *t)
{
    size_t leaked = t->live_count;
    if (leaked != 0) {
        fprintf(stderr, "ax_tracking_allocator: %zu block(s) leaked, %zu bytes\n", leaked,
                t->live_bytes);
    }
    while (t->live != NULL) {
        ax_tracking_block *b = t->live;
        fprintf(stderr, "  leaked %zu bytes (align %zu) at %p\n", b->size, b->align, user_of(b));
        unlink_block(t, b);
        t->parent.free(t->parent.ctx, base_of(b), prefix_size(b->align) + b->size);
    }
    fflush(stderr);
    t->live_count = 0;
    t->live_bytes = 0;
    return leaked;
}
