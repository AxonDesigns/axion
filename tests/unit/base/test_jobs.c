/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "ax_test.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_jobs.h"
#include "axion/base/ax_thread.h"

static ax_job_system *make_system(ax_jobs_mode mode, uint32_t workers)
{
    ax_job_system *js = NULL;
    ax_jobs_desc desc = ax_jobs_default_desc();
    desc.mode = mode;
    desc.worker_count = workers;
    if (ax_jobs_create(&desc, &js) != AX_OK) {
        return NULL;
    }
    return js;
}

#define REQUIRE_THREADS()              \
    do {                               \
        if (!ax_threads_supported()) { \
            AX_SKIP("needs threads");  \
        }                              \
    } while (0)

/* Waits until a worker has taken a job from the injection queue (taken_before + 1 jobs taken in
 * all), so the test thread's own ax_jobs_wait cannot run that job itself. */
static void wait_until_a_worker_takes_it(ax_job_system *js, uint64_t taken_before)
{
    for (;;) {
        ax_jobs_stats s;
        ax_jobs_get_stats(js, &s);
        if (s.injected > taken_before) {
            return;
        }
        ax_thread_yield();
    }
}

/* Spins for roughly `iterations` steps without being optimized away. */
static void busy_work(uint32_t iterations)
{
    volatile uint32_t sink = 0;
    uint32_t i;
    for (i = 0; i < iterations; ++i) {
        sink = sink + i;
    }
    (void)sink;
}

/* ---- Creation -------------------------------------------------------------------------------- */

AX_TEST(default_worker_count_leaves_the_dedicated_threads)
{
    ax_job_system *js;
    uint32_t cpus = ax_cpu_count();
    uint32_t expected =
        cpus > AX_JOBS_DEFAULT_RESERVED_THREADS ? cpus - AX_JOBS_DEFAULT_RESERVED_THREADS : 1;
    REQUIRE_THREADS();
    AX_CHECK(cpus >= 1);
    js = make_system(AX_JOBS_THREADED, 0);
    AX_REQUIRE(js != NULL);
    if (expected > AX_JOBS_MAX_WORKERS) {
        expected = AX_JOBS_MAX_WORKERS;
    }
    AX_CHECK_EQ_U(ax_jobs_worker_count(js), expected);
    AX_CHECK(!ax_jobs_is_inline(js));
    AX_CHECK_EQ_U(ax_jobs_current_worker(js), UINT32_MAX);
    ax_jobs_destroy(js);
}

AX_TEST(worker_count_override)
{
    ax_job_system *js;
    REQUIRE_THREADS();
    js = make_system(AX_JOBS_THREADED, 3);
    AX_REQUIRE(js != NULL);
    AX_CHECK_EQ_U(ax_jobs_worker_count(js), 3);
    ax_jobs_destroy(js);
}

/* ---- Running jobs ---------------------------------------------------------------------------- */

typedef struct sum_ctx {
    ax_atomic_u64 sum;
    ax_atomic_u32 count;
} sum_ctx;

typedef struct add_job_arg {
    sum_ctx *ctx;
    uint64_t value;
} add_job_arg;

static void add_job(void *arg)
{
    add_job_arg *a = (add_job_arg *)arg;
    ax_atomic_fetch_add_u64(&a->ctx->sum, a->value, AX_MO_RELAXED);
    ax_atomic_fetch_add_u32(&a->ctx->count, 1, AX_MO_RELAXED);
}

enum { MANY = 10000 };
static add_job_arg g_args[MANY];
static ax_job g_jobs[MANY];

static void fill_add_jobs(sum_ctx *ctx, uint32_t n)
{
    uint32_t i;
    ax_atomic_init_u64(&ctx->sum, 0);
    ax_atomic_init_u32(&ctx->count, 0);
    for (i = 0; i < n; ++i) {
        g_args[i].ctx = ctx;
        g_args[i].value = (uint64_t)i + 1;
        g_jobs[i].fn = add_job;
        g_jobs[i].arg = &g_args[i];
    }
}

AX_TEST(threaded_runs_every_job_and_the_counter_reaches_zero)
{
    sum_ctx ctx;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    ax_job_system *js;
    int round;
    REQUIRE_THREADS();
    js = make_system(AX_JOBS_THREADED, 4);
    AX_REQUIRE(js != NULL);
    /* The counter is reused across rounds. */
    for (round = 0; round < 3; ++round) {
        fill_add_jobs(&ctx, MANY);
        ax_jobs_submit(js, g_jobs, MANY, &done);
        ax_jobs_wait(js, &done);
        AX_CHECK_EQ_U(ax_atomic_load_u32(&done.pending, AX_MO_RELAXED), 0);
        AX_CHECK_EQ_U(ax_atomic_load_u32(&ctx.count, AX_MO_RELAXED), MANY);
        AX_CHECK_EQ_U(ax_atomic_load_u64(&ctx.sum, AX_MO_RELAXED), (uint64_t)MANY * (MANY + 1) / 2);
    }
    ax_jobs_destroy(js);
}

