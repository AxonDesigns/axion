/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The world: lifecycle, thread roles, the clock, and the queues between the app thread and the
 * scene thread.
 *
 * Queue of committed transactions: a wait-free single-producer single-consumer linked queue with
 * a stub node (the owner chose it over a fixed ring, decision 65). The producer links a node
 * after its tail with a release store; the consumer follows head->next with an acquire load. The
 * node the consumer last took stays as the dummy head until the next take, because the producer
 * may still be about to read its link only until it has written it, and the consumer has seen
 * that write.
 */
#include <stdio.h>
#include <string.h>

#include "world_internal.h"

#include <axion/ax_error.h>
#include <axion/base/ax_error_set.h>
#include <axion/base/ax_memory.h>

/* AX_MAX_WORLDS (ax_world.h) is the guaranteed minimum; the table holds exactly that many. */
#define WORLD_SLOTS AX_MAX_WORLDS

#define RECYCLED_CAPACITY 4096u
#define RETURNED_CAPACITY 256u

/* Live worlds by slot, claimed with compare-and-swap like ax_app's table (platform/common). */
static ax_atomic_ptr g_worlds[WORLD_SLOTS];
static ax_atomic_u32 g_world_generation = AX_ATOMIC_INIT(1);

world_impl *ax__world_from_handle(ax_world world)
{
    uint32_t slot = ax_handle_slot(world.h);
    world_impl *w;
    if (world.h == 0 || slot >= WORLD_SLOTS) {
        return NULL;
    }
    w = (world_impl *)ax_atomic_load_ptr(&g_worlds[slot], AX_MO_ACQUIRE);
    if (w == NULL || w->generation != ax_handle_generation(world.h)) {
        return NULL;
    }
    return w;
}

static uint64_t default_clock(void *user)
{
    (void)user;
    return ax_time_monotonic_ns();
}

static void recycle_handle(world_impl *w, ax_node next_handle);

/* The store reports each destroyed node's slot. While a rejected transaction is rolled back the
 * app thread recycles the handles it created, so the scene must not report them too. */
static void on_slot_freed(void *user, ax_node next_handle)
{
    world_impl *w = (world_impl *)user;
    ax__mirror_touch_slot(w, ax_handle_slot(next_handle.h)); /* the node is gone */
    if (!w->suppress_recycle) {
        recycle_handle(w, next_handle);
    }
}

/* ---- Lifecycle ------------------------------------------------------------------------------- */

static void free_world(world_impl *w)
{
    ax_allocator a = w->allocator;
    ax_txn *t;
    /* Every transaction object, wherever it is (queued, returned, free, the dummy head, open). */
    t = w->all_txns;
    while (t != NULL) {
        ax_txn *next = t->next_all;
        ax__txn_free(w, t);
        t = next;
    }
    ax_spsc_ring_deinit(&w->recycled);
    ax_spsc_ring_deinit(&w->returned);
    ax_spsc_ring_deinit(&w->failures);
    ax__events_free(w);
    if (w->scene_ready) {
        ax_node_store_bind_thread(&w->scene.store); /* torn down on the calling thread */
        ax_scene_deinit(&w->scene);
    }
    ax__apply_free(w);
    ax__frame_free(w);
    ax__mirror_free(w);
    ax_mem_free(&a, w->free_handles, sizeof(ax_node) * w->free_handle_capacity);
    ax_mem_free(&a, w->recycle_backlog, sizeof(ax_node) * w->recycle_backlog_capacity);
    ax_mem_free(&a, w->return_backlog, sizeof(ax_txn *) * w->return_backlog_capacity);
    ax_mem_free(&a, w, sizeof *w);
}

