/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The job system: worker threads with Chase-Lev work-stealing deques, a shared injection queue
 * for other threads, and counter-based waits in which the waiter runs pending jobs.
 *
 * Deques follow Chase and Lev ("Dynamic circular work-stealing deque", 2005) with a fixed
 * capacity, in the C11 formulation of Lê et al. (PPoPP 2013), except that the two standalone
 * seq_cst fences are folded into seq_cst operations on top and bottom: ThreadSanitizer does not
 * model standalone fences, and would report the slot accesses they order. Slots are three atomic
 * words written and read relaxed, so a thief's read that races with the owner reusing the slot is
 * not a data race; such a read is discarded, because the thief's compare-exchange on top fails.
 *
 * Sleeping uses a Dekker handshake, as the event queue and the IR mailbox do (decisions 31 and
 * 52): a sleeper increments `sleepers` and then rechecks `epoch`; a notifier increments `epoch`
 * and then checks `sleepers`, taking the mutex only when someone sleeps. Both sides are seq_cst,
 * so either the sleeper sees the new epoch or the notifier sees the sleeper.
 */
#include <string.h>

#include "axion/ax_error.h"
#include "axion/base/ax_assert.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_compiler.h"
#include "axion/base/ax_error_set.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_memory.h"
#include "axion/base/ax_thread.h"

#if defined(_MSC_VER) && !defined(__clang__)
#    include <intrin.h>
#endif

#define CACHE_LINE 64

/* Backoff of an idle thread before it sleeps: spins, then yields. */
#define IDLE_SPINS  64u
#define IDLE_YIELDS 16u

