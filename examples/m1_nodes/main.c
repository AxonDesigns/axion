/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * m1_nodes: m0_window's picture, built as a node tree by the app thread and drawn through the M1
 * pipeline (decisions 76 to 78).
 *
 *   main thread    runs the platform event pump (ax_app_run) and nothing else
 *   app thread     creates the world and the runtime, builds the tree with transactions, reads
 *                  window events from the world (ax_world_wait_event), publishes the hit regions,
 *                  changes a color every second, and with --block-ms blocks itself on purpose
 *   scene thread   the runtime's: applies transactions at each vsync, positions, records the
 *                  Render IR from the tree, publishes the mirror
 *   render thread  the runtime's: presents with ax_render_wgpu, or with ax_render_soft into
 *                  shared-memory buffers (--renderer=soft, or when no GPU is usable)
 *
 * Motion. Implicit animations do not exist yet, so a scene driver that exists only in this
 * example (slide() below, ax_rt_scene_driver) turns and moves nodes at every tick from the
 * frame's predicted present time, and places what depends on the window's size (the bottom bar,
 * the right and bottom edges) until flex can. It goes away with the fourth prompt of M1. Because
 * it runs on the scene thread, blocking the app thread must not show in the motion.
 *
 * Usage: m1_nodes [--decorations=native|none] [--stats] [--duration=SECONDS] [--width=W]
 *                 [--height=H] [--block-ms=MS] [--block-every=SECONDS]
 *                 [--adapter=default|software] [--max-wait-ms=MS] [--renderer=gpu|soft]
 *                 [--pacing=default|compositor|driver]
 *                 [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate]
 *
 * --block-ms blocks the app thread that long every --block-every seconds (default 2).
 * --stats prints, once a second, what the window presented and how long the last color change
 * took from commit to screen (ax_txn_on_presented).
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

#define PI_F 3.14159265358979f
#define MS   1000000ull

/* As m0_window (examples/m0_window/m0_window.h). */
#define CAPTION_HEIGHT 32.0f
#define BORDER         8.0f
#define CORNER         16.0f
#define BAR_SPEED      400.0
#define BAR_TRACK      1200.0
#define COLOR_BG       0xFF1E2129u
#define COLOR_CAPTION  0xFF2D3240u
#define COLOR_EDGE     0xFF4F7CFFu
#define COLOR_FG       0xFFE8ECF4u
#define COLOR_BAND     0xFF262A33u
#define COLOR_CORNER   0xFF3A4152u

typedef struct options {
    uint32_t decorations;
    bool stats;
    double duration;
    float width, height;
    double block_ms, block_every;
    int adapter, pacing, present_mode;
    int soft; /* --renderer=soft */
    uint64_t max_wait_ns;
} options;

/* The nodes the scene driver moves. The app thread fills them before it commits the tree and
 * then sets `ready` (release); the driver reads them only after it sees `ready` (acquire). */
typedef struct moving {
    ax_atomic_u32 ready;
    bool csd;
    float left, top;
    ax_node turning, fan, knob, tick, bar;
    ax_node band_left, band_right, band_bottom, caption;
    ax_node corner_tr, corner_bl, corner_br;
    ax_node edge_top, edge_bottom, edge_left, edge_right;
} moving;

typedef struct shared {
    options opt;
    ax_app app;
    ax_window window;
    ax_atomic_u32 stop;
    ax_atomic_u32 failed;
    uint64_t start_ns;
    moving m;
} shared;

static const char usage[] =
    "usage: %s [--decorations=native|none] [--stats] [--duration=SECONDS] [--width=W]\n"
    "          [--height=H] [--block-ms=MS] [--block-every=SECONDS]\n"
    "          [--adapter=default|software] [--max-wait-ms=MS] [--renderer=gpu|soft]\n"
    "          [--pacing=default|compositor|driver]\n"
    "          [--present-mode=default|fifo|fifo-relaxed|mailbox|immediate]\n";

