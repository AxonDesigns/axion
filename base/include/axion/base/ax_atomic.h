/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: atomics with explicit memory ordering.
 *
 * Deliberately small: load, store, fetch_add and compare-exchange on 32-bit and 64-bit unsigned
 * integers, and load, store and compare-exchange on pointers. Every call names its memory order.
 *
 * Implementation:
 *   - GCC, Clang (clang-cl included) and Emscripten: C11 <stdatomic.h>.
 *   - MSVC: its C11 atomics still need /experimental:c11atomics, so the wrapper uses the
 *     Interlocked intrinsics. Read-modify-write operations and non-relaxed stores are full
 *     barriers there, and loads are __iso_volatile_load32/64 followed by the barrier the order
 *     needs, so every operation is at least as strong as requested. 64-bit Windows only (x64 and
 *     ARM64); 32-bit Windows is not a supported target.
 *   Define AX_ATOMIC_FORCE_INTERLOCKED to use the MSVC path with clang-cl, to test it.
 *
 * Valid orders, as in C11: loads take RELAXED, ACQUIRE or SEQ_CST; stores take RELAXED, RELEASE
 * or SEQ_CST; read-modify-writes take any. A compare-exchange failure order is RELAXED, ACQUIRE
 * or SEQ_CST. Debug builds assert these.
 *
 * Objects are initialized with AX_ATOMIC_INIT(value) or ax_atomic_init_*() before they are shared.
 */
#ifndef AX_BASE_ATOMIC_H
#define AX_BASE_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_platform.h"
#include "ax_assert.h"
#include "ax_compiler.h"

typedef enum ax_memory_order {
    AX_MO_RELAXED,
    AX_MO_ACQUIRE,
    AX_MO_RELEASE,
    AX_MO_ACQ_REL,
    AX_MO_SEQ_CST
} ax_memory_order;

#define AX_ATOMIC_INIT(value) {(value)}

#define AX__MO_VALID_LOAD(o)  ((o) == AX_MO_RELAXED || (o) == AX_MO_ACQUIRE || (o) == AX_MO_SEQ_CST)
#define AX__MO_VALID_STORE(o) ((o) == AX_MO_RELAXED || (o) == AX_MO_RELEASE || (o) == AX_MO_SEQ_CST)

#if (defined(_MSC_VER) && !defined(__clang__)) || defined(AX_ATOMIC_FORCE_INTERLOCKED)
#    define AX_ATOMIC_INTERLOCKED 1
#else
#    define AX_ATOMIC_INTERLOCKED 0
#endif

#if AX_ATOMIC_INTERLOCKED
/* ============================== MSVC: Interlocked intrinsics ================================ */

#    if !defined(_WIN64)
#        error "ax_atomic.h: 32-bit Windows is not supported"
#    endif
#    include <intrin.h>

typedef struct ax_atomic_u32 {
    volatile long v;
} ax_atomic_u32;

typedef struct ax_atomic_u64 {
    AX_ALIGNAS(8) volatile long long v;
} ax_atomic_u64;

typedef struct ax_atomic_ptr {
    void *volatile v;
} ax_atomic_ptr;

/* MSVC declares the intrinsics with __int32/__int64, which are int/long long there; the standard
 * spellings keep Clang's -Wpedantic quiet when this path is tested with clang-cl. */
AX_STATIC_ASSERT(sizeof(long) == 4 && sizeof(int) == 4, "Interlocked 32-bit operations take long");
AX_STATIC_ASSERT(sizeof(long long) == 8, "Interlocked 64-bit operations take long long");
AX_STATIC_ASSERT(sizeof(void *) == 8, "64-bit Windows only");

/* Barrier after a load with acquire or stronger semantics. x64 loads are already acquire in
 * hardware, so a compiler barrier suffices; ARM64 needs a data memory barrier. */
static inline void ax__atomic_after_load(ax_memory_order order)
{
    if (order != AX_MO_RELAXED) {
#    if defined(_M_ARM64) || defined(_M_ARM64EC)
        __dmb(_ARM64_BARRIER_ISH);
#    else
        _ReadWriteBarrier();
#    endif
    }
}

static inline void ax_atomic_init_u32(ax_atomic_u32 *a, uint32_t value)
{
    a->v = (long)value;
}

