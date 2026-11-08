/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * A sharded LRU cache of reference-counted entries, shared by shaping and glyphs. A shard is a
 * chained hash table with its own mutex and its own LRU list, so threads that touch different
 * keys rarely meet. The cache holds one reference to each entry it contains; eviction drops that
 * reference, and an entry is destroyed when its last reference goes, so a caller holding one
 * never sees it freed.
 */
#include <string.h>

#include "text_internal.h"

uint64_t tx_hash_bytes(uint64_t h, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull; /* FNV-1a */
    }
    return h;
}

void tx_cache_init(tx_cache *c, const ax_allocator *allocator, size_t budget,
                   bool (*equal)(const tx_entry *, const void *), void (*destroy)(tx_entry *, void *),
                   void *user)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    c->allocator = allocator;
    c->budget = budget / TX_CACHE_SHARDS > 0 ? budget / TX_CACHE_SHARDS : 1;
    c->equal = equal;
    c->destroy = destroy;
    c->user = user;
    for (i = 0; i < TX_CACHE_SHARDS; ++i) {
        ax_mutex_init(&c->shards[i].mutex);
    }
}

static void lru_unlink(tx_shard *s, tx_entry *e)
{
    if (e->lru_prev != NULL) {
        e->lru_prev->lru_next = e->lru_next;
    } else {
        s->lru_head = e->lru_next;
    }
    if (e->lru_next != NULL) {
        e->lru_next->lru_prev = e->lru_prev;
    } else {
        s->lru_tail = e->lru_prev;
    }
    e->lru_prev = e->lru_next = NULL;
}

static void lru_push_front(tx_shard *s, tx_entry *e)
{
    e->lru_prev = NULL;
    e->lru_next = s->lru_head;
    if (s->lru_head != NULL) {
        s->lru_head->lru_prev = e;
    }
    s->lru_head = e;
    if (s->lru_tail == NULL) {
        s->lru_tail = e;
    }
}

static void bucket_unlink(tx_shard *s, tx_entry *e)
{
    tx_entry **link = &s->buckets[e->hash & (s->bucket_count - 1u)];
    while (*link != NULL && *link != e) {
        link = &(*link)->next;
    }
    if (*link == e) {
        *link = e->next;
    }
    e->next = NULL;
}

void tx_cache_release(tx_cache *c, tx_entry *e)
{
    if (e != NULL && ax_atomic_fetch_add_u32(&e->refs, UINT32_MAX, AX_MO_ACQ_REL) == 1u) {
        c->destroy(e, c->user);
    }
}

void tx_cache_deinit(tx_cache *c)
{
    uint32_t i;
    for (i = 0; i < TX_CACHE_SHARDS; ++i) {
        tx_shard *s = &c->shards[i];
        tx_entry *e = s->lru_head;
        while (e != NULL) {
            tx_entry *next = e->lru_next;
            e->in_cache = false;
            tx_cache_release(c, e);
            e = next;
        }
        ax_mem_free(c->allocator, s->buckets, sizeof(tx_entry *) * s->bucket_count);
        ax_mutex_deinit(&s->mutex);
    }
    memset(c, 0, sizeof *c);
}

static tx_shard *shard_of(tx_cache *c, uint64_t hash)
{
    return &c->shards[(hash >> 59) & (TX_CACHE_SHARDS - 1u)];
}

tx_entry *tx_cache_get(tx_cache *c, uint64_t hash, const void *key)
{
    tx_shard *s = shard_of(c, hash);
    tx_entry *e = NULL;
    ax_mutex_lock(&s->mutex);
    if (s->bucket_count > 0) {
        e = s->buckets[hash & (s->bucket_count - 1u)];
        while (e != NULL && !(e->hash == hash && c->equal(e, key))) {
            e = e->next;
        }
        if (e != NULL) {
            (void)ax_atomic_fetch_add_u32(&e->refs, 1u, AX_MO_RELAXED);
            lru_unlink(s, e);
            lru_push_front(s, e);
        }
    }
    ax_mutex_unlock(&s->mutex);
    return e;
}