static int parse_options(int argc, char **argv, options *o)
{
    int i;
    memset(o, 0, sizeof *o);
    o->decorations = AX_DECORATIONS_NATIVE;
    o->width = 800;
    o->height = 500;
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
        } else if (strncmp(a, "--height=", 9) == 0) {
            o->height = (float)atof(a + 9);
        } else if (strncmp(a, "--block-ms=", 11) == 0) {
            o->block_ms = atof(a + 11);
        } else if (strncmp(a, "--block-every=", 14) == 0) {
            o->block_every = atof(a + 14);
        } else if (strncmp(a, "--max-wait-ms=", 14) == 0) {
            o->max_wait_ns = (uint64_t)(atof(a + 14) * 1e6);
        } else if (strcmp(a, "--renderer=gpu") == 0) {
            o->soft = 0;
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

/* ---- building the tree (app thread) ---------------------------------------------------------- */

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static ax_rgba c8(uint32_t argb)
{
    return ax_rgba_from_srgb8((uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb,
                              (uint8_t)(argb >> 24));
}

static ax_transform2d rotation(float radians)
{
    ax_affine2 m = ax_affine2_rotate(radians);
    ax_transform2d t;
    t.a = m.a;
    t.b = m.b;
    t.c = m.c;
    t.d = m.d;
    t.tx = m.tx;
    t.ty = m.ty;
    return t;
}

/* A rect node under parent. */
static ax_node rect(ax_txn *t, ax_node parent, float x, float y, float w, float h, ax_rgba color)
{
    ax_node n = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_dimension(t, n, AX_PROP_LEFT, pt(x));
    ax_txn_set_dimension(t, n, AX_PROP_TOP, pt(y));
    ax_txn_set_dimension(t, n, AX_PROP_WIDTH, pt(w));
    ax_txn_set_dimension(t, n, AX_PROP_HEIGHT, pt(h));
    ax_txn_set_color(t, n, AX_PROP_BACKGROUND_COLOR, color);
    ax_txn_append_child(t, parent, n);
    return n;
}

static void radii(ax_txn *t, ax_node n, float tl, float tr, float br, float bl)
{
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_TOP_LEFT, tl);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_TOP_RIGHT, tr);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT, br);
    ax_txn_set_f32(t, n, AX_PROP_CORNER_RADIUS_BOTTOM_LEFT, bl);
}

/* m0_window's scene (examples/m0_window/scene.c) as nodes. Returns the first rounded rect, whose
 * color the app thread changes every second. */
