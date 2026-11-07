/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ax_test.h"
#include "common/frame_clock.h"

#define MS    1000000ull
#define HZ60  16666667ull
#define HZ144 6944444ull

AX_TEST(predicts_one_refresh_ahead_before_any_report)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, 0);
    AX_CHECK_EQ_U(ax_frame_clock_refresh(&c), AX_FRAME_CLOCK_DEFAULT_REFRESH_NS);
    AX_CHECK_EQ_U(ax_frame_clock_predict(&c, 1000 * MS), 1000 * MS + HZ60);
}

AX_TEST(measures_refresh_from_vblank_counter)
{
    ax_frame_clock c;
    uint64_t t = 5000 * MS;
    uint64_t msc = 100;
    int i;
    ax_frame_clock_init(&c, 0);
    for (i = 0; i < 50; ++i) {
        ax_frame_clock_presented(&c, t, 0, msc, true, 0);
        t += HZ144;
        msc += 1;
    }
    /* The measured interval converges to 144 Hz, within 1 us. */
    AX_CHECK(ax_frame_clock_refresh(&c) > HZ144 - 1000 &&
             ax_frame_clock_refresh(&c) < HZ144 + 1000);
    AX_CHECK_EQ_U(c.missed_vblanks, 0);
    AX_CHECK_EQ_U(c.presented, 50);
}

AX_TEST(counts_missed_vblanks)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, 1000 * MS, HZ60, 10, true, 0);
    ax_frame_clock_presented(&c, 1000 * MS + HZ60, HZ60, 11, true, 0);
    ax_frame_clock_presented(&c, 1000 * MS + 4 * HZ60, HZ60, 14, true, 0); /* skipped 2 */
    AX_CHECK_EQ_U(c.missed_vblanks, 2);
    /* Per-vblank measurement is unaffected by the gap. */
    AX_CHECK(ax_frame_clock_refresh(&c) > HZ60 - 1000 && ax_frame_clock_refresh(&c) < HZ60 + 1000);
}

AX_TEST(counts_missed_vblanks_without_counter)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, 1000 * MS, 0, 0, false, 0);
    ax_frame_clock_presented(&c, 1000 * MS + HZ60, 0, 0, false, 0);
    ax_frame_clock_presented(&c, 1000 * MS + 3 * HZ60 + 2 * MS, 0, 0, false, 0); /* ~2 vblanks */
    AX_CHECK_EQ_U(c.missed_vblanks, 1);
}

AX_TEST(predicts_next_boundary_after_now)
{
    ax_frame_clock c;
    uint64_t last = 2000 * MS;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, last, HZ60, 1, true, 0);
    AX_CHECK_EQ_U(ax_frame_clock_predict(&c, last + 1), last + HZ60);
    AX_CHECK_EQ_U(ax_frame_clock_predict(&c, last + HZ60), last + 2 * HZ60); /* strictly after */
    AX_CHECK_EQ_U(ax_frame_clock_predict(&c, last + 5 * HZ60 + 7), last + 6 * HZ60);
}

AX_TEST(latency_and_intervals)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, 100 * MS, HZ60, 1, true, 90 * MS);
    ax_frame_clock_presented(&c, 100 * MS + HZ60, HZ60, 2, true, 0);
    ax_frame_clock_presented(&c, 100 * MS + 2 * HZ60, HZ60, 3, true, 100 * MS);
    AX_CHECK_EQ_U(c.latency_count, 2);
    AX_CHECK_EQ_U(c.latency_max_ns, 2 * HZ60);
    AX_CHECK_EQ_U(c.latency_sum_ns, 10 * MS + 2 * HZ60);
    AX_CHECK_EQ_U(c.interval_count, 2);
    AX_CHECK_EQ_U(c.interval_sum_ns, 2 * HZ60);
}

AX_TEST(reset_ignores_idle_gap_but_keeps_prediction)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, 100 * MS, HZ60, 1, true, 0);
    ax_frame_clock_reset_stats(&c);
    AX_CHECK_EQ_U(ax_frame_clock_predict(&c, 100 * MS + 1), 100 * MS + HZ60);
    ax_frame_clock_presented(&c, 5000 * MS, HZ60, 300, true, 0); /* after a long idle period */
    AX_CHECK_EQ_U(c.missed_vblanks, 0);
    AX_CHECK_EQ_U(c.interval_count, 0);
    AX_CHECK_EQ_U(c.presented, 1);
}

AX_TEST(discarded_frames_are_counted)
{
    ax_frame_clock c;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_discarded(&c);
    ax_frame_clock_discarded(&c);
    AX_CHECK_EQ_U(c.discarded, 2);
}

AX_TEST(targets_never_go_backwards_between_committed_frames)
{
    ax_frame_clock c;
    uint64_t t1;
    uint64_t t2;
    uint64_t t3;
    ax_frame_clock_init(&c, HZ60);

    /* Before any report: a guess one refresh ahead. */
    t1 = ax_frame_clock_target(&c, 1000 * MS);
    ax_frame_clock_committed(&c, t1);
    /* The first report (for a frame shown at 990 ms) puts the vblanks earlier than guessed: the
     * plain prediction for the next frame lands before t1, but the target must not. */
    ax_frame_clock_presented(&c, 990 * MS, HZ60, 50, true, 0);
    AX_CHECK(ax_frame_clock_predict(&c, 1001 * MS) < t1);
    t2 = ax_frame_clock_target(&c, 1001 * MS);
    AX_CHECK_EQ_U(t2, t1); /* held, not pushed further ahead */
    ax_frame_clock_committed(&c, t2);

    /* After an idle second the prediction is far ahead and wins. */
    t3 = ax_frame_clock_target(&c, 2000 * MS);
    AX_CHECK(t3 > 2000 * MS && t3 <= 2000 * MS + HZ60);
}

AX_TEST(extra_commits_do_not_run_the_clock_ahead)
{
    ax_frame_clock c;
    uint64_t now = 3000 * MS;
    uint64_t first;
    int i;
    ax_frame_clock_init(&c, HZ60);
    ax_frame_clock_presented(&c, now - 5 * MS, HZ60, 7, true, 0);
    first = ax_frame_clock_target(&c, now);
    /* Ten commits before one refresh (a burst of configures): they all target the same refresh. */
    for (i = 0; i < 10; ++i) {
        uint64_t t = ax_frame_clock_target(&c, now + (uint64_t)i * 100000u);
        AX_CHECK_EQ_U(t, first);
        ax_frame_clock_committed(&c, t);
    }
    /* A refresh later the target is one refresh later, not ten. */
    AX_CHECK_EQ_U(ax_frame_clock_target(&c, first + 1), first + HZ60);
}