AX_TEST(wait_idle_covers_untracked_jobs)
{
    sum_ctx ctx;
    ax_job_system *js;
    REQUIRE_THREADS();
    js = make_system(AX_JOBS_THREADED, 2);
    AX_REQUIRE(js != NULL);
    fill_add_jobs(&ctx, 1000);
    ax_jobs_submit(js, g_jobs, 1000, NULL);
    ax_jobs_wait_idle(js);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&ctx.count, AX_MO_RELAXED), 1000);
    ax_jobs_destroy(js);
}

/* ---- Inline mode ----------------------------------------------------------------------------- */

typedef struct order_log {
    uint32_t entries[64];
    uint32_t count;
    ax_job_system *js;
} order_log;

typedef struct order_arg {
    order_log *log;
    uint32_t id;
    uint32_t children; /* jobs this one submits: ids id*10+1 .. id*10+children */
} order_arg;

static order_arg g_order_args[64];
static uint32_t g_order_arg_count;

static void order_job(void *arg)
{
    order_arg *a = (order_arg *)arg;
    a->log->entries[a->log->count++] = a->id;
    if (a->children > 0) {
        ax_job jobs[4];
        ax_job_counter done = AX_JOB_COUNTER_INIT;
        uint32_t i;
        for (i = 0; i < a->children; ++i) {
            order_arg *c = &g_order_args[g_order_arg_count++];
            c->log = a->log;
            c->id = a->id * 10 + i + 1;
            c->children = 0;
            jobs[i].fn = order_job;
            jobs[i].arg = c;
        }
        ax_jobs_submit(a->log->js, jobs, a->children, &done);
        ax_jobs_wait(a->log->js, &done);
    }
}

static void run_order_scene(ax_job_system *js, order_log *log)
{
    ax_job jobs[3];
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    uint32_t i;
    memset(log, 0, sizeof *log);
    log->js = js;
    g_order_arg_count = 0;
    for (i = 0; i < 3; ++i) {
        order_arg *a = &g_order_args[g_order_arg_count++];
        a->log = log;
        a->id = i + 1;
        a->children = i; /* 1 has none, 2 has one, 3 has two */
        jobs[i].fn = order_job;
        jobs[i].arg = a;
    }
    ax_jobs_submit(js, jobs, 3, &done);
    /* Inline: everything already ran, depth first. */
    AX_CHECK_EQ_U(ax_atomic_load_u32(&done.pending, AX_MO_RELAXED), 0);
    ax_jobs_wait(js, &done);
}

AX_TEST(inline_mode_runs_jobs_at_once_depth_first)
{
    static const uint32_t expected[] = {1, 2, 21, 3, 31, 32};
    order_log first;
    order_log second;
    ax_job_system *js = make_system(AX_JOBS_INLINE, 8);
    uint32_t i;
    AX_REQUIRE(js != NULL);
    AX_CHECK(ax_jobs_is_inline(js));
    AX_CHECK_EQ_U(ax_jobs_worker_count(js), 0);

    run_order_scene(js, &first);
    AX_REQUIRE(first.count == AX_COUNTOF(expected));
    for (i = 0; i < first.count; ++i) {
        AX_CHECK_EQ_U(first.entries[i], expected[i]);
    }
    /* Deterministic: the same order every time. */
    run_order_scene(js, &second);
    AX_CHECK(memcmp(&first.entries, &second.entries, sizeof first.entries) == 0);
    ax_jobs_destroy(js);
}

AX_DEATH_TEST(inline_wait_on_own_counter_aborts)
{
    ax_job_system *js = make_system(AX_JOBS_INLINE, 0);
    ax_job_counter c = AX_JOB_COUNTER_INIT;
    /* A counter with work that can never run in inline mode. */
    ax_atomic_store_u32(&c.pending, 1, AX_MO_RELAXED);
    ax_jobs_wait(js, &c);
}

/* ---- Nested waits and identical results ------------------------------------------------------ */

