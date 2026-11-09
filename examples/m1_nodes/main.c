/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes: a list of cards in flex (decision 90) that reorder, grow and change color through
 * transactions, every change animated implicitly by the scene (decision 94), drawn through the M1
 * pipeline.
 *
 *   main thread    runs the platform event pump (ax_app_run) and nothing else
 *   app thread     creates the world and the runtime, builds the cards with a transaction, reads
 *                  window events from the world (ax_world_wait_event), publishes the hit regions,
 *                  commits one change every 1.2 s (m1_cards_step), and with --block-ms blocks
 *                  itself on purpose
 *   scene thread   the runtime's: applies transactions at each vsync, lays out, animates, records
 *                  the Render IR, publishes the mirror
 *   render thread  the runtime's: presents with ax_render_wgpu, or with ax_render_soft into
 *                  shared-memory buffers (--renderer=soft, or when no GPU is usable); at rest, when
 *                  nothing animates, it presents nothing (decision 95)
 *
 * Motion is the scene's: a transaction only sets targets. Blocking the app thread therefore must
 * not show in a running animation: run with --block-ms=500 and watch the cards keep moving.
 *
 * Usage: m1_nodes [--decorations=native|none] [--stats] [--duration=SECONDS] [--width=W]
 *                 [--height=H] [--block-ms=MS] [--block-every=SECONDS]
 *                 [--adapter=default|software] [--max-wait-ms=MS] [--renderer=gpu|soft]
 *                 [--pacing=default|compositor|driver]
 *                 [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate]
 *                 [--blend-compare[=default|1|2]] [--still]
 *
 * Accessibility (decision 108): assistive technologies (Orca, Accerciser) read the cards through
 * AccessKit's AT-SPI adapter where the build has it (AX_ACCESSKIT): each card is an article named
 * by its title, and its default action ("click" in Accerciser) grows or shrinks it. --still stops
 * the automatic changes, so a screen reader can be followed without the cards moving under it.
 *
 * --blend-compare shows issue #4's comparison scene (m1_blend.c) instead of the cards, with the
 * renderer blending as the option says: default (sRGB space, glyphs corrected), 1 (shapes
 * corrected too) or 2 (linear light); the window is titled "AXION blend <option>". Option 3 (a
 * layer blended in linear light) draws its layers as option 2 does; no node property marks such
 * a layer yet.
 *
 * --block-ms blocks the app thread that long every --block-every seconds (default 2).
 * --stats prints, once a second, what the window presented and how long the last change took from
 * commit to screen (ax_txn_on_presented). "missed" counts refreshes skipped while the window
 * presents (animating or resizing); "at rest" is the time it presented nothing because nothing
 * changed (decision 95), which is not missed.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axion/ax_base.h"
#include "axion/ax_runtime_window.h"
#include "axion/ax_txn.h"
#include "axion/base/ax_atomic.h"
#include "axion/base/ax_math.h"
#include "axion/base/ax_thread.h"
#include "axion/core/ax_world_sdk.h"
#include "axion/gpu/ax_gpu_surface.h"
#include "axion/platform/ax_hit_regions.h"
#include "axion/render/ax_render_soft_surface.h"
#include "axion/render/ax_render_wgpu.h"
#include "axion/runtime/ax_runtime_platform.h"
#include "m1_blend.h"
#include "m1_cards.h"

#define PI_F 3.14159265358979f
#define MS   1000000ull

typedef struct options {
    uint32_t decorations;
    bool stats;
    double duration;
    float width, height;
    double block_ms, block_every;
    int adapter, pacing, present_mode;
    int soft; /* --renderer=soft */
    uint64_t max_wait_ns;
    int blend_compare;  /* --blend-compare: 0 off, else 1 + ax_render_blend */
    bool size_given;
    bool still; /* --still: no automatic changes */
} options;

/* --blend-compare's options (issue #4) and the window titles they get. */
static const char *const BLEND_NAMES[3] = {"default (sRGB)", "1: sRGB, shapes corrected",
                                           "2: linear light"};
static const char *const BLEND_TITLES[3] = {"AXION blend default", "AXION blend 1",
                                            "AXION blend 2"};

typedef struct shared {
    options opt;
    ax_app app;
    ax_window window;
    ax_atomic_u32 stop;
    ax_atomic_u32 failed;
    uint64_t start_ns;
    bool csd;
} shared;