static bool grow(tx_cache *c, tx_shard *s)
{
    uint32_t cap = s->bucket_count < 64u ? 64u : s->bucket_count * 2u;
    tx_entry **fresh =
        (tx_entry **)ax_mem_alloc(c->allocator, sizeof(tx_entry *) * cap, AX_ALIGNOF(tx_entry *));
    tx_entry *e;
    if (fresh == NULL) {
        return false;
    }
    memset(fresh, 0, sizeof(tx_entry *) * cap);
    for (e = s->lru_head; e != NULL; e = e->lru_next) {
        tx_entry **b = &fresh[e->hash & (cap - 1u)];
        e->next = *b;
        *b = e;
    }
    ax_mem_free(c->allocator, s->buckets, sizeof(tx_entry *) * s->bucket_count);
    s->buckets = fresh;
    s->bucket_count = cap;
    return true;
}

tx_entry *tx_cache_put(tx_cache *c, tx_entry *e, const void *key)
{
    tx_shard *s = shard_of(c, e->hash);
    tx_entry *victims = NULL;
    tx_entry *other;
    ax_mutex_lock(&s->mutex);
    if (s->bucket_count > 0) {
        for (other = s->buckets[e->hash & (s->bucket_count - 1u)]; other != NULL;
             other = other->next) {
            if (other->hash == e->hash && c->equal(other, key)) {
                (void)ax_atomic_fetch_add_u32(&other->refs, 1u, AX_MO_RELAXED);
                lru_unlink(s, other);
                lru_push_front(s, other);
                ax_mutex_unlock(&s->mutex);
                return other;
            }
        }
    }
    if (s->count + 1u > s->bucket_count / 2u && !grow(c, s) && s->bucket_count == 0) {
        ax_mutex_unlock(&s->mutex);
        return e; /* out of memory: the caller keeps an uncached entry */
    }
    ax_atomic_store_u32(&e->refs, 2u, AX_MO_RELAXED); /* the caller's and the cache's */
    e->in_cache = true;
    e->next = s->buckets[e->hash & (s->bucket_count - 1u)];
    s->buckets[e->hash & (s->bucket_count - 1u)] = e;
    lru_push_front(s, e);
    s->count++;
    s->bytes += e->bytes;
    /* Evict from the back: entries nobody else holds. */
    other = s->lru_tail;
    while (s->bytes > c->budget && other != NULL) {
        tx_entry *prev = other->lru_prev;
        if (other != e && ax_atomic_load_u32(&other->refs, AX_MO_ACQUIRE) == 1u) {
            bucket_unlink(s, other);
            lru_unlink(s, other);
            s->count--;
            s->bytes -= other->bytes;
            other->in_cache = false;
            other->next = victims;
            victims = other;
        }
        other = prev;
    }
    ax_mutex_unlock(&s->mutex);
    while (victims != NULL) {
        tx_entry *next = victims->next;
        tx_cache_release(c, victims);
        victims = next;
    }
    return e;
}

size_t tx_cache_bytes(tx_cache *c)
{
    size_t total = 0;
    uint32_t i;
    for (i = 0; i < TX_CACHE_SHARDS; ++i) {
        ax_mutex_lock(&c->shards[i].mutex);
        total += c->shards[i].bytes;
        ax_mutex_unlock(&c->shards[i].mutex);
    }
    return total;
}

uint32_t tx_cache_evict(tx_cache *c, size_t target, bool (*evictable)(const tx_entry *, void *),
                        void *arg, uint64_t *refused)
{
    size_t total = tx_cache_bytes(c);
    uint32_t evicted = 0;
    uint32_t i;
    for (i = 0; i < TX_CACHE_SHARDS && total > target; ++i) {
        tx_shard *s = &c->shards[i];
        tx_entry *victims = NULL;
        tx_entry *e;
        ax_mutex_lock(&s->mutex);
        e = s->lru_tail;
        while (e != NULL && total > target) {
            tx_entry *prev = e->lru_prev;
            if (ax_atomic_load_u32(&e->refs, AX_MO_ACQUIRE) == 1u) {
                if (evictable(e, arg)) {
                    bucket_unlink(s, e);
                    lru_unlink(s, e);
                    s->count--;
                    s->bytes -= e->bytes;
                    total -= e->bytes;
                    e->in_cache = false;
                    e->next = victims;
                    victims = e;
                    evicted++;
                } else if (refused != NULL) {
                    (*refused)++;
                }
            }
            e = prev;
        }
        ax_mutex_unlock(&s->mutex);
        while (victims != NULL) {
            tx_entry *next = victims->next;
            tx_cache_release(c, victims);
            victims = next;
        }
    }
    return evicted;
}
