/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: the job system. A fixed pool of worker threads with work-stealing deques and
 * counter-based waits; no fibers, so it ports to WASM unchanged.
 *
 *     ax_job_system *js;
 *     ax_jobs_desc desc = ax_jobs_default_desc();
 *     if (ax_jobs_create(&desc, &js) != AX_OK) { ... }
 *
 *     ax_job jobs[64];                           // fn(arg) for each
 *     ax_job_counter done = AX_JOB_COUNTER_INIT;
 *     ax_jobs_submit(js, jobs, 64, &done);        // done.pending += 64
 *     ax_jobs_wait(js, &done);                    // returns when every job has finished
 *
 * Workers. ax_jobs_desc.worker_count threads, or by default the logical processors minus the
 * threads AXION dedicates (main, app, scene and render: reserved_threads), at least one. Each
 * worker owns a Chase-Lev deque of fixed capacity: it pushes and pops at the bottom, and idle
 * threads steal from the top. Threads that are not workers (the scene thread, tests) submit to a
 * shared injection queue of fixed capacity. Nothing is allocated after ax_jobs_create: when a
 * deque or the injection queue is full, the submitting thread runs the job itself, at once.
 *
 * Waiting. ax_jobs_wait never just sleeps while there is work: the waiting thread runs pending
 * jobs (its own deque if it is a worker, then the injection queue, then steals) until the counter
 * reaches zero. Jobs may submit jobs and wait for them (nested waits). When there is nothing to
 * run, the waiter backs off (spins, then yields), then sleeps for at most AX_JOBS_WAIT_SLEEP_NS
 * at a time; new work and counters reaching zero wake it earlier. A waiter may run other
 * pending jobs of the priorities it is allowed (below), not only its own, so a deep chain of
 * nested waits uses stack on that thread.
 *
 * Priorities (decision 80). Every job is either a frame job (ax_jobs_submit: work a frame waits
 * for, such as tiles or relayout boundaries) or a background job (ax_jobs_submit_background:
 * decoding an image, rasterizing glyphs). Each priority has its own deques and injection queue.
 *   - Idle workers run frame jobs first, then background jobs.
 *   - A frame context runs only frame jobs while it waits, so a frame never picks up a long
 *     background job. Frame contexts are a thread that is not a worker (the scene and render
 *     threads, tests) outside any job, and any thread inside a frame job.
 *   - Inside a background job a thread runs both while it waits, frame jobs first (a thread that
 *     is not a worker gets one only from ax_jobs_wait_idle, or in inline mode).
 *   - A counter remembers the priority of its jobs (one priority per counter until its wait
 *     returns). Frame work never waits for background work: waiting on a background counter from
 *     a frame context asserts in debug builds, because with every worker inside frame jobs
 *     nobody would run it (a deadlock). Release builds let such a waiter run background jobs too,
 *     trading the frame for the hang.
 *   In inline mode both priorities run at once, when submitted.
 *
 * Inline mode. With AX_JOBS_INLINE, or in builds without threads (ax_threads_supported() is
 * false), there are no workers: ax_jobs_submit runs each job at once, in submission order and
 * depth-first, and ax_jobs_wait finds the counter already at zero. The API is the same, and the
 * order is deterministic: this is the mode of the single-threaded web build, of hosts that call
 * ax_host_frame and of deterministic tests.
 *
 * Memory order. Every write a job makes before it finishes is visible to the thread that returns
 * from ax_jobs_wait on its counter.
 *
 * Thread: any, for every function except ax_jobs_create and ax_jobs_destroy (one thread, with no
 * other call in progress).
 */
#ifndef AX_BASE_JOBS_H
#define AX_BASE_JOBS_H

#include <stdbool.h>
#include <stdint.h>

#include "../ax_allocator.h"
#include "../ax_types.h"
#include "ax_atomic.h"
#include "ax_compiler.h"

AX_EXTERN_C_BEGIN

typedef void (*ax_job_fn)(void *arg);

typedef struct ax_job {
    ax_job_fn fn;
    void *arg;
} ax_job;

/* Job priorities (see "Priorities" above). */
typedef enum ax_job_priority {
    AX_JOB_FRAME = 0,     /* work a frame waits for */
    AX_JOB_BACKGROUND = 1 /* long work no frame waits for */
} ax_job_priority;

/* Counts unfinished jobs. Zero-initialize it (AX_JOB_COUNTER_INIT) before its first submit; it
 * may be reused once ax_jobs_wait has returned on it, with either priority. It must outlive its
 * jobs. `priority` is the ax_job_priority of its jobs, set by the submits (private). */
typedef struct ax_job_counter {
    ax_atomic_u32 pending;
    ax_atomic_u32 priority;
} ax_job_counter;