static ax_node build(ax_world world, ax_node root, moving *m, float w, float h, uint64_t *out_seq)
{
    static const float r4[4][4] = {{0.0f, 6.0f, 12.0f, 24.0f},
                                   {16.0f, 16.0f, 16.0f, 16.0f},
                                   {2.0f, 30.0f, 2.0f, 30.0f},
                                   {40.0f, 0.0f, 40.0f, 0.0f}};
    static const float thin[4] = {0.25f, 0.5f, 1.0f, 1.5f};
    const ax_rgba fg = c8(COLOR_FG), accent = c8(COLOR_EDGE), none = {0, 0, 0, 0};
    ax_txn *t = ax_txn_begin(world);
    ax_node first = AX_HANDLE_NULL_INIT, n, group;
    float left, top;
    int i;

    m->top = top = (m->csd ? CAPTION_HEIGHT : 0.0f) + 16.0f;
    m->left = left = (m->csd ? BORDER : 0.0f) + 16.0f;
    ax_txn_set_color(t, root, AX_PROP_BACKGROUND_COLOR, c8(COLOR_BG));

    /* Rounded rects, a different radius per corner. */
    for (i = 0; i < 4; ++i) {
        n = rect(t, root, left + (float)i * 96.0f, top, 80.0f, 56.0f, i % 2 == 0 ? fg : accent);
        radii(t, n, r4[i][0], r4[i][1], r4[i][2], r4[i][3]);
        if (i == 0) {
            first = n;
        }
    }

    /* A rounded rect turning about its center, with a translucent rect turning with it. */
    m->turning = rect(t, root, left + 60.0f - 40.0f, top + 130.0f - 26.0f, 80.0f, 52.0f, fg);
    radii(t, m->turning, 10.0f, 2.0f, 18.0f, 6.0f);
    (void)rect(t, m->turning, 20.0f, -14.0f, 40.0f, 80.0f,
               ax_rgba_from_srgb(0.31f, 0.49f, 1.0f, 0.5f));

    /* A hairline fan: a point at its center, turning, with 0.5-unit lines every 15 degrees. */
    m->fan = rect(t, root, left + 190.0f, top + 130.0f, 0.0f, 0.0f, none);
    for (i = 0; i < 12; ++i) {
        n = rect(t, m->fan, -50.0f, -0.25f, 100.0f, 0.5f, fg);
        ax_txn_set_transform(t, n, AX_PROP_TRANSFORM, rotation((float)i * PI_F / 12.0f));
    }

    /* A slow, fractional slider: a track line, a knob and its tick. */
    (void)rect(t, root, left + 260.0f, top + 104.0f, 104.0f + 24.0f, 0.5f, accent);
    m->knob = rect(t, root, left + 262.0f, top + 110.0f, 24.0f, 24.0f, fg);
    radii(t, m->knob, 4.0f, 4.0f, 4.0f, 4.0f);
    m->tick = rect(t, root, left + 273.5f, top + 140.0f, 1.0f, 20.0f, fg);

    /* Thin rects: straight, then a group turned by 3 degrees. */
    for (i = 0; i < 4; ++i) {
        (void)rect(t, root, left + 400.0f + (float)i * 14.0f, top, thin[i], 120.0f, fg);
        (void)rect(t, root, left + 380.0f, top + 140.0f + (float)i * 8.0f, 90.0f, thin[i], fg);
    }
    group = rect(t, root, left + 480.0f, top + 60.0f, 0.0f, 0.0f, none);
    ax_txn_set_transform(t, group, AX_PROP_TRANSFORM, rotation(0.0523599f));
    for (i = 0; i < 4; ++i) {
        (void)rect(t, group, (float)i * 14.0f, -60.0f, thin[i], 120.0f, fg);
    }

    /* The timed bar along the bottom (placed by the driver). */
    m->bar = rect(t, root, -24.0f, h - 40.0f, 24.0f, 40.0f, fg);

    /* Without server decorations: the title band, resize borders and corners, and an outline,
     * over exactly the hit regions; the driver follows the window's size. */
    if (m->csd) {
        const ax_rgba band = c8(COLOR_BAND), corner = c8(COLOR_CORNER);
        m->band_left = rect(t, root, 0, 0, BORDER, h, band);
        m->band_right = rect(t, root, w - BORDER, 0, BORDER, h, band);
        m->band_bottom = rect(t, root, 0, h - BORDER, w, BORDER, band);
        m->caption = rect(t, root, 0, 0, w, CAPTION_HEIGHT, c8(COLOR_CAPTION));
        (void)rect(t, root, 0, 0, CORNER, CORNER, corner);
        m->corner_tr = rect(t, root, w - CORNER, 0, CORNER, CORNER, corner);
        m->corner_bl = rect(t, root, 0, h - CORNER, CORNER, CORNER, corner);
        m->corner_br = rect(t, root, w - CORNER, h - CORNER, CORNER, CORNER, corner);
        m->edge_top = rect(t, root, 0, 0, w, 1, accent);
        m->edge_bottom = rect(t, root, 0, h - 1, w, 1, accent);
        m->edge_left = rect(t, root, 0, 0, 1, h, accent);
        m->edge_right = rect(t, root, w - 1, 0, 1, h, accent);
    }
    if (ax_txn_commit(t, out_seq) != AX_OK) {
        first.h = 0;
    }
    ax_atomic_store_u32(&m->ready, 1, AX_MO_RELEASE);
    return first;
}

/* ---- the scene driver (scene thread; example only, until animations) ------------------------- */

static void set_dim(ax_scene *scene, ax_node n, ax_prop p, float v)
{
    ax_value value;
    value.dimension = pt(v);
    (void)ax_scene_set(scene, n, p, AX_VALUE_DIMENSION, &value);
}