ax_result ax_world_create(const ax_world_desc *desc, ax_world *out_world)
{
    const ax_allocator *a = ax_default_allocator();
    world_impl *w;
    ax_node_store_desc store_desc;
    uint32_t slot;
    uint64_t me = ax_thread_current_id();

    AX_ASSERT(out_world != NULL);
    out_world->h = 0;
    if (desc != NULL) {
        if (desc->struct_size < sizeof(ax_world_desc)) {
            return ax_set_error(AX_ERR_ABI_MISMATCH,
                                "ax_world_create: ax_world_desc.struct_size %u, expected %u",
                                (unsigned)desc->struct_size, (unsigned)sizeof(ax_world_desc));
        }
        if (desc->allocator != NULL) {
            a = desc->allocator;
        }
    }
    w = (world_impl *)ax_mem_alloc(a, sizeof *w, AX_ALIGNOF(world_impl));
    if (w == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_create: out of memory");
    }
    memset(w, 0, sizeof *w);
    w->allocator = *a;
    w->clock = desc != NULL && desc->clock != NULL ? desc->clock : default_clock;
    w->clock_user = desc != NULL && desc->clock != NULL ? desc->clock_user : NULL;
    w->app_thread = me;
    w->scene_thread = me;
    ax_atomic_init_ptr(&w->stub.next, NULL);
    w->tail = &w->stub;
    w->head = &w->stub;
    ax_atomic_init_u64(&w->applied_seq, 0);
    ax_atomic_init_u32(&w->dropped_failures, 0);

    if (ax_spsc_ring_init(&w->recycled, a, RECYCLED_CAPACITY, sizeof(ax_node)) != AX_OK ||
        ax_spsc_ring_init(&w->returned, a, RETURNED_CAPACITY, sizeof(ax_txn *)) != AX_OK ||
        ax_spsc_ring_init(&w->failures, a, AX_TXN_ERROR_QUEUE, sizeof(txn_failure)) != AX_OK ||
        ax__events_init(w) != AX_OK) {
        free_world(w);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_create: out of memory");
    }

    w->generation = ax_atomic_fetch_add_u32(&g_world_generation, 1, AX_MO_RELAXED);
    if (w->generation == 0) {
        w->generation = ax_atomic_fetch_add_u32(&g_world_generation, 1, AX_MO_RELAXED);
    }
    for (slot = 0; slot < WORLD_SLOTS; ++slot) {
        void *expected = NULL;
        if (ax_atomic_compare_exchange_ptr(&g_worlds[slot], &expected, w, AX_MO_ACQ_REL,
                                           AX_MO_RELAXED)) {
            break;
        }
    }
    if (slot == WORLD_SLOTS) {
        free_world(w);
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_world_create: at most %u worlds may exist",
                            AX_MAX_WORLDS);
    }
    w->slot = slot;

    memset(&store_desc, 0, sizeof store_desc);
    store_desc.allocator = &w->allocator;
    store_desc.on_slot_freed = on_slot_freed;
    store_desc.user = w;
    ax_scene_init(&w->scene, &store_desc);
    w->scene_ready = true;
    ax__mirror_init(w);

    out_world->h = ax_handle_make(slot, w->generation);
    return AX_OK;
}

ax_result ax_world_destroy(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_destroy: invalid or destroyed world");
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_world_destroy: not the world's app thread");
    /* Every ax_txn_on_presented callback still waiting comes now, while the world is valid. */
    ax__events_world_destroyed(w);
    ax_atomic_store_ptr(&g_worlds[w->slot], NULL, AX_MO_RELEASE);
    free_world(w);
    return AX_OK;
}

uint64_t ax_world_now(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    return w != NULL ? ax__world_now(w) : 0;
}

bool ax_world_is_valid(ax_world world)
{
    return ax__world_from_handle(world) != NULL;
}

bool ax_world_clock_is_monotonic(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    return w != NULL && w->clock == default_clock;
}

uint64_t ax_world_time_from_monotonic(ax_world world, uint64_t monotonic_ns)
{
    world_impl *w = ax__world_from_handle(world);
    uint64_t now_mono;
    uint64_t now_world;
    if (w == NULL) {
        return 0;
    }
    if (w->clock == default_clock) {
        return monotonic_ns; /* the same clock (ax_time_monotonic_ns) */
    }
    now_mono = ax_time_monotonic_ns();
    now_world = ax__world_now(w);
    if (monotonic_ns >= now_mono) {
        uint64_t ahead = monotonic_ns - now_mono;
        return ahead > UINT64_MAX - now_world ? UINT64_MAX : now_world + ahead;
    }
    {
        uint64_t behind = now_mono - monotonic_ns;
        return behind > now_world ? 0 : now_world - behind;
    }
}

void ax_world_bind_scene_thread(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    if (w != NULL) {
        w->scene_thread = ax_thread_current_id();
        ax_node_store_bind_thread(&w->scene.store);
    }
}

void ax_world_bind_app_thread(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    if (w != NULL) {
        w->app_thread = ax_thread_current_id();
    }
}