static const char usage[] =
    "usage: %s [--decorations=native|none] [--stats] [--duration=SECONDS] [--width=W]\n"
    "          [--height=H] [--block-ms=MS] [--block-every=SECONDS]\n"
    "          [--adapter=default|software] [--max-wait-ms=MS] [--renderer=gpu|soft]\n"
    "          [--pacing=default|compositor|driver]\n"
    "          [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate]\n"
    "          [--blend-compare[=default|1|2]] [--still]\n";

static int parse_options(int argc, char **argv, options *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->decorations = AX_DECORATIONS_NATIVE;
    o->width = 860;
    o->height = 860;
    o->block_every = 2.0;
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--decorations=native") == 0) {
            o->decorations = AX_DECORATIONS_NATIVE;
        } else if (strcmp(a, "--decorations=none") == 0) {
            o->decorations = AX_DECORATIONS_NONE;
        } else if (strcmp(a, "--stats") == 0) {
            o->stats = true;
        } else if (strncmp(a, "--duration=", 11) == 0) {
            o->duration = atof(a + 11);
        } else if (strncmp(a, "--width=", 8) == 0) {
            o->width = (float)atof(a + 8);
            o->size_given = true;
        } else if (strncmp(a, "--height=", 9) == 0) {
            o->height = (float)atof(a + 9);
            o->size_given = true;
        } else if (strncmp(a, "--block-ms=", 11) == 0) {
            o->block_ms = atof(a + 11);
        } else if (strncmp(a, "--block-every=", 14) == 0) {
            o->block_every = atof(a + 14);
        } else if (strncmp(a, "--max-wait-ms=", 14) == 0) {
            o->max_wait_ns = (uint64_t)(atof(a + 14) * 1e6);
        } else if (strcmp(a, "--renderer=gpu") == 0) {
            o->soft = 0;
        } else if (strcmp(a, "--blend-compare=default") == 0 ||
                   strcmp(a, "--blend-compare") == 0) {
            o->blend_compare = 1 + AX_RENDER_BLEND_SRGB;
        } else if (strcmp(a, "--blend-compare=1") == 0) {
            o->blend_compare = 1 + AX_RENDER_BLEND_SRGB_CORRECTED;
        } else if (strcmp(a, "--blend-compare=2") == 0) {
            o->blend_compare = 1 + AX_RENDER_BLEND_LINEAR;
        } else if (strcmp(a, "--still") == 0) {
            o->still = true;
        } else if (strcmp(a, "--renderer=soft") == 0) {
            o->soft = 1;
        } else if (strcmp(a, "--adapter=default") == 0) {
            o->adapter = AX_GPU_ADAPTER_DEFAULT;
        } else if (strcmp(a, "--adapter=software") == 0) {
            o->adapter = AX_GPU_ADAPTER_SOFTWARE;
        } else if (strcmp(a, "--pacing=default") == 0) {
            o->pacing = AX_GPU_PACING_DEFAULT;
        } else if (strcmp(a, "--pacing=compositor") == 0) {
            o->pacing = AX_GPU_PACING_COMPOSITOR;
        } else if (strcmp(a, "--pacing=driver") == 0) {
            o->pacing = AX_GPU_PACING_DRIVER;
        } else if (strcmp(a, "--present-mode=default") == 0) {
            o->present_mode = WGPUPresentMode_Undefined;
        } else if (strcmp(a, "--present-mode=fifo") == 0) {
            o->present_mode = WGPUPresentMode_Fifo;
        } else if (strcmp(a, "--present-mode=fifo-relaxed") == 0) {
            o->present_mode = WGPUPresentMode_FifoRelaxed;
        } else if (strcmp(a, "--present-mode=mailbox") == 0) {
            o->present_mode = WGPUPresentMode_Mailbox;
        } else if (strcmp(a, "--present-mode=immediate") == 0) {
            o->present_mode = WGPUPresentMode_Immediate;
        } else {
            fprintf(stderr, usage, argv[0]);
            return -1;
        }
    }
    if (o->block_every <= 0.0) {
        o->block_every = 2.0;
    }
    return 0;
}

static bool stopping(shared *s)
{
    return ax_atomic_load_u32(&s->stop, AX_MO_ACQUIRE) != 0;
}

/* ---- the app thread -------------------------------------------------------------------------- */