static void set_box(ax_scene *scene, ax_node n, float x, float y, float w, float h)
{
    set_dim(scene, n, AX_PROP_LEFT, x);
    set_dim(scene, n, AX_PROP_TOP, y);
    set_dim(scene, n, AX_PROP_WIDTH, w);
    set_dim(scene, n, AX_PROP_HEIGHT, h);
}

static void set_rotation(ax_scene *scene, ax_node n, float radians)
{
    ax_value value;
    value.transform2d = ax_affine2_rotate(radians);
    (void)ax_scene_set(scene, n, AX_PROP_TRANSFORM, AX_VALUE_TRANSFORM2D, &value);
}

static void slide(void *user, ax_world world, ax_node root, uint64_t time_ns, float w, float h)
{
    moving *m = (moving *)user;
    ax_scene *scene = ax_world_scene(world);
    const double t = (double)time_ns / 1e9; /* the predicted present time, as in m0_window */
    (void)root;
    if (ax_atomic_load_u32(&m->ready, AX_MO_ACQUIRE) == 0) {
        return;
    }
    set_rotation(scene, m->turning, (float)fmod(t * PI_F / 4.0, 2.0 * PI_F));
    set_rotation(scene, m->fan, (float)fmod(t * PI_F / 18.0, 2.0 * PI_F));
    {
        /* 7.3 units a second over 100 units and back. */
        const double period = 2.0 * 100.0 / 7.3;
        const double phase = fmod(t, period) / period;
        const float x = (float)(phase < 0.5 ? phase * 200.0 : (1.0 - phase) * 200.0);
        set_dim(scene, m->knob, AX_PROP_LEFT, m->left + 260.0f + x * 0.5f + 2.0f);
        set_dim(scene, m->tick, AX_PROP_LEFT, m->left + 260.0f + x * 0.5f + 13.5f);
    }
    {
        const float bottom = h - (m->csd ? BORDER : 0.0f);
        set_dim(scene, m->bar, AX_PROP_LEFT, (float)(fmod(t * BAR_SPEED, BAR_TRACK) - 24.0));
        set_dim(scene, m->bar, AX_PROP_TOP, bottom - 40.0f);
    }
    if (m->csd) { /* setting what did not change marks nothing */
        set_box(scene, m->band_left, 0, 0, BORDER, h);
        set_box(scene, m->band_right, w - BORDER, 0, BORDER, h);
        set_box(scene, m->band_bottom, 0, h - BORDER, w, BORDER);
        set_box(scene, m->caption, 0, 0, w, CAPTION_HEIGHT);
        set_box(scene, m->corner_tr, w - CORNER, 0, CORNER, CORNER);
        set_box(scene, m->corner_bl, 0, h - CORNER, CORNER, CORNER);
        set_box(scene, m->corner_br, w - CORNER, h - CORNER, CORNER, CORNER);
        set_box(scene, m->edge_top, 0, 0, w, 1);
        set_box(scene, m->edge_bottom, 0, h - 1, w, 1);
        set_box(scene, m->edge_left, 0, 0, 1, h);
        set_box(scene, m->edge_right, w - 1, 0, 1, h);
    }
}

/* ---- the app thread -------------------------------------------------------------------------- */