static void cpu_relax(void)
{
#if defined(_MSC_VER) && !defined(__clang__)
    _mm_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    __builtin_ia32_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

/* ---- Job records ----------------------------------------------------------------------------- */

typedef struct job_rec {
    ax_job_fn fn;
    void *arg;
    ax_job_counter *counter;
} job_rec;

/* A deque slot: the record as three words. The function pointer travels through memcpy, since
 * converting it to an object pointer or integer is not portable C. */
typedef struct deque_slot {
    ax_atomic_u64 w[3];
} deque_slot;

AX_STATIC_ASSERT(sizeof(ax_job_fn) <= sizeof(uint64_t), "job function pointers must fit 64 bits");

static void slot_store(deque_slot *s, const job_rec *r)
{
    uint64_t fn_word = 0;
    memcpy(&fn_word, &r->fn, sizeof r->fn);
    ax_atomic_store_u64(&s->w[0], fn_word, AX_MO_RELAXED);
    ax_atomic_store_u64(&s->w[1], (uint64_t)(uintptr_t)r->arg, AX_MO_RELAXED);
    ax_atomic_store_u64(&s->w[2], (uint64_t)(uintptr_t)r->counter, AX_MO_RELAXED);
}

static void slot_load(const deque_slot *s, job_rec *r)
{
    uint64_t fn_word = ax_atomic_load_u64(&s->w[0], AX_MO_RELAXED);
    memcpy(&r->fn, &fn_word, sizeof r->fn);
    r->arg = (void *)(uintptr_t)ax_atomic_load_u64(&s->w[1], AX_MO_RELAXED);
    r->counter = (ax_job_counter *)(uintptr_t)ax_atomic_load_u64(&s->w[2], AX_MO_RELAXED);
}

/* ---- Statistics ------------------------------------------------------------------------------ */

typedef struct stat_block {
    ax_atomic_u64 jobs_run;
    ax_atomic_u64 steals;
    ax_atomic_u64 injected;
    ax_atomic_u64 ran_when_full;
    ax_atomic_u64 sleeps;
} stat_block;

static void stat_inc(ax_atomic_u64 *counter)
{
    ax_atomic_fetch_add_u64(counter, 1, AX_MO_RELAXED);
}

/* ---- The system ------------------------------------------------------------------------------ */

typedef struct worker {
    /* Read and written by thieves. */
    AX_ALIGNAS(CACHE_LINE) ax_atomic_u64 top;
    /* Written by the owner only. */
    AX_ALIGNAS(CACHE_LINE) ax_atomic_u64 bottom;
    deque_slot *buf;
    uint64_t mask;
    struct ax_job_system *js;
    uint32_t index;
    ax_thread thread;
    AX_ALIGNAS(CACHE_LINE) stat_block stats;
} worker;

struct ax_job_system {
    ax_allocator allocator;
    bool inline_mode;
    uint32_t worker_count;
    uint32_t deque_capacity;
    worker *workers;

    /* Injection queue: a mutex-protected ring for threads that are not workers. count_hint lets
     * idle threads skip the lock when it is empty. */
    ax_mutex inject_lock;
    job_rec *inject_ring;
    uint32_t inject_mask;
    uint32_t inject_head;
    uint32_t inject_count;
    ax_atomic_u32 inject_count_hint;

    /* Every submitted job that has not finished (ax_jobs_wait_idle, ax_jobs_destroy). */
    ax_job_counter outstanding;

    /* Sleeping (see the top of this file). */
    ax_mutex sleep_lock;
    ax_cond sleep_cond;
    AX_ALIGNAS(CACHE_LINE) ax_atomic_u64 epoch;
    ax_atomic_u32 sleepers;
    ax_atomic_u32 shutdown;
    ax_atomic_u32 steal_start; /* rotates the first victim of threads that are not workers */

    /* Statistics of threads that are not workers (and of inline mode). */
    AX_ALIGNAS(CACHE_LINE) stat_block stats;
};

static AX_THREAD_LOCAL worker *tl_worker;

static worker *current_worker(const ax_job_system *js)
{
    worker *w = tl_worker;
    return (w != NULL && w->js == js) ? w : NULL;
}

static stat_block *stats_of(ax_job_system *js, worker *self)
{
    return self != NULL ? &self->stats : &js->stats;
}

/* Wakes sleepers after new work, a counter reaching zero, or shutdown. */
static void notify(ax_job_system *js)
{
    ax_atomic_fetch_add_u64(&js->epoch, 1, AX_MO_SEQ_CST);
    if (ax_atomic_load_u32(&js->sleepers, AX_MO_SEQ_CST) != 0) {
        ax_mutex_lock(&js->sleep_lock);
        ax_cond_broadcast(&js->sleep_cond);
        ax_mutex_unlock(&js->sleep_lock);
    }
}

/* Returns true when this decrement took the counter to zero. Nothing may touch the counter
 * afterwards: its waiter may return and free it. */
static bool counter_done(ax_job_counter *counter)
{
    uint32_t before = ax_atomic_fetch_add_u32(&counter->pending, UINT32_MAX, AX_MO_ACQ_REL);
    AX_ASSERT_MSG(before != 0, "ax_job_counter: more jobs finished than were submitted");
    return before == 1;
}

static void run_job(ax_job_system *js, worker *self, const job_rec *r)
{
    bool reached_zero = false;
    r->fn(r->arg);
    stat_inc(&stats_of(js, self)->jobs_run);
    /* outstanding first: once the job's own counter reaches zero its waiter may go on to
     * ax_jobs_destroy, which checks that nothing is outstanding. */
    if (counter_done(&js->outstanding)) {
        reached_zero = true;
    }
    if (r->counter != NULL && counter_done(r->counter)) {
        reached_zero = true;
    }
    if (reached_zero && !js->inline_mode) {
        notify(js);
    }
}

/* ---- Deques ---------------------------------------------------------------------------------- */

/* Owner only. False when full. */
static bool deque_push(worker *w, const job_rec *r)
{
    uint64_t b = ax_atomic_load_u64(&w->bottom, AX_MO_RELAXED);
    uint64_t t = ax_atomic_load_u64(&w->top, AX_MO_ACQUIRE);
    if (b - t > w->mask) {
        return false;
    }
    slot_store(&w->buf[b & w->mask], r);
    ax_atomic_store_u64(&w->bottom, b + 1, AX_MO_RELEASE);
    return true;
}

/* Owner only. */
static bool deque_pop(worker *w, job_rec *out)
{
    uint64_t b = ax_atomic_load_u64(&w->bottom, AX_MO_RELAXED) - 1;
    uint64_t t;
    bool won = true;

    ax_atomic_store_u64(&w->bottom, b, AX_MO_SEQ_CST);
    t = ax_atomic_load_u64(&w->top, AX_MO_SEQ_CST);
    if ((int64_t)(b - t) < 0) {
        /* Empty: restore bottom. */
        ax_atomic_store_u64(&w->bottom, b + 1, AX_MO_RELEASE);
        return false;
    }
    slot_load(&w->buf[b & w->mask], out);
    if (b != t) {
        return true; /* more than one left: no thief can reach this one */
    }
    /* The last job: race the thieves for it. */
    won = ax_atomic_compare_exchange_u64(&w->top, &t, t + 1, AX_MO_SEQ_CST, AX_MO_RELAXED);
    ax_atomic_store_u64(&w->bottom, b + 1, AX_MO_RELEASE);
    return won;
}

typedef enum steal_result { STEAL_EMPTY, STEAL_TAKEN, STEAL_LOST } steal_result;

/* Any thread. */
static steal_result deque_steal(worker *w, job_rec *out)
{
    uint64_t t = ax_atomic_load_u64(&w->top, AX_MO_SEQ_CST);
    uint64_t b = ax_atomic_load_u64(&w->bottom, AX_MO_SEQ_CST);
    if ((int64_t)(b - t) <= 0) {
        return STEAL_EMPTY;
    }
    slot_load(&w->buf[t & w->mask], out);
    if (!ax_atomic_compare_exchange_u64(&w->top, &t, t + 1, AX_MO_SEQ_CST, AX_MO_RELAXED)) {
        return STEAL_LOST; /* another thief or the owner took it; out may be torn */
    }
    return STEAL_TAKEN;
}

/* ---- Injection queue ------------------------------------------------------------------------- */

/* Queues up to count records; returns how many fit. */
static uint32_t inject_push(ax_job_system *js, const job_rec *recs, uint32_t count)
{
    uint32_t n = 0;
    ax_mutex_lock(&js->inject_lock);
    while (n < count && js->inject_count <= js->inject_mask) {
        js->inject_ring[(js->inject_head + js->inject_count) & js->inject_mask] = recs[n];
        js->inject_count++;
        n++;
    }
    ax_atomic_store_u32(&js->inject_count_hint, js->inject_count, AX_MO_RELAXED);
    ax_mutex_unlock(&js->inject_lock);
    return n;
}

static bool inject_pop(ax_job_system *js, job_rec *out)
{
    bool got = false;
    if (ax_atomic_load_u32(&js->inject_count_hint, AX_MO_RELAXED) == 0) {
        return false;
    }
    ax_mutex_lock(&js->inject_lock);
    if (js->inject_count != 0) {
        *out = js->inject_ring[js->inject_head];
        js->inject_head = (js->inject_head + 1) & js->inject_mask;
        js->inject_count--;
        got = true;
    }
    ax_atomic_store_u32(&js->inject_count_hint, js->inject_count, AX_MO_RELAXED);
    ax_mutex_unlock(&js->inject_lock);
    return got;
}

/* ---- Finding work ---------------------------------------------------------------------------- */

/* Runs one pending job, if there is any: own deque, then the injection queue, then steals. */
static bool try_run_one(ax_job_system *js, worker *self)
{
    job_rec r;
    uint32_t n = js->worker_count;
    uint32_t start;
    uint32_t i;

    if (self != NULL && deque_pop(self, &r)) {
        run_job(js, self, &r);
        return true;
    }
    if (inject_pop(js, &r)) {
        stat_inc(&stats_of(js, self)->injected);
        run_job(js, self, &r);
        return true;
    }
    start = self != NULL ? self->index + 1
                         : ax_atomic_fetch_add_u32(&js->steal_start, 1, AX_MO_RELAXED);
    for (i = 0; i < n; ++i) {
        worker *victim = &js->workers[(start + i) % n];
        if (victim == self) {
            continue;
        }
        for (;;) {
            steal_result s = deque_steal(victim, &r);
            if (s == STEAL_TAKEN) {
                stat_inc(&stats_of(js, self)->steals);
                run_job(js, self, &r);
                return true;
            }
            if (s == STEAL_EMPTY) {
                break;
            }
            /* STEAL_LOST: the victim still had work a moment ago; try it again. */
            cpu_relax();
        }
    }
    return false;
}

/* ---- Workers --------------------------------------------------------------------------------- */

static int worker_main(void *arg)
{
    worker *self = (worker *)arg;
    ax_job_system *js = self->js;
    uint32_t idle = 0;

    tl_worker = self;
    for (;;) {
        uint64_t seen_epoch;

        if (try_run_one(js, self)) {
            idle = 0;
            continue;
        }
        if (ax_atomic_load_u32(&js->shutdown, AX_MO_ACQUIRE) != 0) {
            break;
        }
        if (idle < IDLE_SPINS) {
            idle++;
            cpu_relax();
            continue;
        }
        if (idle < IDLE_SPINS + IDLE_YIELDS) {
            idle++;
            ax_thread_yield();
            continue;
        }
        /* Sleep until notified. Anything submitted before seen_epoch was read is found by the
         * search below; anything after changes the epoch or finds this thread asleep. */
        seen_epoch = ax_atomic_load_u64(&js->epoch, AX_MO_SEQ_CST);
        if (try_run_one(js, self)) {
            idle = 0;
            continue;
        }
        ax_mutex_lock(&js->sleep_lock);
        ax_atomic_fetch_add_u32(&js->sleepers, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&js->epoch, AX_MO_SEQ_CST) == seen_epoch &&
            ax_atomic_load_u32(&js->shutdown, AX_MO_SEQ_CST) == 0) {
            stat_inc(&self->stats.sleeps);
            ax_cond_wait(&js->sleep_cond, &js->sleep_lock);
        }
        ax_atomic_fetch_add_u32(&js->sleepers, UINT32_MAX, AX_MO_SEQ_CST);
        ax_mutex_unlock(&js->sleep_lock);
        idle = 0;
    }
    tl_worker = NULL;
    return 0;
}