static void publish_regions(shared *s, float w, float h)
{
    ax_hit_region r[9];
    uint32_t n = 0;
    if (!s->csd) {
        ax_window_set_hit_regions(s->window, NULL, 0); /* the compositor's frame does it */
        return;
    }
    r[n++] = (ax_hit_region){0, 0, w, M1_CAPTION_HEIGHT, AX_REGION_CAPTION};
    r[n++] = (ax_hit_region){0, 0, w, M1_BORDER, AX_REGION_RESIZE_TOP};
    r[n++] = (ax_hit_region){0, h - M1_BORDER, w, M1_BORDER, AX_REGION_RESIZE_BOTTOM};
    r[n++] = (ax_hit_region){0, 0, M1_BORDER, h, AX_REGION_RESIZE_LEFT};
    r[n++] = (ax_hit_region){w - M1_BORDER, 0, M1_BORDER, h, AX_REGION_RESIZE_RIGHT};
    r[n++] = (ax_hit_region){0, 0, M1_CORNER, M1_CORNER, AX_REGION_RESIZE_TOP_LEFT};
    r[n++] = (ax_hit_region){w - M1_CORNER, 0, M1_CORNER, M1_CORNER, AX_REGION_RESIZE_TOP_RIGHT};
    r[n++] = (ax_hit_region){0, h - M1_CORNER, M1_CORNER, M1_CORNER, AX_REGION_RESIZE_BOTTOM_LEFT};
    r[n++] = (ax_hit_region){w - M1_CORNER, h - M1_CORNER, M1_CORNER, M1_CORNER,
                             AX_REGION_RESIZE_BOTTOM_RIGHT};
    ax_window_set_hit_regions(s->window, r, n);
}

typedef struct latency {
    uint64_t committed_ns; /* monotonic (the world's default clock) */
    uint64_t last_ns;      /* commit -> on screen of the last color change */
    uint64_t max_ns;
    uint32_t presented, other;
} latency;

static void on_presented(void *user, uint64_t seq, ax_presented_status status, uint64_t t)
{
    latency *l = (latency *)user;
    (void)seq;
    if (status == AX_PRESENTED && t >= l->committed_ns) {
        l->last_ns = t - l->committed_ns;
        if (l->last_ns > l->max_ns) {
            l->max_ns = l->last_ns;
        }
        l->presented++;
    } else {
        l->other++;
    }
}

static void print_stats(shared *s, ax_runtime rt, ax_rt_window_counters *prev, latency *l,
                        double blocked_ms)
{
    ax_rt_window_counters c;
    if (ax_rt_get_window_counters(rt, s->window, &c) != AX_OK) {
        return;
    }
    printf(
        "stats: %s | presented %llu | missed %llu | frames %llu | snapshots shown %llu | refresh "
        "%.3f ms | scene late %llu | forced %llu | change -> screen %.2f ms (max %.2f) "
        "| app blocked %.0f ms | at rest %.0f ms (%llu rests)\n",
        ax_rt_renderer_vtbl(rt) == ax_render_soft_vtbl() ? "soft" : "gpu",
        (unsigned long long)(c.presented - prev->presented),
        (unsigned long long)(c.missed_vblanks - prev->missed_vblanks),
        (unsigned long long)(c.frames - prev->frames),
        (unsigned long long)(c.snapshots_shown - prev->snapshots_shown), (double)c.refresh_ns / 1e6,
        (unsigned long long)(c.scene_late - prev->scene_late),
        (unsigned long long)(c.forced_acks - prev->forced_acks), (double)l->last_ns / 1e6,
        (double)l->max_ns / 1e6, blocked_ms, (double)(c.rest_ns - prev->rest_ns) / 1e6,
        (unsigned long long)(c.rests - prev->rests));
    fflush(stdout);
    *prev = c;
    l->max_ns = 0;
}

