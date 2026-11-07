/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 */
#include "frame_clock.h"

/* Per-vblank intervals outside this range are noise (clock jumps, suspend), not refresh. */
#define AX_MIN_REFRESH_NS 1000000u    /* 1000 Hz */
#define AX_MAX_REFRESH_NS 1000000000u /* 1 Hz */

void ax_frame_clock_init(ax_frame_clock *clock, uint64_t default_refresh_ns)
{
    *clock = (ax_frame_clock){0};
    clock->nominal_refresh_ns =
        default_refresh_ns != 0 ? default_refresh_ns : AX_FRAME_CLOCK_DEFAULT_REFRESH_NS;
}

uint64_t ax_frame_clock_refresh(const ax_frame_clock *clock)
{
    return clock->measured_refresh_ns != 0 ? clock->measured_refresh_ns : clock->nominal_refresh_ns;
}

void ax_frame_clock_presented(ax_frame_clock *clock, uint64_t present_ns, uint64_t refresh_ns,
                              uint64_t msc, bool msc_valid, uint64_t configure_ns)
{
    if (refresh_ns >= AX_MIN_REFRESH_NS && refresh_ns <= AX_MAX_REFRESH_NS) {
        clock->nominal_refresh_ns = refresh_ns;
    }

    if (clock->interval_valid && present_ns > clock->last_present_ns) {
        uint64_t dt = present_ns - clock->last_present_ns;
        uint64_t vblanks;

        if (msc_valid && clock->have_msc && msc > clock->last_msc) {
            vblanks = msc - clock->last_msc;
        } else {
            /* No counter: count refresh cycles in dt, rounding to the nearest. */
            uint64_t r = ax_frame_clock_refresh(clock);
            vblanks = (dt + r / 2) / r;
            if (vblanks == 0) {
                vblanks = 1;
            }
        }
        clock->missed_vblanks += vblanks - 1;
        clock->interval_sum_ns += dt;
        clock->interval_count++;

        {
            uint64_t per_vblank = dt / vblanks;
            if (per_vblank >= AX_MIN_REFRESH_NS && per_vblank <= AX_MAX_REFRESH_NS) {
                /* Exponential moving average, weight 1/8 for the new sample. */
                clock->measured_refresh_ns =
                    clock->measured_refresh_ns == 0
                        ? per_vblank
                        : (clock->measured_refresh_ns * 7 + per_vblank) / 8;
            }
        }
    }

    if (configure_ns != 0 && present_ns >= configure_ns) {
        uint64_t latency = present_ns - configure_ns;
        clock->latency_sum_ns += latency;
        clock->latency_count++;
        if (latency > clock->latency_max_ns) {
            clock->latency_max_ns = latency;
        }
    }

    clock->presented++;
    clock->last_present_ns = present_ns;
    clock->have_last = true;
    clock->interval_valid = true;
    clock->last_msc = msc;
    clock->have_msc = msc_valid;
}

void ax_frame_clock_discarded(ax_frame_clock *clock)
{
    clock->discarded++;
}

uint64_t ax_frame_clock_predict(const ax_frame_clock *clock, uint64_t now_ns)
{
    uint64_t r = ax_frame_clock_refresh(clock);
    uint64_t k;
    if (!clock->have_last || now_ns < clock->last_present_ns) {
        return now_ns + r;
    }
    /* The first boundary strictly after now. */
    k = (now_ns - clock->last_present_ns) / r + 1;
    return clock->last_present_ns + k * r;
}

void ax_frame_clock_reset_stats(ax_frame_clock *clock)
{
    clock->presented = 0;
    clock->discarded = 0;
    clock->missed_vblanks = 0;
    clock->interval_sum_ns = 0;
    clock->interval_count = 0;
    clock->latency_sum_ns = 0;
    clock->latency_count = 0;
    clock->latency_max_ns = 0;
    /* A gap across an idle period is neither a miss nor a refresh sample; predictions keep
     * their anchor. */
    clock->interval_valid = false;
}