typedef struct tree_task {
    ax_job_system *js;
    uint32_t lo, hi, depth;
    uint64_t *out; /* out[i] for i in [lo, hi) */
    ax_atomic_u32 *max_depth;
    uint64_t sum; /* sum of out[lo..hi) */
} tree_task;

static uint64_t mix(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

static void tree_job(void *arg)
{
    tree_task *t = (tree_task *)arg;
    uint32_t seen = ax_atomic_load_u32(t->max_depth, AX_MO_RELAXED);
    while (t->depth > seen && !ax_atomic_compare_exchange_u32(t->max_depth, &seen, t->depth,
                                                              AX_MO_RELAXED, AX_MO_RELAXED)) {
    }
    if (t->hi - t->lo <= 64) {
        uint32_t i;
        t->sum = 0;
        for (i = t->lo; i < t->hi; ++i) {
            t->out[i] = mix((uint64_t)i * 31u + t->depth);
            t->sum += t->out[i] & 0xFFFF;
        }
        busy_work(200);
    } else {
        /* Nested wait: submit both halves and wait inside this job. */
        tree_task child[2];
        ax_job jobs[2];
        ax_job_counter done = AX_JOB_COUNTER_INIT;
        uint32_t mid = t->lo + (t->hi - t->lo) / 2;
        int k;
        for (k = 0; k < 2; ++k) {
            child[k] = *t;
            child[k].depth = t->depth + 1;
            child[k].lo = k == 0 ? t->lo : mid;
            child[k].hi = k == 0 ? mid : t->hi;
            jobs[k].fn = tree_job;
            jobs[k].arg = &child[k];
        }
        ax_jobs_submit(t->js, jobs, 2, &done);
        ax_jobs_wait(t->js, &done);
        t->sum = child[0].sum + child[1].sum;
    }
}

enum { TREE_N = 1 << 14 };
static uint64_t g_tree_threaded[TREE_N];
static uint64_t g_tree_inline[TREE_N];

static uint64_t run_tree(ax_job_system *js, uint64_t *out, uint32_t *out_depth)
{
    ax_atomic_u32 max_depth;
    tree_task root;
    ax_job job;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    ax_atomic_init_u32(&max_depth, 0);
    memset(&root, 0, sizeof root);
    root.js = js;
    root.lo = 0;
    root.hi = TREE_N;
    root.out = out;
    root.max_depth = &max_depth;
    job.fn = tree_job;
    job.arg = &root;
    ax_jobs_submit(js, &job, 1, &done);
    ax_jobs_wait(js, &done);
    *out_depth = ax_atomic_load_u32(&max_depth, AX_MO_RELAXED);
    return root.sum;
}

AX_TEST(nested_waits_and_identical_results_in_inline_mode)
{
    ax_job_system *threaded;
    ax_job_system *inl;
    uint32_t depth_threaded = 0;
    uint32_t depth_inline = 0;
    uint64_t sum_threaded;
    uint64_t sum_inline;
    REQUIRE_THREADS();
    threaded = make_system(AX_JOBS_THREADED, 4);
    inl = make_system(AX_JOBS_INLINE, 0);
    AX_REQUIRE(threaded != NULL && inl != NULL);

    sum_threaded = run_tree(threaded, g_tree_threaded, &depth_threaded);
    sum_inline = run_tree(inl, g_tree_inline, &depth_inline);

    /* 2^14 split down to 64 is 8 levels of nested waits. */
    AX_CHECK_EQ_U(depth_threaded, 8);
    AX_CHECK_EQ_U(depth_inline, 8);
    AX_CHECK_EQ_U(sum_threaded, sum_inline);
    AX_CHECK(memcmp(g_tree_threaded, g_tree_inline, sizeof g_tree_threaded) == 0);
    ax_jobs_destroy(threaded);
    ax_jobs_destroy(inl);
}

/* ---- Stealing -------------------------------------------------------------------------------- */

enum { UNBALANCED_JOBS = 2000 };

typedef struct unbalanced_ctx {
    ax_job_system *js;
    uint32_t root_worker;
    ax_atomic_u32 ran_on[AX_JOBS_MAX_WORKERS + 1]; /* last entry: not a worker */
} unbalanced_ctx;

static void leaf_job(void *arg)
{
    unbalanced_ctx *ctx = (unbalanced_ctx *)arg;
    uint32_t w = ax_jobs_current_worker(ctx->js);
    busy_work(2000);
    ax_atomic_fetch_add_u32(&ctx->ran_on[w == UINT32_MAX ? AX_JOBS_MAX_WORKERS : w], 1,
                            AX_MO_RELAXED);
}

static ax_job g_leaf_jobs[UNBALANCED_JOBS];

/* Every leaf goes into the root worker's own deque: the others only get work by stealing. */
static void root_job(void *arg)
{
    unbalanced_ctx *ctx = (unbalanced_ctx *)arg;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    uint32_t i;
    ctx->root_worker = ax_jobs_current_worker(ctx->js);
    for (i = 0; i < UNBALANCED_JOBS; ++i) {
        g_leaf_jobs[i].fn = leaf_job;
        g_leaf_jobs[i].arg = ctx;
    }
    ax_jobs_submit(ctx->js, g_leaf_jobs, UNBALANCED_JOBS, &done);
    ax_jobs_wait(ctx->js, &done);
}

AX_TEST(idle_workers_steal_from_an_unbalanced_deque)
{
    static unbalanced_ctx ctx;
    ax_jobs_desc desc = ax_jobs_default_desc();
    ax_job_system *js = NULL;
    ax_job root;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    ax_jobs_stats stats;
    uint32_t i;
    uint32_t total = 0;
    uint32_t other_workers = 0;
    REQUIRE_THREADS();
    desc.worker_count = 4;
    desc.deque_capacity = UNBALANCED_JOBS; /* never full: no job runs on the submitter */
    AX_REQUIRE(ax_jobs_create(&desc, &js) == AX_OK);
    memset(&ctx, 0, sizeof ctx);
    ctx.js = js;
    for (i = 0; i <= AX_JOBS_MAX_WORKERS; ++i) {
        ax_atomic_init_u32(&ctx.ran_on[i], 0);
    }
    root.fn = root_job;
    root.arg = &ctx;
    ax_jobs_submit(js, &root, 1, &done);
    wait_until_a_worker_takes_it(js, 0);
    ax_jobs_wait(js, &done);

    AX_REQUIRE(ctx.root_worker < 4);
    for (i = 0; i <= AX_JOBS_MAX_WORKERS; ++i) {
        uint32_t n = ax_atomic_load_u32(&ctx.ran_on[i], AX_MO_RELAXED);
        total += n;
        /* Workers other than the root's: the test thread's entry does not count. */
        if (i < AX_JOBS_MAX_WORKERS && i != ctx.root_worker && n > 0) {
            other_workers++;
        }
    }
    ax_jobs_get_stats(js, &stats);
    AX_CHECK_EQ_U(total, UNBALANCED_JOBS);
    AX_CHECK(other_workers >= 1);
    AX_CHECK(stats.steals > 0);
    AX_CHECK_EQ_U(stats.ran_when_full, 0);
    AX_CHECK_EQ_U(stats.jobs_run, UNBALANCED_JOBS + 1);
    ax_jobs_destroy(js);
}

/* ---- Full queues ----------------------------------------------------------------------------- */

static uint32_t g_fan_worker;

static void fan_out_job(void *arg)
{
    ax_job_system *js = (ax_job_system *)((void **)arg)[0];
    ax_job *jobs = (ax_job *)((void **)arg)[1];
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    g_fan_worker = ax_jobs_current_worker(js);
    ax_jobs_submit(js, jobs, 200, &done); /* into a deque of 2 */
    ax_jobs_wait(js, &done);
}

AX_TEST(full_queues_run_jobs_on_the_submitter)
{
    sum_ctx ctx;
    ax_jobs_desc desc = ax_jobs_default_desc();
    ax_job_system *js = NULL;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    ax_jobs_stats stats;
    void *fan_arg[2];
    ax_job fan;
    REQUIRE_THREADS();
    desc.worker_count = 1;
    desc.deque_capacity = 2;
    desc.inject_capacity = 2;
    AX_REQUIRE(ax_jobs_create(&desc, &js) == AX_OK);

    /* From a thread that is not a worker: the injection queue fills. */
    fill_add_jobs(&ctx, 200);
    ax_jobs_submit(js, g_jobs, 200, &done);
    ax_jobs_wait(js, &done);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&ctx.count, AX_MO_RELAXED), 200);
    ax_jobs_get_stats(js, &stats);
    AX_CHECK(stats.ran_when_full > 0);

    /* From a worker: its deque fills. */
    fill_add_jobs(&ctx, 200);
    fan_arg[0] = js;
    fan_arg[1] = g_jobs;
    fan.fn = fan_out_job;
    fan.arg = fan_arg;
    g_fan_worker = UINT32_MAX;
    ax_jobs_submit(js, &fan, 1, &done);
    wait_until_a_worker_takes_it(js, stats.injected);
    ax_jobs_wait(js, &done);
    AX_CHECK_EQ_U(g_fan_worker, 0);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&ctx.count, AX_MO_RELAXED), 200);
    {
        ax_jobs_stats after;
        ax_jobs_get_stats(js, &after);
        AX_CHECK(after.ran_when_full > stats.ran_when_full);
    }
    ax_jobs_destroy(js);
}

