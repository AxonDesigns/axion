/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * One render-thread frame with the size handshake (ax_render_frame.h, decisions 54 and 57).
 */
#include <string.h>

#include <axion/base/ax_assert.h>
#include <axion/base/ax_thread.h>
#include <axion/render/ax_render_frame.h>

void ax_render_frame_init(ax_render_frame *rf, const ax_render_frame_desc *desc)
{
    AX_ASSERT(desc != NULL && desc->struct_size >= sizeof(ax_render_frame_desc));
    memset(rf, 0, sizeof *rf);
    rf->desc = *desc;
    rf->stats.struct_size = sizeof rf->stats;
}

uint64_t ax_render_frame_default_max_wait(uint64_t refresh_ns)
{
    /* Half a refresh, so a late scene costs at most that frame; within 2-8 ms. */
    uint64_t w = refresh_ns / 2u;
    if (w < 2000000u) {
        w = 2000000u;
    }
    if (w > 8000000u) {
        w = 8000000u;
    }
    return w;
}

static void request(ax_render_frame *rf, const ax_frame_target *t, uint64_t time_ns, bool resize)
{
    ax_scene_request q;
    memset(&q, 0, sizeof q);
    q.logical_width = t->logical_width;
    q.logical_height = t->logical_height;
    q.scale = t->scale;
    q.frame_time_ns = time_ns;
    q.refresh_interval_ns = t->refresh_interval_ns;
    q.resize = resize;
    rf->desc.request(rf->desc.request_ctx, &q);
}

static bool window_hidden(ax_window w)
{
    ax_window_info info;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    return ax_window_get_info(w, &info) == AX_OK && info.occluded;
}

static void add_counts(ax_render_counts *sum, const ax_render_counts *c)
{
    sum->unknown_items += c->unknown_items;
    sum->skipped_items += c->skipped_items;
    sum->skipped_layers += c->skipped_layers;
    sum->neutralized += c->neutralized;
    sum->approximated += c->approximated;
    sum->invalid_items += c->invalid_items;
    sum->instances += c->instances;
    sum->clips_approximated += c->clips_approximated;
    sum->linear_layers += c->linear_layers;
    sum->linear_layers_unsupported += c->linear_layers_unsupported;
}

ax_result ax_render_frame_step(ax_render_frame *rf, bool animating, uint64_t timeout_ns,
                               ax_frame_result *out_result)
{
    ax_wake_reason reason = AX_WAKE_TIMEOUT;
    ax_result r = rf->desc.vtbl->wait_frame(rf->desc.renderer, rf->desc.surface, animating,
                                            timeout_ns, &reason);
    if (r != AX_OK) {
        return r;
    }
    return ax_render_frame_present(rf, reason, animating, out_result);
}

ax_result ax_render_frame_present(ax_render_frame *rf, ax_wake_reason reason, bool animating,
                                  ax_frame_result *out_result)
{
    const ax_renderer_vtbl *vt = rf->desc.vtbl;
    const ax_render_ir *drawn = NULL;
    ax_frame_result res;
    ax_frame_info info;
    uint64_t wait_start = 0, wait_end = 0, deadline = 0;
    bool waited = false, accept_any = false;
    ax_result r;

    for (;;) {
        const ax_render_ir *ir = ax_ir_mailbox_acquire(rf->desc.mailbox);
        memset(&res, 0, sizeof res);
        res.struct_size = sizeof res;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        info.flags = accept_any ? AX_FRAME_ACCEPT_ANY_SIZE : 0u;
        /* At rest (decision 95): a timeout, or a frame callback with nothing new to show, draws
         * only to answer a configure or a commit request, so no commit asks for another
         * callback and frames stop. */
        info.flags |=
            (!animating && (reason == AX_WAKE_TIMEOUT || (reason == AX_WAKE_FRAME && ir != NULL &&
                                                          ir->sequence == rf->last_sequence)))
                ? AX_FRAME_ONLY_IF_REQUIRED
                : 0u;
        info.result = &res;
        if (waited) {
            wait_end = ax_time_monotonic_ns(); /* the wait ends where this attempt starts */
        }
        r = vt->render(rf->desc.renderer, rf->desc.surface, ir, info);
        drawn = ir;
        if (out_result != NULL) {
            *out_result = res;
        }
        if (r != AX_OK) {
            return r;
        }
        if (res.status == AX_FRAME_CONTENT_MISMATCH) {
            const uint64_t now = ax_time_monotonic_ns();
            request(rf, &res.target, res.target.predicted_present_ns, true);
            if (!waited) {
                uint64_t bound = rf->desc.max_wait_ns;
                if (bound == 0) {
                    bound = ax_render_frame_default_max_wait(res.target.refresh_interval_ns);
                }
                waited = true;
                wait_start = now;
                deadline = now + bound;
            }
            if (window_hidden(rf->desc.window)) {
                rf->stats.hidden_acks++; /* decision 40: a hidden window still acknowledges */
                accept_any = true;
                continue;
            }
            if (now >= deadline) {
                rf->stats.forced_acks++;
                accept_any = true;
                continue;
            }
            (void)ax_ir_mailbox_wait(rf->desc.mailbox, ir != NULL ? ir->sequence : 0,
                                     deadline - now);
            continue;
        }
        break;
    }

    if (waited) {
        /* Up to the start of the frame that acknowledged: not its own rendering and present. */
        const uint64_t dt = wait_end - wait_start;
        rf->stats.wait_ns_total += dt;
        if (dt > rf->stats.wait_ns_max) {
            rf->stats.wait_ns_max = dt;
        }
        if (!accept_any) {
            rf->stats.scene_late++;
        }
    }
    if (res.status == AX_FRAME_PRESENTED || res.status == AX_FRAME_CLEARED) {
        if (res.status == AX_FRAME_PRESENTED) {
            rf->stats.presented++;
            rf->last_sequence = drawn != NULL ? drawn->sequence : 0;
        } else {
            rf->stats.cleared++;
        }
        add_counts(&rf->stats.counts, &res.counts);
        /* The scene records frame N+1 while this one is shown. */
        request(rf, &res.target, res.target.predicted_present_ns + res.target.refresh_interval_ns,
                false);
        rf->requested_first = true;
    } else if (!rf->requested_first && res.target.ready) {
        request(rf, &res.target, res.target.predicted_present_ns, false);
        rf->requested_first = true;
    }
    return AX_OK;
}

void ax_render_frame_get_stats(ax_render_frame *rf, ax_render_frame_stats *out, bool reset)
{
    uint32_t size;
    AX_ASSERT(out != NULL && out->struct_size >= sizeof(ax_render_frame_stats));
    size = out->struct_size;
    *out = rf->stats;
    out->struct_size = size;
    if (reset) {
        memset(&rf->stats, 0, sizeof rf->stats);
        rf->stats.struct_size = sizeof rf->stats;
    }
}
