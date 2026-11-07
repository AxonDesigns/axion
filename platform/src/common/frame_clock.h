/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Frame clock math: from presentation reports, predict when the next frame will reach the screen
 * and measure the display's refresh interval. Pure and single-threaded (it belongs to a surface's
 * presenter); the windowing backend feeds it, tests feed it synthetic reports.
 *
 * Times are nanoseconds on ax_time_monotonic_ns's clock.
 */
#ifndef AX_PLATFORM_FRAME_CLOCK_H
#define AX_PLATFORM_FRAME_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct ax_frame_clock {
    uint64_t nominal_refresh_ns;  /* from the system when known, else a default */
    uint64_t measured_refresh_ns; /* smoothed measurement; 0 until two frames were presented */
    uint64_t last_present_ns;
    uint64_t last_msc;       /* media stream counter (vblank count) of the last present */
    uint64_t last_target_ns; /* target of the last committed frame, 0 if none */
    bool have_last;          /* last_present_ns is valid (anchors predictions) */
    bool have_msc;
    bool interval_valid; /* the next present may be measured against the last one */

    /* statistics, cleared by ax_frame_clock_reset_stats */
    uint64_t presented;
    uint64_t discarded;
    uint64_t missed_vblanks; /* vblanks skipped between consecutive presented frames */
    uint64_t interval_sum_ns;
    uint64_t interval_count;
    uint64_t latency_sum_ns;
    uint64_t latency_count;
    uint64_t latency_max_ns;
} ax_frame_clock;

#define AX_FRAME_CLOCK_DEFAULT_REFRESH_NS 16666667u /* 60 Hz until measured */

void ax_frame_clock_init(ax_frame_clock *clock, uint64_t default_refresh_ns);

/*
 * A frame reached the screen at present_ns.
 *   refresh_ns   the system's nominal refresh interval for it, or 0 if unknown/variable;
 *   msc          the vblank counter at that present, valid when msc_valid;
 *   configure_ns when the configure this frame acknowledged arrived, or 0.
 * Missed vblanks assume the presenter submits a frame for every refresh, which is what an
 * animating surface does; an idle surface's gaps are not misses (reset the stats when idling).
 */
void ax_frame_clock_presented(ax_frame_clock *clock, uint64_t present_ns, uint64_t refresh_ns,
                              uint64_t msc, bool msc_valid, uint64_t configure_ns);

/* A submitted frame was never shown (replaced before it reached the screen). */
void ax_frame_clock_discarded(ax_frame_clock *clock);

/* Best known refresh interval: measured, else nominal. */
uint64_t ax_frame_clock_refresh(const ax_frame_clock *clock);

/* Predicted present time of the next frame started at now_ns: the first refresh boundary after
 * now_ns, counted from the last presented frame (now_ns + one refresh before any report). */
uint64_t ax_frame_clock_predict(const ax_frame_clock *clock, uint64_t now_ns);

/*
 * Target present time for a frame about to be drawn at now_ns: the prediction, but never before
 * the previous committed frame's target. Animations sample this, so it never goes backwards, even
 * when the first estimates (made before any presentation report) were too far ahead. Two commits
 * before the same refresh share its target: the later one replaces the earlier on screen.
 */
uint64_t ax_frame_clock_target(const ax_frame_clock *clock, uint64_t now_ns);

/* Records that a frame with this target was committed. */
void ax_frame_clock_committed(ax_frame_clock *clock, uint64_t target_ns);

void ax_frame_clock_reset_stats(ax_frame_clock *clock);

#endif /* AX_PLATFORM_FRAME_CLOCK_H */