/* ---- Sleeping -------------------------------------------------------------------------------- */

static void slow_job(void *arg)
{
    uint64_t until = ax_time_monotonic_ns() + 30000000u; /* 30 ms */
    while (ax_time_monotonic_ns() < until) {
        ax_thread_yield();
    }
    ax_atomic_store_u32((ax_atomic_u32 *)arg, 1, AX_MO_RELAXED);
}

AX_TEST(a_waiter_with_nothing_to_run_sleeps_and_is_woken)
{
    ax_atomic_u32 finished;
    ax_job_system *js;
    ax_job job;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    ax_jobs_stats stats;
    REQUIRE_THREADS();
    js = make_system(AX_JOBS_THREADED, 1);
    AX_REQUIRE(js != NULL);
    ax_atomic_init_u32(&finished, 0);
    job.fn = slow_job;
    job.arg = &finished;
    ax_jobs_submit(js, &job, 1, &done);
    /* Give the worker time to take the job, so this thread has nothing to run. */
    while (ax_atomic_load_u32(&done.pending, AX_MO_ACQUIRE) != 0) {
        ax_jobs_stats s;
        ax_jobs_get_stats(js, &s);
        if (s.injected > 0) {
            break;
        }
        ax_thread_yield();
    }
    ax_jobs_wait(js, &done);
    AX_CHECK_EQ_U(ax_atomic_load_u32(&finished, AX_MO_RELAXED), 1);
    ax_jobs_get_stats(js, &stats);
    AX_CHECK(stats.sleeps > 0);
    ax_jobs_destroy(js);
}