/* ---- Public API ------------------------------------------------------------------------------ */

ax_jobs_desc ax_jobs_default_desc(void)
{
    ax_jobs_desc d;
    d.allocator = NULL;
    d.mode = AX_JOBS_THREADED;
    d.worker_count = 0;
    d.reserved_threads = AX_JOBS_DEFAULT_RESERVED_THREADS;
    d.deque_capacity = AX_JOBS_DEFAULT_DEQUE_CAPACITY;
    d.inject_capacity = AX_JOBS_DEFAULT_INJECT_CAPACITY;
    return d;
}

static uint32_t round_up_pow2(uint32_t x, uint32_t fallback)
{
    uint32_t p = 1;
    if (x == 0) {
        x = fallback;
    }
    while (p < x && p < 0x80000000u) {
        p <<= 1;
    }
    return p;
}

static void free_system(ax_job_system *js)
{
    uint32_t i;
    ax_allocator a = js->allocator;
    if (js->workers != NULL) {
        for (i = 0; i < js->worker_count; ++i) {
            ax_mem_free(&a, js->workers[i].buf, sizeof(deque_slot) * js->deque_capacity);
        }
        ax_mem_free(&a, js->workers, sizeof(worker) * js->worker_count);
    }
    ax_mem_free(&a, js->inject_ring, sizeof(job_rec) * ((size_t)js->inject_mask + 1));
    if (!js->inline_mode) {
        ax_cond_deinit(&js->sleep_cond);
        ax_mutex_deinit(&js->sleep_lock);
        ax_mutex_deinit(&js->inject_lock);
    }
    ax_mem_free(&a, js, sizeof *js);
}