#define AX_JOB_COUNTER_INIT {AX_ATOMIC_INIT(0), AX_ATOMIC_INIT(0)}

typedef enum ax_jobs_mode {
    AX_JOBS_THREADED = 0, /* worker threads (inline anyway without thread support) */
    AX_JOBS_INLINE = 1    /* no workers: submit runs each job at once */
} ax_jobs_mode;

/* Defaults, as ax_jobs_default_desc() sets them. */
#define AX_JOBS_DEFAULT_RESERVED_THREADS 4u    /* main, app, scene and render */
#define AX_JOBS_DEFAULT_DEQUE_CAPACITY   1024u /* jobs per worker deque */
#define AX_JOBS_DEFAULT_INJECT_CAPACITY  1024u /* jobs in the shared injection queue */
#define AX_JOBS_MAX_WORKERS              64u

/* Longest single sleep of a waiter with nothing to run (it is woken earlier by new work and by
 * counters reaching zero; the bound only limits the cost of a missed wakeup). */
#define AX_JOBS_WAIT_SLEEP_NS 1000000u

typedef struct ax_jobs_desc {
    const ax_allocator *allocator; /* NULL: ax_default_allocator(); copied */
    ax_jobs_mode mode;
    uint32_t worker_count;     /* 0: ax_cpu_count() - reserved_threads, at least 1 */
    uint32_t reserved_threads; /* only used when worker_count is 0 */
    uint32_t deque_capacity;   /* rounded up to a power of two */
    uint32_t inject_capacity;  /* rounded up to a power of two */
} ax_jobs_desc;

typedef struct ax_job_system ax_job_system;

/* Counters for tests and profiling. Read while jobs run, they are approximate. */
typedef struct ax_jobs_stats {
    uint64_t jobs_run;      /* jobs finished, inline ones included */
    uint64_t steals;        /* jobs taken from another worker's deque */
    uint64_t injected;      /* jobs taken from the injection queue */
    uint64_t ran_when_full; /* jobs the submitter ran because a queue was full */
    uint64_t sleeps;        /* times a worker or a waiter went to sleep */
    uint64_t background_run; /* of jobs_run, background jobs */
} ax_jobs_stats;

AX_API ax_jobs_desc ax_jobs_default_desc(void);

/* Starts the workers. Errors: AX_ERR_OUT_OF_MEMORY (memory or threads), with ax_last_error. */
AX_API ax_result ax_jobs_create(const ax_jobs_desc *desc, ax_job_system **out_js);

/* Stops and joins the workers. Every counter must have been waited for: no job may be pending. */
AX_API void ax_jobs_destroy(ax_job_system *js);

/* Number of worker threads; 0 in inline mode. */
AX_API uint32_t ax_jobs_worker_count(const ax_job_system *js);

/* True when submit runs jobs at once (AX_JOBS_INLINE, or no thread support). */
AX_API bool ax_jobs_is_inline(const ax_job_system *js);

/* Index of the calling worker thread of js (0 .. worker_count-1), or UINT32_MAX when the caller
 * is not one of its workers. */
AX_API uint32_t ax_jobs_current_worker(const ax_job_system *js);

/*
 * Queues count frame jobs and adds count to counter->pending first (counter may be NULL: nothing
 * tracks those jobs, and they must still finish before ax_jobs_destroy; see ax_jobs_wait_idle).
 * From a worker, jobs go to its own deque; from any other thread, to the injection queue.
 * A counter holds jobs of one priority: submitting frame jobs to a counter that still has
 * background jobs pending asserts.
 */
AX_API void ax_jobs_submit(ax_job_system *js, const ax_job *jobs, uint32_t count,
                           ax_job_counter *counter);

/* As ax_jobs_submit, with background priority. */
AX_API void ax_jobs_submit_background(ax_job_system *js, const ax_job *jobs, uint32_t count,
                                      ax_job_counter *counter);

/* Runs pending jobs (of the priorities the caller may run) until counter->pending is zero.
 * Waiting on a background counter from a frame context asserts (see "Priorities"). */
AX_API void ax_jobs_wait(ax_job_system *js, ax_job_counter *counter);

/* Runs pending jobs of both priorities until every submitted job has run (for untracked jobs, and
 * teardown; not for frame work). A job's own counter reaches zero just after the job is counted
 * here, so it may still read nonzero for a moment. */
AX_API void ax_jobs_wait_idle(ax_job_system *js);

AX_API void ax_jobs_get_stats(const ax_job_system *js, ax_jobs_stats *out);

AX_EXTERN_C_END

#endif /* AX_BASE_JOBS_H */