static inline uint32_t ax_atomic_load_u32(const ax_atomic_u32 *a, ax_memory_order order)
{
    uint32_t value;
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    value = (uint32_t)__iso_volatile_load32((const volatile int *)&a->v);
    ax__atomic_after_load(order);
    return value;
}

static inline void ax_atomic_store_u32(ax_atomic_u32 *a, uint32_t value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    if (order == AX_MO_RELAXED) {
        __iso_volatile_store32((volatile int *)&a->v, (int)value);
    } else {
        _InterlockedExchange(&a->v, (long)value); /* full barrier */
    }
}

static inline uint32_t ax_atomic_fetch_add_u32(ax_atomic_u32 *a, uint32_t delta,
                                               ax_memory_order order)
{
    (void)order;
    return (uint32_t)_InterlockedExchangeAdd(&a->v, (long)delta);
}

static inline bool ax_atomic_compare_exchange_u32(ax_atomic_u32 *a, uint32_t *expected,
                                                  uint32_t desired, ax_memory_order success,
                                                  ax_memory_order failure)
{
    long prev;
    (void)success;
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    prev = _InterlockedCompareExchange(&a->v, (long)desired, (long)*expected);
    if (prev == (long)*expected) {
        return true;
    }
    *expected = (uint32_t)prev;
    return false;
}

static inline void ax_atomic_init_u64(ax_atomic_u64 *a, uint64_t value)
{
    a->v = (long long)value;
}

static inline uint64_t ax_atomic_load_u64(const ax_atomic_u64 *a, ax_memory_order order)
{
    uint64_t value;
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    value = (uint64_t)__iso_volatile_load64(&a->v);
    ax__atomic_after_load(order);
    return value;
}

static inline void ax_atomic_store_u64(ax_atomic_u64 *a, uint64_t value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    if (order == AX_MO_RELAXED) {
        __iso_volatile_store64(&a->v, (long long)value);
    } else {
        _InterlockedExchange64(&a->v, (long long)value);
    }
}

static inline uint64_t ax_atomic_fetch_add_u64(ax_atomic_u64 *a, uint64_t delta,
                                               ax_memory_order order)
{
    (void)order;
    return (uint64_t)_InterlockedExchangeAdd64(&a->v, (long long)delta);
}

static inline bool ax_atomic_compare_exchange_u64(ax_atomic_u64 *a, uint64_t *expected,
                                                  uint64_t desired, ax_memory_order success,
                                                  ax_memory_order failure)
{
    long long prev;
    (void)success;
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    prev = _InterlockedCompareExchange64(&a->v, (long long)desired, (long long)*expected);
    if (prev == (long long)*expected) {
        return true;
    }
    *expected = (uint64_t)prev;
    return false;
}

static inline void ax_atomic_init_ptr(ax_atomic_ptr *a, void *value)
{
    a->v = value;
}

static inline void *ax_atomic_load_ptr(const ax_atomic_ptr *a, ax_memory_order order)
{
    void *value;
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    value = (void *)__iso_volatile_load64((const volatile long long *)&a->v);
    ax__atomic_after_load(order);
    return value;
}

static inline void ax_atomic_store_ptr(ax_atomic_ptr *a, void *value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    if (order == AX_MO_RELAXED) {
        __iso_volatile_store64((volatile long long *)&a->v, (long long)value);
    } else {
        _InterlockedExchangePointer((void *volatile *)&a->v, value);
    }
}

static inline bool ax_atomic_compare_exchange_ptr(ax_atomic_ptr *a, void **expected, void *desired,
                                                  ax_memory_order success, ax_memory_order failure)
{
    void *prev;
    (void)success;
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    prev = _InterlockedCompareExchangePointer((void *volatile *)&a->v, desired, *expected);
    if (prev == *expected) {
        return true;
    }
    *expected = prev;
    return false;
}

#else
/* ==================================== C11 <stdatomic.h> ===================================== */

#    include <stdatomic.h>

typedef struct ax_atomic_u32 {
    _Atomic uint32_t v;
} ax_atomic_u32;

typedef struct ax_atomic_u64 {
    _Atomic uint64_t v;
} ax_atomic_u64;