/* Stops and joins the first `started` workers. */
static void stop_workers(ax_job_system *js, uint32_t started)
{
    uint32_t i;
    ax_atomic_store_u32(&js->shutdown, 1, AX_MO_SEQ_CST);
    notify(js);
    for (i = 0; i < started; ++i) {
        ax_thread_join(&js->workers[i].thread);
    }
}

ax_result ax_jobs_create(const ax_jobs_desc *desc, ax_job_system **out_js)
{
    const ax_allocator *a;
    ax_job_system *js;
    uint32_t inject_capacity;
    uint32_t i;

    AX_ASSERT(desc != NULL && out_js != NULL);
    *out_js = NULL;
    a = desc->allocator != NULL ? desc->allocator : ax_default_allocator();

    js = (ax_job_system *)ax_mem_alloc(a, sizeof *js, AX_ALIGNOF(ax_job_system));
    if (js == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_jobs_create: out of memory");
    }
    memset(js, 0, sizeof *js);
    js->allocator = *a;
    js->inline_mode = desc->mode == AX_JOBS_INLINE || !ax_threads_supported();
    ax_atomic_init_u32(&js->outstanding.pending, 0);
    ax_atomic_init_u64(&js->epoch, 0);
    ax_atomic_init_u32(&js->sleepers, 0);
    ax_atomic_init_u32(&js->shutdown, 0);
    ax_atomic_init_u32(&js->steal_start, 0);
    ax_atomic_init_u32(&js->inject_count_hint, 0);

    if (js->inline_mode) {
        *out_js = js;
        return AX_OK;
    }

    if (desc->worker_count != 0) {
        js->worker_count = desc->worker_count;
    } else {
        uint32_t cpus = ax_cpu_count();
        js->worker_count = cpus > desc->reserved_threads ? cpus - desc->reserved_threads : 1;
    }
    if (js->worker_count > AX_JOBS_MAX_WORKERS) {
        js->worker_count = AX_JOBS_MAX_WORKERS;
    }
    js->deque_capacity = round_up_pow2(desc->deque_capacity, AX_JOBS_DEFAULT_DEQUE_CAPACITY);
    inject_capacity = round_up_pow2(desc->inject_capacity, AX_JOBS_DEFAULT_INJECT_CAPACITY);
    js->inject_mask = inject_capacity - 1;

    ax_mutex_init(&js->inject_lock);
    ax_mutex_init(&js->sleep_lock);
    ax_cond_init(&js->sleep_cond);

    js->inject_ring =
        (job_rec *)ax_mem_alloc(a, sizeof(job_rec) * inject_capacity, AX_ALIGNOF(job_rec));
    js->workers = (worker *)ax_mem_alloc(a, sizeof(worker) * js->worker_count, CACHE_LINE);
    if (js->inject_ring == NULL || js->workers == NULL) {
        if (js->workers == NULL) {
            js->worker_count = 0;
        }
        goto out_of_memory;
    }
    memset(js->workers, 0, sizeof(worker) * js->worker_count);
    for (i = 0; i < js->worker_count; ++i) {
        worker *w = &js->workers[i];
        uint32_t s;
        w->js = js;
        w->index = i;
        w->mask = js->deque_capacity - 1;
        ax_atomic_init_u64(&w->top, 0);
        ax_atomic_init_u64(&w->bottom, 0);
        w->buf = (deque_slot *)ax_mem_alloc(a, sizeof(deque_slot) * js->deque_capacity,
                                            AX_ALIGNOF(deque_slot));
        if (w->buf == NULL) {
            goto out_of_memory;
        }
        for (s = 0; s < js->deque_capacity; ++s) {
            ax_atomic_init_u64(&w->buf[s].w[0], 0);
            ax_atomic_init_u64(&w->buf[s].w[1], 0);
            ax_atomic_init_u64(&w->buf[s].w[2], 0);
        }
    }

    for (i = 0; i < js->worker_count; ++i) {
        ax_result r = ax_thread_create(&js->workers[i].thread, worker_main, &js->workers[i]);
        if (r != AX_OK) {
            /* ax_thread_create recorded the reason; keep it. */
            stop_workers(js, i);
            free_system(js);
            return r;
        }
    }
    *out_js = js;
    return AX_OK;

out_of_memory:
    free_system(js);
    return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_jobs_create: out of memory");
}

