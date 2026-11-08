/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Animations while a window is being resized (decision 104): the resize itself never animates
 * frames (decision 94), but it neither stops the animations already running nor keeps a
 * transaction applied mid-resize from animating.
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_scene.h"
#include "axion/core/ax_world_sdk.h"

#define MS 1000000ull

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

static ax_dimension pct(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_PERCENT;
    return d;
}

/* One cycle as the runtime runs it: the viewport laid out first, then the transactions. */
static void cycle(ax_world w, manual_clock *clock, ax_node root, float width, uint64_t t)
{
    clock->now = t;
    if (ax_world_set_root_viewport(w, root, width, 300.0f, 1.0f) == AX_OK) {
        (void)ax_world_layout_viewports(w);
    }
    (void)ax_world_apply_pending(w, t);
    (void)ax_world_set_root_viewport(w, root, width, 300.0f, 1.0f);
    (void)ax_world_position(w);
    (void)ax_world_animate(w, t);
    ax_world_record_collect(w);
    ax_scene_clear_dirty(ax_world_scene(w));
}

typedef struct fixture {
    manual_clock clock;
    ax_world w;
    ax_node root, box;
    uint64_t now;
} fixture;

/* A root with one box, a third of the window wide, placed by its left inset. */
static bool setup(fixture *f)
{
    ax_world_desc desc;
    ax_txn *t;
    memset(f, 0, sizeof *f);
    f->clock.now = f->now = 1000 * MS;
    memset(&desc, 0, sizeof desc);
    desc.struct_size = sizeof desc;
    desc.clock = read_clock;
    desc.clock_user = &f->clock;
    if (ax_world_create(&desc, &f->w) != AX_OK) {
        return false;
    }
    t = ax_txn_begin(f->w);
    f->root = ax_txn_create(t, AX_NODE_VIEW);
    f->box = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_u32(t, f->box, AX_PROP_POSITION, AX_POSITION_ABSOLUTE);
    ax_txn_set_dimension(t, f->box, AX_PROP_LEFT, pt(0.0f));
    ax_txn_set_dimension(t, f->box, AX_PROP_TOP, pt(0.0f));
    ax_txn_set_dimension(t, f->box, AX_PROP_WIDTH, pct(33.0f));
    ax_txn_set_dimension(t, f->box, AX_PROP_HEIGHT, pt(40.0f));
    ax_txn_append_child(t, f->root, f->box);
    if (ax_txn_commit(t, NULL) != AX_OK) {
        return false;
    }
    cycle(f->w, &f->clock, f->root, 600.0f, f->now);
    return true;
}

static ax_rect presented(const fixture *f)
{
    ax_rect r;
    (void)ax_world_presentation_frame(f->w, f->box, &r);
    return r;
}

static ax_rect target(const fixture *f)
{
    const ax_scene *s = ax_world_scene(f->w);
    return s->store.layout[ax_node_store_row(&s->store, f->box)].frame;
}

AX_TEST(a_resize_neither_animates_nor_stops_running_animations)
{
    static fixture f;
    ax_txn *t;
    ax_rect p, tg;
    AX_REQUIRE(setup(&f));
    /* The box slides right with the default spring. */
    t = ax_txn_begin(f.w);
    ax_txn_set_dimension(t, f.box, AX_PROP_LEFT, pt(200.0f));
    AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
    f.now += 16 * MS;
    cycle(f.w, &f.clock, f.root, 600.0f, f.now);
    f.now += 50 * MS;
    cycle(f.w, &f.clock, f.root, 600.0f, f.now);
    AX_CHECK(presented(&f).x < 190.0f); /* moving */

    /* The window narrows mid-slide: the width follows at once (no animation of the resize), and
     * the slide goes on. */
    f.now += 16 * MS;
    cycle(f.w, &f.clock, f.root, 450.0f, f.now);
    p = presented(&f);
    tg = target(&f);
    AX_CHECK_NEAR_F(tg.w, 148.5f, 0.5f);   /* 33 % of 450 */
    AX_CHECK_NEAR_F(p.w, tg.w, 0.01f);     /* the resize did not animate */
    AX_CHECK(p.x > 1.0f && p.x < 199.0f);  /* still sliding */
    AX_CHECK(ax_world_root_animating(f.w, f.root));
    f.now += 1000 * MS;
    cycle(f.w, &f.clock, f.root, 450.0f, f.now);
    AX_CHECK_NEAR_F(presented(&f).x, 200.0f, 0.01f);
    ax_world_destroy(f.w);
}

AX_TEST(a_transaction_applied_mid_resize_animates)
{
    static fixture f;
    ax_txn *t;
    uint64_t end;
    AX_REQUIRE(setup(&f));
    /* Every cycle a new window width, and in one of them a move. */
    end = f.now + 160 * MS;
    while (f.now < end) {
        f.now += 16 * MS;
        if (f.now >= end - 80 * MS && f.now < end - 64 * MS) {
            t = ax_txn_begin(f.w);
            ax_txn_set_dimension(t, f.box, AX_PROP_LEFT, pt(200.0f));
            AX_REQUIRE(ax_txn_commit(t, NULL) == AX_OK);
        }
        cycle(f.w, &f.clock, f.root, 600.0f - (float)((f.now / MS) % 100u), f.now);
    }
    AX_CHECK(presented(&f).x > 1.0f && presented(&f).x < 199.0f); /* animating, not snapped */
    AX_CHECK_NEAR_F(presented(&f).w, target(&f).w, 0.01f);        /* the resize, at once */
    ax_world_destroy(f.w);
}