/* ---- Stress: several submitting threads (run under TSan in CI) ------------------------------- */

enum { SUBMITTERS = 3, PER_SUBMITTER = 6000, CHUNK = 100 };

typedef struct submitter_ctx {
    ax_job_system *js;
    sum_ctx sums;
    add_job_arg args[PER_SUBMITTER];
    ax_job jobs[PER_SUBMITTER];
} submitter_ctx;

static submitter_ctx g_submitters[SUBMITTERS];

static int submitter_main(void *arg)
{
    submitter_ctx *s = (submitter_ctx *)arg;
    ax_job_counter done = AX_JOB_COUNTER_INIT;
    uint32_t i;
    for (i = 0; i < PER_SUBMITTER; ++i) {
        s->args[i].ctx = &s->sums;
        s->args[i].value = (uint64_t)i + 1;
        s->jobs[i].fn = add_job;
        s->jobs[i].arg = &s->args[i];
    }
    for (i = 0; i < PER_SUBMITTER; i += CHUNK) {
        ax_jobs_submit(s->js, &s->jobs[i], CHUNK, &done);
        if ((i / CHUNK) % 4 == 3) {
            ax_jobs_wait(s->js, &done);
        }
    }
    ax_jobs_wait(s->js, &done);
    return 0;
}

AX_TEST(stress_several_submitters)
{
    ax_thread threads[SUBMITTERS];
    ax_job_system *js;
    int round;
    uint32_t i;
    REQUIRE_THREADS();
    js = make_system(AX_JOBS_THREADED, 4);
    AX_REQUIRE(js != NULL);
    for (round = 0; round < 3; ++round) {
        for (i = 0; i < SUBMITTERS; ++i) {
            g_submitters[i].js = js;
            ax_atomic_init_u64(&g_submitters[i].sums.sum, 0);
            ax_atomic_init_u32(&g_submitters[i].sums.count, 0);
            AX_REQUIRE(ax_thread_create(&threads[i], submitter_main, &g_submitters[i]) == AX_OK);
        }
        for (i = 0; i < SUBMITTERS; ++i) {
            ax_thread_join(&threads[i]);
        }
        for (i = 0; i < SUBMITTERS; ++i) {
            AX_CHECK_EQ_U(ax_atomic_load_u32(&g_submitters[i].sums.count, AX_MO_RELAXED),
                          PER_SUBMITTER);
            AX_CHECK_EQ_U(ax_atomic_load_u64(&g_submitters[i].sums.sum, AX_MO_RELAXED),
                          (uint64_t)PER_SUBMITTER * (PER_SUBMITTER + 1) / 2);
        }
    }
    ax_jobs_destroy(js);
}