void ax_jobs_destroy(ax_job_system *js)
{
    if (js == NULL) {
        return;
    }
    AX_ASSERT_MSG(ax_atomic_load_u32(&js->outstanding.pending, AX_MO_ACQUIRE) == 0,
                  "ax_jobs_destroy: jobs are still pending; wait for their counters first");
    if (!js->inline_mode) {
        stop_workers(js, js->worker_count);
    }
    free_system(js);
}

uint32_t ax_jobs_worker_count(const ax_job_system *js)
{
    return js->worker_count;
}

bool ax_jobs_is_inline(const ax_job_system *js)
{
    return js->inline_mode;
}

uint32_t ax_jobs_current_worker(const ax_job_system *js)
{
    worker *w = current_worker(js);
    return w != NULL ? w->index : UINT32_MAX;
}

static job_rec make_rec(const ax_job *job, ax_job_counter *counter)
{
    job_rec r;
    AX_ASSERT(job->fn != NULL);
    r.fn = job->fn;
    r.arg = job->arg;
    r.counter = counter;
    return r;
}

void ax_jobs_submit(ax_job_system *js, const ax_job *jobs, uint32_t count, ax_job_counter *counter)
{
    worker *self;
    uint32_t i;
    bool notified = false;

    AX_ASSERT(js != NULL && (jobs != NULL || count == 0));
    if (count == 0) {
        return;
    }
    /* Count first, so no waiter can see zero while these jobs are queued. */
    if (counter != NULL) {
        ax_atomic_fetch_add_u32(&counter->pending, count, AX_MO_RELAXED);
    }
    ax_atomic_fetch_add_u32(&js->outstanding.pending, count, AX_MO_RELAXED);

    if (js->inline_mode) {
        for (i = 0; i < count; ++i) {
            job_rec r = make_rec(&jobs[i], counter);
            run_job(js, NULL, &r);
        }
        return;
    }

    self = current_worker(js);
    i = 0;
    while (i < count) {
        job_rec r;
        if (self != NULL) {
            r = make_rec(&jobs[i], counter);
            if (deque_push(self, &r)) {
                i++;
                continue;
            }
        } else {
            /* Batch the injection: lock once for as many as fit. */
            job_rec batch[32];
            uint32_t n = 0;
            uint32_t pushed;
            while (n < AX_COUNTOF(batch) && i + n < count) {
                batch[n] = make_rec(&jobs[i + n], counter);
                n++;
            }
            pushed = inject_push(js, batch, n);
            i += pushed;
            if (pushed == n) {
                continue;
            }
            r = make_rec(&jobs[i], counter);
        }
        /* The queue is full: wake the others for what is queued, then run this job here. */
        if (!notified) {
            notify(js);
            notified = true;
        }
        stat_inc(&stats_of(js, self)->ran_when_full);
        run_job(js, self, &r);
        i++;
    }
    notify(js);
}