ax_scene *ax_world_scene(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    if (w == NULL) {
        return NULL;
    }
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_scene: not the world's scene thread");
    return &w->scene;
}

/* ---- Handles (app thread) -------------------------------------------------------------------- */

ax_node ax__world_mint_handle(world_impl *w)
{
    ax_node n = AX_HANDLE_NULL_INIT;
    if (w->free_handle_count > 0) {
        return w->free_handles[--w->free_handle_count];
    }
    if (w->next_new_slot < AX_NODE_STORE_MAX_SLOTS) {
        n.h = ax_handle_make(w->next_new_slot++, 1);
    }
    return n;
}

static void push_free_handle(world_impl *w, ax_node h)
{
    if (w->free_handle_count == w->free_handle_capacity) {
        uint32_t cap = w->free_handle_capacity < 64u ? 64u : w->free_handle_capacity * 2u;
        ax_node *fresh =
            (ax_node *)ax_mem_alloc(&w->allocator, sizeof(ax_node) * cap, AX_ALIGNOF(ax_node));
        if (fresh == NULL) {
            w->retired_slots++; /* the slot is lost, never reused: safe */
            return;
        }
        if (w->free_handles != NULL) {
            memcpy(fresh, w->free_handles, sizeof(ax_node) * w->free_handle_count);
            ax_mem_free(&w->allocator, w->free_handles, sizeof(ax_node) * w->free_handle_capacity);
        }
        w->free_handles = fresh;
        w->free_handle_capacity = cap;
    }
    w->free_handles[w->free_handle_count++] = h;
}

void ax__world_unmint_handle(world_impl *w, ax_node handle)
{
    uint32_t gen = ax_handle_generation(handle.h);
    ax_node next;
    if (gen == UINT32_MAX) {
        w->retired_slots++; /* the last generation: retire the slot, as the store does */
        return;
    }
    next.h = ax_handle_make(ax_handle_slot(handle.h), gen + 1u);
    push_free_handle(w, next);
}

void ax__world_reclaim(world_impl *w)
{
    ax_txn *t;
    ax_node h;
    while (ax_spsc_ring_pop(&w->returned, &t)) {
        ax__txn_release(w, t, t->result == AX_OK);
    }
    while (ax_spsc_ring_pop(&w->recycled, &h)) {
        push_free_handle(w, h);
    }
}

/* ---- The queue ------------------------------------------------------------------------------- */

void ax__world_push_txn(world_impl *w, ax_txn *t)
{
    ax_atomic_store_ptr(&t->link.next, NULL, AX_MO_RELAXED);
    ax_atomic_store_ptr(&w->tail->next, &t->link, AX_MO_RELEASE);
    w->tail = &t->link;
}

static void flush_backlogs(world_impl *w)
{
    uint32_t i = 0;
    while (i < w->return_backlog_count && ax_spsc_ring_push(&w->returned, &w->return_backlog[i])) {
        i++;
    }
    if (i > 0) {
        memmove(w->return_backlog, w->return_backlog + i,
                sizeof(ax_txn *) * (w->return_backlog_count - i));
        w->return_backlog_count -= i;
    }
    i = 0;
    while (i < w->recycle_backlog_count &&
           ax_spsc_ring_push(&w->recycled, &w->recycle_backlog[i])) {
        i++;
    }
    if (i > 0) {
        memmove(w->recycle_backlog, w->recycle_backlog + i,
                sizeof(ax_node) * (w->recycle_backlog_count - i));
        w->recycle_backlog_count -= i;
    }
}

/* Appends to a scene-side backlog array; false when out of memory. */
static bool backlog_push(world_impl *w, void **array, uint32_t *count, uint32_t *capacity,
                         size_t elem, const void *value)
{
    if (*count == *capacity) {
        uint32_t cap = *capacity < 64u ? 64u : *capacity * 2u;
        void *fresh = ax_mem_alloc(&w->allocator, elem * cap, AX_MAX_ALIGN);
        if (fresh == NULL) {
            return false;
        }
        if (*array != NULL) {
            memcpy(fresh, *array, elem * *count);
            ax_mem_free(&w->allocator, *array, elem * *capacity);
        }
        *array = fresh;
        *capacity = cap;
    }
    memcpy((unsigned char *)*array + elem * *count, value, elem);
    (*count)++;
    return true;
}