typedef struct ax_atomic_ptr {
    _Atomic(void *) v;
} ax_atomic_ptr;

static inline memory_order ax__atomic_order(ax_memory_order order)
{
    switch (order) {
    case AX_MO_RELAXED:
        return memory_order_relaxed;
    case AX_MO_ACQUIRE:
        return memory_order_acquire;
    case AX_MO_RELEASE:
        return memory_order_release;
    case AX_MO_ACQ_REL:
        return memory_order_acq_rel;
    case AX_MO_SEQ_CST:
        break;
    }
    return memory_order_seq_cst;
}

/* Loads take a const object; C11 atomic_load wants a non-const pointer, which it only reads. */
#    define AX__ATOMIC_MUT(type, p) ((type *)(uintptr_t)(p))

static inline void ax_atomic_init_u32(ax_atomic_u32 *a, uint32_t value)
{
    atomic_init(&a->v, value);
}

static inline uint32_t ax_atomic_load_u32(const ax_atomic_u32 *a, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    return atomic_load_explicit(&AX__ATOMIC_MUT(ax_atomic_u32, a)->v, ax__atomic_order(order));
}

static inline void ax_atomic_store_u32(ax_atomic_u32 *a, uint32_t value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    atomic_store_explicit(&a->v, value, ax__atomic_order(order));
}

static inline uint32_t ax_atomic_fetch_add_u32(ax_atomic_u32 *a, uint32_t delta,
                                               ax_memory_order order)
{
    return atomic_fetch_add_explicit(&a->v, delta, ax__atomic_order(order));
}

static inline bool ax_atomic_compare_exchange_u32(ax_atomic_u32 *a, uint32_t *expected,
                                                  uint32_t desired, ax_memory_order success,
                                                  ax_memory_order failure)
{
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    return atomic_compare_exchange_strong_explicit(
        &a->v, expected, desired, ax__atomic_order(success), ax__atomic_order(failure));
}

static inline void ax_atomic_init_u64(ax_atomic_u64 *a, uint64_t value)
{
    atomic_init(&a->v, value);
}

static inline uint64_t ax_atomic_load_u64(const ax_atomic_u64 *a, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    return atomic_load_explicit(&AX__ATOMIC_MUT(ax_atomic_u64, a)->v, ax__atomic_order(order));
}

static inline void ax_atomic_store_u64(ax_atomic_u64 *a, uint64_t value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    atomic_store_explicit(&a->v, value, ax__atomic_order(order));
}

static inline uint64_t ax_atomic_fetch_add_u64(ax_atomic_u64 *a, uint64_t delta,
                                               ax_memory_order order)
{
    return atomic_fetch_add_explicit(&a->v, delta, ax__atomic_order(order));
}

static inline bool ax_atomic_compare_exchange_u64(ax_atomic_u64 *a, uint64_t *expected,
                                                  uint64_t desired, ax_memory_order success,
                                                  ax_memory_order failure)
{
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    return atomic_compare_exchange_strong_explicit(
        &a->v, expected, desired, ax__atomic_order(success), ax__atomic_order(failure));
}

static inline void ax_atomic_init_ptr(ax_atomic_ptr *a, void *value)
{
    atomic_init(&a->v, value);
}

static inline void *ax_atomic_load_ptr(const ax_atomic_ptr *a, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_LOAD(order));
    return atomic_load_explicit(&AX__ATOMIC_MUT(ax_atomic_ptr, a)->v, ax__atomic_order(order));
}

static inline void ax_atomic_store_ptr(ax_atomic_ptr *a, void *value, ax_memory_order order)
{
    AX_ASSERT(AX__MO_VALID_STORE(order));
    atomic_store_explicit(&a->v, value, ax__atomic_order(order));
}

static inline bool ax_atomic_compare_exchange_ptr(ax_atomic_ptr *a, void **expected, void *desired,
                                                  ax_memory_order success, ax_memory_order failure)
{
    AX_ASSERT(AX__MO_VALID_LOAD(failure));
    return atomic_compare_exchange_strong_explicit(
        &a->v, expected, desired, ax__atomic_order(success), ax__atomic_order(failure));
}

#endif /* AX_ATOMIC_INTERLOCKED */

#endif /* AX_BASE_ATOMIC_H */