void ax_jobs_wait(ax_job_system *js, ax_job_counter *counter)
{
    worker *self;
    uint32_t idle = 0;

    AX_ASSERT(js != NULL && counter != NULL);
    if (ax_atomic_load_u32(&counter->pending, AX_MO_ACQUIRE) == 0) {
        return;
    }
    AX_ASSERT_MSG(!js->inline_mode,
                  "ax_jobs_wait: in inline mode every submitted job has finished; a job is "
                  "waiting on its own counter, or on jobs that were never submitted");
    if (js->inline_mode) {
        return; /* release builds: waiting could never end */
    }
    self = current_worker(js);
    while (ax_atomic_load_u32(&counter->pending, AX_MO_ACQUIRE) != 0) {
        uint64_t seen_epoch;

        if (try_run_one(js, self)) {
            idle = 0;
            continue;
        }
        if (idle < IDLE_SPINS) {
            idle++;
            cpu_relax();
            continue;
        }
        if (idle < IDLE_SPINS + IDLE_YIELDS) {
            idle++;
            ax_thread_yield();
            continue;
        }
        /* Sleep, bounded, until new work or a counter reaching zero changes the epoch. */
        seen_epoch = ax_atomic_load_u64(&js->epoch, AX_MO_SEQ_CST);
        if (ax_atomic_load_u32(&counter->pending, AX_MO_ACQUIRE) == 0) {
            break;
        }
        if (try_run_one(js, self)) {
            idle = 0;
            continue;
        }
        ax_mutex_lock(&js->sleep_lock);
        ax_atomic_fetch_add_u32(&js->sleepers, 1, AX_MO_SEQ_CST);
        if (ax_atomic_load_u64(&js->epoch, AX_MO_SEQ_CST) == seen_epoch &&
            ax_atomic_load_u32(&counter->pending, AX_MO_SEQ_CST) != 0) {
            stat_inc(&stats_of(js, self)->sleeps);
            (void)ax_cond_wait_timeout(&js->sleep_cond, &js->sleep_lock, AX_JOBS_WAIT_SLEEP_NS);
        }
        ax_atomic_fetch_add_u32(&js->sleepers, UINT32_MAX, AX_MO_SEQ_CST);
        ax_mutex_unlock(&js->sleep_lock);
        /* Look for work once more before sleeping again; skip the spinning. */
        idle = IDLE_SPINS + IDLE_YIELDS;
    }
}

void ax_jobs_wait_idle(ax_job_system *js)
{
    ax_jobs_wait(js, &js->outstanding);
}

static void add_stats(ax_jobs_stats *out, const stat_block *b)
{
    out->jobs_run += ax_atomic_load_u64(&b->jobs_run, AX_MO_RELAXED);
    out->steals += ax_atomic_load_u64(&b->steals, AX_MO_RELAXED);
    out->injected += ax_atomic_load_u64(&b->injected, AX_MO_RELAXED);
    out->ran_when_full += ax_atomic_load_u64(&b->ran_when_full, AX_MO_RELAXED);
    out->sleeps += ax_atomic_load_u64(&b->sleeps, AX_MO_RELAXED);
}

void ax_jobs_get_stats(const ax_job_system *js, ax_jobs_stats *out)
{
    uint32_t i;
    memset(out, 0, sizeof *out);
    add_stats(out, &js->stats);
    for (i = 0; i < js->worker_count; ++i) {
        add_stats(out, &js->workers[i].stats);
    }
}