static int app_thread(void *arg)
{
    shared *s = (shared *)arg;
    ax_world world;
    ax_runtime rt;
    ax_runtime_desc d;
    ax_rt_platform_desc pd;
    ax_render_wgpu_desc wd;
    ax_render_soft_desc sd;
    ax_renderer_desc rd;
    ax_node root;
    static m1_cards cards;
    ax_rt_window_counters prev;
    latency lat;
    float w = s->opt.width, h = s->opt.height;
    uint64_t next_change, next_stats, next_block, seq = 0;
    double blocked_ms = 0.0;
    uint32_t step = 0;

    memset(&lat, 0, sizeof lat);
    memset(&prev, 0, sizeof prev);
    if (ax_world_create(NULL, &world) != AX_OK) {
        ax_atomic_store_u32(&s->failed, 1, AX_MO_RELEASE);
        ax_app_quit(s->app);
        return 1;
    }
    memset(&wd, 0, sizeof wd);
    memset(&sd, 0, sizeof sd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = (ax_gpu_adapter_kind)s->opt.adapter;
    wd.pacing = s->opt.pacing;
    wd.present_mode = s->opt.present_mode;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    if (s->opt.blend_compare != 0) {
        /* Issue #4's comparison: the renderer blends as the option says, GPU or software. */
        wd.blend = (uint32_t)(s->opt.blend_compare - 1);
        sd.chain.type = AX_RENDERER_CHAIN_SOFT;
        sd.chain.next = &wd.chain;
        sd.blend = wd.blend;
        rd.next = &sd.chain;
    }
    rd.initial_background = ax_color_from_srgb8(0xFF1E2129u);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.world = world;
    memset(&pd, 0, sizeof pd);
    pd.app = s->app;
    pd.renderer_desc = &rd;
    if (s->opt.soft) {
        pd.renderer = ax_render_soft_vtbl(); /* pinned; wd is ignored */
    }
    pd.max_wait_ns = s->opt.max_wait_ns;
    if (ax_rt_create_for_app(&d, &pd, &rt) != AX_OK ||
        ax_runtime_add_window(rt, s->window, &root) != AX_OK) {
        fprintf(stderr, "m1_nodes: runtime: %s: %.*s\n", ax_result_name(ax_last_error()->code),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        ax_world_destroy(world);
        ax_atomic_store_u32(&s->failed, 1, AX_MO_RELEASE);
        ax_app_quit(s->app);
        return 1;
    }

    /* The first configure tells the size and who draws the frame; then the tree. */
    {
        uint64_t until = ax_time_monotonic_ns() + 2000 * MS;
        ax_window_info info;
        while (ax_time_monotonic_ns() < until) {
            ax_world_event e;
            memset(&e, 0, sizeof e);
            e.struct_size = sizeof e;
            ax_world_wait_event(world, &e, 20 * MS);
            if (e.type == AX_WORLD_EVENT_RESIZED) {
                w = e.data.resized.width;
                h = e.data.resized.height;
                break;
            }
        }
        info.struct_size = sizeof info;
        s->csd = ax_window_get_info(s->window, &info) == AX_OK && !info.server_decorated;
        publish_regions(s, w, h);
    }
    if (s->opt.blend_compare != 0) {
        (void)m1_blend_build(world, root, BLEND_NAMES[s->opt.blend_compare - 1], s->csd, &seq);
        printf("m1_nodes: blend comparison, %s (%s)\n", BLEND_NAMES[s->opt.blend_compare - 1],
               s->opt.soft ? "soft" : "gpu");
    } else {
        (void)m1_cards_build(world, root, &cards, s->csd, &seq);
        printf("m1_nodes: %u cards committed (transaction %llu)%s\n", (unsigned)M1_CARDS,
               (unsigned long long)seq, s->csd ? ", client-side decorations" : "");
    }
    fflush(stdout);

    next_change = ax_time_monotonic_ns() + 1200 * MS;
    next_stats = ax_time_monotonic_ns() + 1000 * MS;
    next_block = ax_time_monotonic_ns() + (uint64_t)(s->opt.block_every * 1e9);
    while (!stopping(s)) {
        ax_world_event e;
        uint64_t now;
        memset(&e, 0, sizeof e);
        e.struct_size = sizeof e;
        ax_world_wait_event(world, &e, 20 * MS);
        switch (e.type) {
        case AX_WORLD_EVENT_RESIZED:
            publish_regions(s, e.data.resized.width, e.data.resized.height);
            break;
        case AX_WORLD_EVENT_CLOSE_REQUESTED:
            printf("close requested\n");
            ax_atomic_store_u32(&s->stop, 1, AX_MO_RELEASE);
            break;
        case AX_WORLD_EVENT_SCALE_CHANGED:
            printf("scale changed: %.3f\n", (double)e.data.scale_changed.scale);
            break;
        case AX_WORLD_EVENT_OCCLUSION:
            printf("occlusion: %s\n", e.data.occlusion.occluded ? "hidden" : "visible");
            break;
        case AX_WORLD_EVENT_A11Y_ACTION: {
            /* An assistive technology's action (decision 108): a card's default action grows or
             * shrinks it; the focus is the tree's own. */
            ax_node n;
            n.h = e.data.a11y_action.node;
            printf("a11y: %s on node %llx\n",
                   e.data.a11y_action.action == AX_ACTION_DEFAULT ? "default action"
                   : e.data.a11y_action.action == AX_ACTION_FOCUS ? "focus"
                                                                  : "action",
                   (unsigned long long)n.h);
            if (e.data.a11y_action.action == AX_ACTION_DEFAULT && s->opt.blend_compare == 0) {
                (void)m1_cards_toggle(world, &cards, n, NULL);
            }
            break;
        }
        default:
            break;
        }
        now = ax_time_monotonic_ns();
        if (now >= next_change && s->opt.blend_compare == 0 && !s->opt.still) {
            /* A reorder, a card growing or a new color: a transaction, timed to the screen; the
             * scene animates it. */
            if (m1_cards_step(world, &cards, step++, &seq) == AX_OK) {
                lat.committed_ns = ax_world_now(world);
                (void)ax_txn_on_presented(world, seq, on_presented, &lat);
            }
            next_change = now + 1200 * MS;
        }
        if (s->opt.block_ms > 0.0 && now >= next_block) {
            /* Block the app thread on purpose: running animations must not notice. */
            uint64_t until = now + (uint64_t)(s->opt.block_ms * 1e6);
            while (ax_time_monotonic_ns() < until) {
                ax_thread_yield();
            }
            blocked_ms += s->opt.block_ms;
            next_block = ax_time_monotonic_ns() + (uint64_t)(s->opt.block_every * 1e9);
        }
        if (s->opt.stats && ax_time_monotonic_ns() >= next_stats) {
            print_stats(s, rt, &prev, &lat, blocked_ms);
            blocked_ms = 0.0;
            next_stats += 1000 * MS;
        }
        if (s->opt.duration > 0.0 &&
            (double)(ax_time_monotonic_ns() - s->start_ns) / 1e9 >= s->opt.duration) {
            ax_atomic_store_u32(&s->stop, 1, AX_MO_RELEASE);
        }
    }
    (void)ax_runtime_remove_window(rt, s->window);
    (void)ax_runtime_destroy(rt);
    (void)ax_world_destroy(world);
    ax_app_quit(s->app);
    return 0;
}

/* ---- the main thread ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static shared s;
    ax_app_desc app_desc;
    ax_window_desc win_desc;
    ax_thread app_t;
    ax_result r;

    memset(&s, 0, sizeof s);
    if (parse_options(argc, argv, &s.opt) != 0) {
        return 2;
    }
    ax_atomic_init_u32(&s.stop, 0);
    ax_atomic_init_u32(&s.failed, 0);

    memset(&app_desc, 0, sizeof app_desc);
    app_desc.struct_size = sizeof app_desc;
    app_desc.allocator = ax_default_allocator();
    app_desc.app_id = AX_STR("net.axonworks.axion.m1_nodes");
    r = ax_app_create(&app_desc, &s.app);
    if (r != AX_OK) {
        fprintf(stderr, "m1_nodes: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        return 1;
    }
    memset(&win_desc, 0, sizeof win_desc);
    win_desc.struct_size = sizeof win_desc;
    win_desc.title = AX_STR("AXION m1_nodes");
    if (s.opt.blend_compare != 0) {
        const char *title = BLEND_TITLES[s.opt.blend_compare - 1];
        win_desc.title.ptr = title;
        win_desc.title.len = strlen(title);
        if (!s.opt.size_given) {
            s.opt.width = 928;
            s.opt.height = 664;
        }
    }
    win_desc.width = s.opt.width;
    win_desc.height = s.opt.height;
    win_desc.min_width = 240;
    win_desc.min_height = 160;
    win_desc.decorations = s.opt.decorations;
    win_desc.flags = AX_WINDOW_RESIZABLE;
    r = ax_window_create(s.app, &win_desc, &s.window);
    if (r != AX_OK) {
        fprintf(stderr, "m1_nodes: %s: %.*s\n", ax_result_name(r),
                (int)ax_last_error()->message.len, ax_last_error()->message.ptr);
        ax_app_destroy(s.app);
        return 1;
    }
    s.start_ns = ax_time_monotonic_ns();
    if (ax_thread_create(&app_t, app_thread, &s) != AX_OK) {
        fprintf(stderr, "m1_nodes: could not start the app thread\n");
        return 1;
    }
    r = ax_app_run(s.app); /* until the app thread calls ax_app_quit */
    ax_atomic_store_u32(&s.stop, 1, AX_MO_RELEASE);
    ax_thread_join(&app_t);
    ax_window_destroy(s.window);
    ax_app_destroy(s.app);
    return (r == AX_OK && ax_atomic_load_u32(&s.failed, AX_MO_ACQUIRE) == 0) ? 0 : 1;
}