static void return_txn(world_impl *w, ax_txn *t)
{
    if (w->return_backlog_count == 0 && ax_spsc_ring_push(&w->returned, &t)) {
        return;
    }
    if (!backlog_push(w, (void **)&w->return_backlog, &w->return_backlog_count,
                      &w->return_backlog_capacity, sizeof t, &t)) {
        /* Out of memory: the object stays where it is (on the all-txns list, freed with the
         * world). Its created handles, if any, are never recycled: their slots are lost. */
    }
}

static void recycle_handle(world_impl *w, ax_node next_handle)
{
    if (w->recycle_backlog_count == 0 && ax_spsc_ring_push(&w->recycled, &next_handle)) {
        return;
    }
    (void)backlog_push(w, (void **)&w->recycle_backlog, &w->recycle_backlog_count,
                       &w->recycle_backlog_capacity, sizeof next_handle, &next_handle);
    /* Out of memory: the slot is lost, never reused. */
}

void ax_world_recycle_handle(ax_world world, ax_node next_handle)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_recycle_handle: not the scene thread");
    recycle_handle(w, next_handle);
}

const ax_txn *ax_world_peek_txn(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    txn_link *next;
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_peek_txn: not the scene thread");
    next = (txn_link *)ax_atomic_load_ptr(&w->head->next, AX_MO_ACQUIRE);
    return (const ax_txn *)next;
}

ax_txn *ax_world_take_txn(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    txn_link *old;
    txn_link *next;
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_take_txn: not the scene thread");
    flush_backlogs(w);
    next = (txn_link *)ax_atomic_load_ptr(&w->head->next, AX_MO_ACQUIRE);
    if (next == NULL) {
        return NULL;
    }
    old = w->head;
    w->head = next;
    /* The old dummy is out of the queue: the producer wrote its link, and we have seen it. */
    if (old != &w->stub) {
        return_txn(w, (ax_txn *)old);
    }
    return (ax_txn *)next;
}

void ax_world_finish_txn(ax_world world, ax_txn *t, ax_result result, uint32_t command,
                         const char *message)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL && t != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_finish_txn: not the scene thread");
    t->result = result;
    if (result != AX_OK) {
        txn_failure f;
        memset(&f, 0, sizeof f);
        f.seq = t->seq;
        f.result = result;
        f.command = command;
        snprintf(f.message, sizeof f.message, "%s", message != NULL ? message : "");
        if (!ax_spsc_ring_push(&w->failures, &f)) {
            ax_atomic_fetch_add_u32(&w->dropped_failures, 1, AX_MO_RELAXED);
        }
    }
    ax_atomic_store_u64(&w->applied_seq, t->seq, AX_MO_RELEASE);
    if (result != AX_OK) {
        /* For ax_txn_on_presented; the details stay in the failures ring above. Queued before
         * any report that covers this seq. */
        ax__events_rejected(w, t->seq);
    }
}

/* ---- Results (app thread) -------------------------------------------------------------------- */

bool ax_world_poll_txn_error(ax_world world, ax_txn_error *out, uint32_t *dropped)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(out != NULL);
    memset(out, 0, sizeof *out);
    if (dropped != NULL) {
        *dropped = 0;
    }
    if (w == NULL) {
        (void)ax_set_error(AX_ERR_INVALID_HANDLE, "ax_world_poll_txn_error: invalid world");
        return false;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_world_poll_txn_error: not the app thread");
    ax__world_reclaim(w);
    if (dropped != NULL) {
        uint32_t n = ax_atomic_load_u32(&w->dropped_failures, AX_MO_RELAXED);
        if (n != 0) {
            ax_atomic_fetch_add_u32(&w->dropped_failures, 0u - n, AX_MO_RELAXED);
        }
        *dropped = n;
    }
    if (!ax_spsc_ring_pop(&w->failures, &w->last_failure)) {
        return false;
    }
    out->seq = w->last_failure.seq;
    out->result = w->last_failure.result;
    out->command = w->last_failure.command;
    out->message.ptr = w->last_failure.message;
    out->message.len = strlen(w->last_failure.message);
    return true;
}

uint64_t ax_world_last_committed_seq(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    if (w == NULL) {
        return 0;
    }
    AX_ASSERT_MSG(ax__world_on_app_thread(w), "ax_world_last_committed_seq: not the app thread");
    return w->next_seq;
}

uint64_t ax_world_last_applied_seq(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    return w != NULL ? ax_atomic_load_u64(&w->applied_seq, AX_MO_ACQUIRE) : 0;
}