static void publish_regions(shared *s, float w, float h)
{
    ax_hit_region r[9];
    uint32_t n = 0;
    if (!s->m.csd) {
        ax_window_set_hit_regions(s->window, NULL, 0); /* the compositor's frame does it */
        return;
    }
    r[n++] = (ax_hit_region){0, 0, w, CAPTION_HEIGHT, AX_REGION_CAPTION};
    r[n++] = (ax_hit_region){0, 0, w, BORDER, AX_REGION_RESIZE_TOP};
    r[n++] = (ax_hit_region){0, h - BORDER, w, BORDER, AX_REGION_RESIZE_BOTTOM};
    r[n++] = (ax_hit_region){0, 0, BORDER, h, AX_REGION_RESIZE_LEFT};
    r[n++] = (ax_hit_region){w - BORDER, 0, BORDER, h, AX_REGION_RESIZE_RIGHT};
    r[n++] = (ax_hit_region){0, 0, CORNER, CORNER, AX_REGION_RESIZE_TOP_LEFT};
    r[n++] = (ax_hit_region){w - CORNER, 0, CORNER, CORNER, AX_REGION_RESIZE_TOP_RIGHT};
    r[n++] = (ax_hit_region){0, h - CORNER, CORNER, CORNER, AX_REGION_RESIZE_BOTTOM_LEFT};
    r[n++] = (ax_hit_region){w - CORNER, h - CORNER, CORNER, CORNER, AX_REGION_RESIZE_BOTTOM_RIGHT};
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
        "%.3f ms | scene late %llu | forced %llu | color change -> screen %.2f ms (max %.2f) "
        "| app blocked %.0f ms\n",
        ax_rt_renderer_vtbl(rt) == ax_render_soft_vtbl() ? "soft" : "gpu",
        (unsigned long long)(c.presented - prev->presented),
        (unsigned long long)(c.missed_vblanks - prev->missed_vblanks),
        (unsigned long long)(c.frames - prev->frames),
        (unsigned long long)(c.snapshots_shown - prev->snapshots_shown), (double)c.refresh_ns / 1e6,
        (unsigned long long)(c.scene_late - prev->scene_late),
        (unsigned long long)(c.forced_acks - prev->forced_acks), (double)l->last_ns / 1e6,
        (double)l->max_ns / 1e6, blocked_ms);
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
    ax_renderer_desc rd;
    ax_node root, swatch = AX_HANDLE_NULL_INIT;
    ax_rt_window_counters prev;
    latency lat;
    float w = s->opt.width, h = s->opt.height;
    uint64_t next_color, next_stats, next_block, seq = 0;
    double blocked_ms = 0.0;
    uint32_t color_index = 0;
    static const uint32_t colors[4] = {COLOR_FG, 0xFFFF7A59u, 0xFF5BD18Bu, 0xFFF7C948u};

    memset(&lat, 0, sizeof lat);
    memset(&prev, 0, sizeof prev);
    if (ax_world_create(NULL, &world) != AX_OK) {
        ax_atomic_store_u32(&s->failed, 1, AX_MO_RELEASE);
        ax_app_quit(s->app);
        return 1;
    }
    memset(&wd, 0, sizeof wd);
    wd.chain.type = AX_RENDERER_CHAIN_WGPU;
    wd.adapter = (ax_gpu_adapter_kind)s->opt.adapter;
    wd.pacing = s->opt.pacing;
    wd.present_mode = s->opt.present_mode;
    memset(&rd, 0, sizeof rd);
    rd.struct_size = sizeof rd;
    rd.next = &wd.chain;
    rd.initial_background = ax_color_from_srgb8(COLOR_BG);
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
    pd.driver = slide;
    pd.driver_user = &s->m;
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
        s->m.csd = ax_window_get_info(s->window, &info) == AX_OK && !info.server_decorated;
        publish_regions(s, w, h);
    }
    swatch = build(world, root, &s->m, w, h, &seq);
    printf("m1_nodes: tree committed (transaction %llu)%s\n", (unsigned long long)seq,
           s->m.csd ? ", client-side decorations" : "");
    fflush(stdout);

    next_color = ax_time_monotonic_ns() + 1000 * MS;
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
        default:
            break;
        }
        now = ax_time_monotonic_ns();
        if (now >= next_color && !AX_HANDLE_IS_NULL(swatch)) {
            /* A change that goes through a transaction, timed to the screen. */
            ax_txn *t = ax_txn_begin(world);
            color_index = (color_index + 1u) % 4u;
            ax_txn_set_color(t, swatch, AX_PROP_BACKGROUND_COLOR, c8(colors[color_index]));
            if (ax_txn_commit(t, &seq) == AX_OK) {
                lat.committed_ns = ax_world_now(world);
                (void)ax_txn_on_presented(world, seq, on_presented, &lat);
            }
            next_color = now + 1000 * MS;
        }
        if (s->opt.block_ms > 0.0 && now >= next_block) {
            /* Block the app thread on purpose: motion must not notice. */
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
    ax_atomic_init_u32(&s.m.ready, 0);

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
