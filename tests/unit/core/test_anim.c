/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implicit animations (decision 94), through the frame's stages with a manual clock: the transition
 * lookup and its defaults, springs and timing curves, interruption (a spring keeps its velocity, a
 * timing curve blends), set_animated(false), reduce motion, clamping, layout animations of frames,
 * the fast path of transform and opacity (no item recorded again), the mirror showing targets, and
 * determinism (the same sequence gives the same bits).
 */
#include <math.h>
#include <string.h>

#include "ax_test.h"
#include "axion/ax_error.h"
#include "axion/ax_mirror.h"
#include "axion/ax_txn.h"
#include "axion/core/ax_frame.h"
#include "axion/core/ax_world_sdk.h"

#define MS 1000000ull

typedef struct manual_clock {
    uint64_t now;
} manual_clock;

static uint64_t read_clock(void *user)
{
    return ((manual_clock *)user)->now;
}

typedef struct pipe {
    manual_clock clock;
    ax_world w;
    ax_ir_builder b;
    ax_node root, list, card[3];
    uint64_t t; /* the last frame's time */
    char dump[16384];
    size_t len;
} pipe;

static void write_dump(void *ctx, const char *s, size_t n)
{
    pipe *p = (pipe *)ctx;
    if (p->len + n < sizeof p->dump) {
        memcpy(p->dump + p->len, s, n);
        p->len += n;
        p->dump[p->len] = '\0';
    }
}

static ax_dimension pt(float v)
{
    ax_dimension d;
    d.value = v;
    d.unit = AX_UNIT_POINTS;
    return d;
}

/* One frame at time t (refresh 16 ms), in the pipeline's order, with an IR dump. */
static void frame_at(pipe *p, uint64_t t)
{
    const ax_render_ir *ir = NULL;
    p->t = t;
    (void)ax_world_apply_pending(p->w, t - 16 * MS);
    (void)ax_world_set_root_viewport(p->w, p->root, 400, 300, 1.0f);
    (void)ax_world_position(p->w);
    (void)ax_world_animate(p->w, t);
    ax_world_record_collect(p->w);
    ax_ir_builder_begin(&p->b, 400, 300, t, ax_color_make(0, 0, 0, 1));
    (void)ax_world_record_root(p->w, p->root, &p->b);
    (void)ax_ir_builder_end(&p->b, &ir);
    p->len = 0;
    p->dump[0] = '\0';
    if (ir != NULL) {
        ax_ir_dump(ir, write_dump, p);
    }
    (void)ax_world_publish_mirror(p->w);
    ax_scene_clear_dirty(ax_world_scene(p->w));
}

/* A column of three cards (100 x 40, gap 10) under a 400 x 300 window; the first frame at 100 ms
 * (the window's first content never animates). */
static bool pipe_start(pipe *p)
{
    ax_world_desc wd;
    ax_txn *t;
    int i;
    memset(p, 0, sizeof *p);
    p->clock.now = 50 * MS;
    memset(&wd, 0, sizeof wd);
    wd.struct_size = sizeof wd;
    wd.clock = read_clock;
    wd.clock_user = &p->clock;
    if (ax_world_create(&wd, &p->w) != AX_OK) {
        return false;
    }
    ax_ir_builder_init(&p->b, ax_default_allocator());
    t = ax_txn_begin(p->w);
    p->root = ax_txn_create(t, AX_NODE_VIEW);
    p->list = ax_txn_create(t, AX_NODE_VIEW);
    ax_txn_set_dimension(t, p->list, AX_PROP_GAP, pt(10));
    ax_txn_set_dimension(t, p->list, AX_PROP_PADDING, pt(20));
    ax_txn_append_child(t, p->root, p->list);
    for (i = 0; i < 3; ++i) {
        p->card[i] = ax_txn_create(t, AX_NODE_VIEW);
        ax_txn_set_dimension(t, p->card[i], AX_PROP_WIDTH, pt(100));
        ax_txn_set_dimension(t, p->card[i], AX_PROP_HEIGHT, pt(40));
        ax_txn_set_color(t, p->card[i], AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(1, 1, 1, 1));
        ax_txn_append_child(t, p->list, p->card[i]);
    }
    if (ax_txn_commit(t, NULL) != AX_OK) {
        return false;
    }
    frame_at(p, 100 * MS);
    return true;
}

static void pipe_stop(pipe *p)
{
    ax_ir_builder_deinit(&p->b);
    (void)ax_world_destroy(p->w);
}

/* Commits a transaction the test filled, at the current clock. */
static void commit(pipe *p, ax_txn *t)
{
    (void)p; /* at the clock the test set */
    AX_CHECK_RESULT(ax_txn_commit(t, NULL), AX_OK);
}

static float opacity_of(pipe *p, ax_node n)
{
    ax_value v;
    (void)ax_world_presentation(p->w, n, AX_PROP_OPACITY, &v);
    return v.f32;
}

static ax_rect frame_of(pipe *p, ax_node n)
{
    ax_rect r;
    (void)ax_world_presentation_frame(p->w, n, &r);
    return r;
}

static ax_transition kind(ax_transition_kind k)
{
    ax_transition tr;
    memset(&tr, 0, sizeof tr);
    tr.struct_size = sizeof tr;
    tr.kind = (uint32_t)k;
    return tr;
}

/* ---- Defaults, springs and the fast path
 * --------------------------------------------------------- */

AX_TEST(opacity_springs_to_its_target_without_recording_items_again)
{
    static pipe p;
    ax_txn *t;
    ax_frame_counters fc;
    ax_anim_counters ac;
    float prev = 1.0f;
    uint64_t ms;
    float o;
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_f32(t, p.card[1], AX_PROP_OPACITY, 0.2f);
    commit(&p, t);
    /* The change applies at the frame whose latch (T - 16 ms) follows the commit: T = 132 ms;
     * the animation starts at the latch (116 ms), so that frame already shows 16 ms of motion. */
    frame_at(&p, 132 * MS);
    ax_world_get_frame_counters(p.w, &fc, true); /* the promotion records the card once */
    o = opacity_of(&p, p.card[1]);
    AX_CHECK(o < 1.0f && o > 0.2f);
    /* The mirror shows the target at once. */
    (void)ax_world_mirror_sync(p.w);
    {
        float m = 0.0f;
        AX_CHECK_RESULT(ax_mirror_get_f32(p.w, p.card[1], AX_PROP_OPACITY, &m), AX_OK);
        AX_CHECK(m == 0.2f);
    }
    /* A critically damped spring never overshoots: the value only falls. */
    for (ms = 148; ms <= 900; ms += 16) {
        frame_at(&p, ms * MS);
        o = opacity_of(&p, p.card[1]);
        AX_CHECK(o <= prev + 1e-6f && o >= 0.2f - 1e-6f);
        prev = o;
    }
    ax_world_get_frame_counters(p.w, &fc, true);
    AX_CHECK_EQ_U(fc.items_recorded, 0); /* the fast path: the layer's opacity only */
    AX_CHECK(fc.snapshots > 40);
    AX_CHECK(opacity_of(&p, p.card[1]) == 0.2f); /* at rest, exactly the target */
    ax_world_get_anim_counters(p.w, &ac, false);
    AX_CHECK_EQ_U(ac.started, 1);
    AX_CHECK_EQ_U(ac.finished, 1);
    AX_CHECK_EQ_U(ac.active, 0);
    pipe_stop(&p);
}

AX_TEST(colors_ease_over_the_default_two_tenths_of_a_second)
{
    static pipe p;
    ax_txn *t;
    ax_value v;
    ax_frame_counters fc;
    const ax_rgba red = ax_rgba_from_srgb(1, 0, 0, 1);
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_color(t, p.card[0], AX_PROP_BACKGROUND_COLOR, red);
    commit(&p, t);
    ax_world_get_frame_counters(p.w, &fc, true);
    frame_at(&p, 132 * MS); /* started at 116: 16 of 200 ms */
    AX_CHECK_RESULT(ax_world_presentation(p.w, p.card[0], AX_PROP_BACKGROUND_COLOR, &v), AX_OK);
    AX_CHECK(v.color.g > 0.5f && v.color.g < 1.0f);
    ax_world_get_frame_counters(p.w, &fc, true);
    AX_CHECK_EQ_U(fc.nodes_recorded, 1); /* paint: the card's items again */
    frame_at(&p, 316 * MS);              /* 116 + 200: done */
    AX_CHECK_RESULT(ax_world_presentation(p.w, p.card[0], AX_PROP_BACKGROUND_COLOR, &v), AX_OK);
    AX_CHECK(v.color.r == red.r && v.color.g == red.g && v.color.a == red.a);
    pipe_stop(&p);
}

/* ---- Interruption -------------------------------------------------------------------------------
 */

/* The presentation x of card 2 at time t, with a frame at t (a probe). */
static float x_at(pipe *p, uint64_t t)
{
    frame_at(p, t);
    return frame_of(p, p->card[2]).x;
}

AX_TEST(a_spring_retargeted_mid_flight_keeps_its_velocity)
{
    static pipe p;
    ax_txn *t;
    float x0, x1, x2, v_before, v_after, a, b, c;
    AX_REQUIRE(pipe_start(&p));
    /* Card 2 moves right by 200 (a margin), with the default spring (layout). */
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_dimension(t, p.card[2], AX_PROP_MARGIN_LEFT, pt(200));
    commit(&p, t);
    (void)x_at(&p, 132 * MS);
    (void)x_at(&p, 148 * MS);
    /* Three frames 1 ms apart before the retarget, to measure the velocity. */
    a = x_at(&p, 179 * MS);
    b = x_at(&p, 180 * MS);
    v_before = (b - a) / 0.001f;
    /* Retarget at the latch of the frame at 196 ms (180 ms): back to the left. */
    p.clock.now = 170 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_dimension(t, p.card[2], AX_PROP_MARGIN_LEFT, pt(0));
    commit(&p, t);
    x0 = b;
    x1 = x_at(&p, 196 * MS); /* applies with change time 180 ms */
    (void)x1;
    /* The motion after 180 ms, sampled 1 and 2 ms later. */
    c = x_at(&p, 181 * MS);
    x2 = x_at(&p, 182 * MS);
    v_after = (x2 - c) / 0.001f;
    /* Position continuous across the retarget, and the velocity too: it still moves right at
     * first, decelerating, although the target is now on the left. */
    AX_CHECK_NEAR_F(c, x0 + v_before * 0.001f, 0.05f);
    AX_CHECK(v_before > 100.0f);
    AX_CHECK_NEAR_F(v_after, v_before, 0.1f * v_before);
    /* And it comes to rest at the new target. */
    AX_CHECK_NEAR_F(x_at(&p, 1500 * MS), 20.0f, 0.0f);
    pipe_stop(&p);
}

AX_TEST(timing_curves_blend_into_the_new_one)
{
    static pipe p;
    ax_txn *t;
    ax_transition lin = kind(AX_TRANSITION_TIMING);
    float before, after;
    lin.duration = 0.5f; /* all-zero curve: linear */
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_transition(t, p.card[0], AX_PROP_OPACITY, &lin);
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 0.0f);
    commit(&p, t);
    frame_at(&p, 132 * MS);
    frame_at(&p, 216 * MS); /* 100 ms of 500: 0.8 */
    AX_CHECK_NEAR_F(opacity_of(&p, p.card[0]), 0.8f, 1e-5f);
    before = opacity_of(&p, p.card[0]);
    /* Back to 1 at 216 ms (the next frame's latch): the first curve keeps going toward 0 while
     * the second adds the way back; the sum is continuous. */
    p.clock.now = 210 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 1.0f);
    commit(&p, t);
    frame_at(&p, 232 * MS);
    frame_at(&p, 216 * MS); /* a probe at the retarget's instant */
    after = opacity_of(&p, p.card[0]);
    AX_CHECK_NEAR_F(after, before, 1e-5f);
    /* 1 + (-1)(1 - 0.2 - 0.2) + (0 - 1)... : at 316 ms, 0.4 - 0.2 + ... checked through its rest */
    frame_at(&p, 800 * MS);
    AX_CHECK(opacity_of(&p, p.card[0]) == 1.0f);
    pipe_stop(&p);
}

/* ---- Opting out, reduce motion, lookup ----------------------------------------------------------
 */

AX_TEST(not_animated_transactions_snap_and_stop_running_animations)
{
    static pipe p;
    ax_txn *t;
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_f32(t, p.card[1], AX_PROP_OPACITY, 0.0f);
    commit(&p, t);
    frame_at(&p, 132 * MS);
    AX_CHECK(opacity_of(&p, p.card[1]) > 0.0f);
    p.clock.now = 140 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_f32(t, p.card[1], AX_PROP_OPACITY, 0.5f);
    ax_txn_set_dimension(t, p.card[0], AX_PROP_HEIGHT, pt(60)); /* moves cards 1 and 2 */
    ax_txn_set_animated(t, false);
    commit(&p, t);
    frame_at(&p, 164 * MS);
    AX_CHECK(opacity_of(&p, p.card[1]) == 0.5f);
    AX_CHECK(frame_of(&p, p.card[1]).y == 20.0f + 60.0f + 10.0f);
    pipe_stop(&p);
}

AX_TEST(reduce_motion_makes_motion_instant_and_fades_the_rest)
{
    static pipe p;
    ax_txn *t;
    ax_transition bouncy = kind(AX_TRANSITION_SPRING);
    bouncy.duration = 0.6f;
    bouncy.bounce = 0.5f;
    AX_REQUIRE(pipe_start(&p));
    ax_world_set_reduce_motion(p.w, true);
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_transition(t, p.card[0], AX_PROP_OPACITY, &bouncy);
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 0.0f);
    ax_txn_set_dimension(t, p.card[0], AX_PROP_HEIGHT, pt(80));
    commit(&p, t);
    frame_at(&p, 132 * MS);
    AX_CHECK(frame_of(&p, p.card[1]).y == 20.0f + 80.0f + 10.0f); /* the frame: at once */
    AX_CHECK_NEAR_F(opacity_of(&p, p.card[0]), 1.0f - 16.0f / 150.0f, 1e-4f); /* a linear fade */
    frame_at(&p, 268 * MS);                                                   /* 116 + 150: done */
    AX_CHECK(opacity_of(&p, p.card[0]) == 0.0f);
    pipe_stop(&p);
}

AX_TEST(the_node_then_the_world_then_the_default_decides)
{
    static pipe p;
    ax_txn *t;
    ax_transition none = kind(AX_TRANSITION_NONE);
    ax_transition slow = kind(AX_TRANSITION_TIMING);
    ax_transition reset = kind(AX_TRANSITION_DEFAULT);
    slow.duration = 1.0f;
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_default_transition(t, AX_PROP_OPACITY, &slow);    /* the world: linear 1 s */
    ax_txn_set_transition(t, p.card[0], AX_PROP_OPACITY, &none); /* card 0: at once */
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 0.0f);
    ax_txn_set_f32(t, p.card[1], AX_PROP_OPACITY, 0.0f);
    commit(&p, t);
    frame_at(&p, 132 * MS);
    AX_CHECK(opacity_of(&p, p.card[0]) == 0.0f);
    AX_CHECK_NEAR_F(opacity_of(&p, p.card[1]), 1.0f - 0.016f, 1e-4f);
    /* Card 0's override removed, the world's default removed: the built-in spring. */
    p.clock.now = 140 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_transition(t, p.card[0], AX_PROP_OPACITY, &reset);
    ax_txn_set_default_transition(t, AX_PROP_OPACITY, &reset);
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 1.0f);
    commit(&p, t);
    frame_at(&p, 164 * MS);
    AX_CHECK(opacity_of(&p, p.card[0]) > 0.0f && opacity_of(&p, p.card[0]) < 0.5f);
    pipe_stop(&p);
}

AX_TEST(bouncy_springs_stay_in_the_valid_range)
{
    static pipe p;
    ax_txn *t;
    ax_transition bouncy = kind(AX_TRANSITION_SPRING);
    uint64_t ms;
    bool overshot = false;
    bouncy.duration = 0.3f;
    bouncy.bounce = 0.8f;
    AX_REQUIRE(pipe_start(&p));
    t = ax_txn_begin(p.w); /* rounded at once, so the radius has somewhere to fall from */
    ax_txn_set_f32(t, p.card[0], AX_PROP_CORNER_RADIUS, 12.0f);
    ax_txn_set_animated(t, false);
    commit(&p, t);
    frame_at(&p, 116 * MS);
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_transition(t, p.card[0], AX_PROP_OPACITY, &bouncy);
    ax_txn_set_transition(t, p.card[0], AX_PROP_CORNER_RADIUS, &bouncy);
    ax_txn_set_f32(t, p.card[0], AX_PROP_OPACITY, 0.0f);
    ax_txn_set_f32(t, p.card[0], AX_PROP_CORNER_RADIUS, 0.0f);
    commit(&p, t);
    /* First give them somewhere to bounce from. */
    frame_at(&p, 132 * MS);
    for (ms = 148; ms < 1200; ms += 4) {
        ax_value r;
        float o;
        frame_at(&p, ms * MS);
        o = opacity_of(&p, p.card[0]);
        (void)ax_world_presentation(p.w, p.card[0], AX_PROP_CORNER_RADIUS_TOP_LEFT, &r);
        AX_CHECK(o >= 0.0f && o <= 1.0f);
        AX_CHECK(r.f32 >= 0.0f);
        overshot = overshot || o == 0.0f; /* clamped where the spring went below 0 */
    }
    AX_CHECK(overshot);
    pipe_stop(&p);
}

/* ---- Layout animations
 * ------------------------------------------------------------------------------ */

AX_TEST(reordered_cards_move_to_their_new_place)
{
    static pipe p;
    ax_txn *t;
    ax_rect r;
    ax_frame_counters fc;
    uint64_t ms;
    AX_REQUIRE(pipe_start(&p));
    /* Card 2 moves first: every card's frame changes; each animates from where it was. */
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_insert_before(t, p.list, p.card[2], p.card[0]);
    commit(&p, t);
    frame_at(&p, 132 * MS);
    r = frame_of(&p, p.card[2]);
    AX_CHECK(r.y < 20.0f + 2.0f * 50.0f && r.y > 20.0f); /* on its way up */
    r = frame_of(&p, p.card[0]);
    AX_CHECK(r.y > 20.0f && r.y < 70.0f); /* on its way down */
    ax_world_get_frame_counters(p.w, &fc, true);
    for (ms = 148; ms < 1000; ms += 16) {
        frame_at(&p, ms * MS);
    }
    ax_world_get_frame_counters(p.w, &fc, true);
    AX_CHECK_EQ_U(fc.items_recorded, 0); /* moving cards are layers: no item recorded again */
    AX_CHECK(frame_of(&p, p.card[2]).y == 20.0f);
    AX_CHECK(frame_of(&p, p.card[0]).y == 70.0f);
    /* The window resizes: frames change at once. */
    (void)ax_world_set_root_viewport(p.w, p.root, 500, 300, 1.0f);
    t = ax_txn_begin(p.w);
    ax_txn_set_dimension(t, p.list, AX_PROP_PADDING_TOP, pt(30));
    ax_txn_set_animated(t, false);
    commit(&p, t);
    frame_at(&p, 1100 * MS);
    AX_CHECK(frame_of(&p, p.card[2]).y == 30.0f);
    pipe_stop(&p);
}

/* ---- Determinism
 * ---------------------------------------------------------------------------------------- */

static void scripted(pipe *p)
{
    ax_txn *t;
    ax_transition ease = kind(AX_TRANSITION_TIMING);
    uint64_t ms;
    ease.duration = 0.3f;
    ease.curve[0] = 0.4f;
    ease.curve[2] = 0.2f;
    ease.curve[3] = 1.0f;
    p->clock.now = 110 * MS;
    t = ax_txn_begin(p->w);
    ax_txn_set_transition(t, p->card[1], AX_PROP_BACKGROUND_COLOR, &ease);
    ax_txn_set_color(t, p->card[1], AX_PROP_BACKGROUND_COLOR, ax_rgba_from_srgb(0.2f, 0.4f, 1, 1));
    ax_txn_set_transform(t, p->card[0], AX_PROP_TRANSFORM, (ax_transform2d){0, 1, -1, 0, 5, 0});
    ax_txn_insert_before(t, p->list, p->card[2], p->card[1]);
    commit(p, t);
    for (ms = 132; ms < 700; ms += 17) {
        if (ms == 302) {
            p->clock.now = ms * MS;
            t = ax_txn_begin(p->w);
            ax_txn_set_f32(t, p->card[2], AX_PROP_OPACITY, 0.3f);
            ax_txn_set_dimension(t, p->card[0], AX_PROP_WIDTH, pt(160));
            commit(p, t);
        }
        frame_at(p, ms * MS);
    }
}

AX_TEST(the_same_sequence_gives_the_same_bits)
{
    static pipe a, b;
    uint64_t ms;
    AX_REQUIRE(pipe_start(&a));
    AX_REQUIRE(pipe_start(&b));
    scripted(&a);
    scripted(&b);
    /* Every frame's IR, compared by its dump (%g prints every float's value). */
    for (ms = 700; ms < 900; ms += 23) {
        frame_at(&a, ms * MS);
        frame_at(&b, ms * MS);
        AX_CHECK(strcmp(a.dump, b.dump) == 0);
    }
    pipe_stop(&a);
    pipe_stop(&b);
}

AX_TEST(a_rotation_animates_through_its_decomposition_on_the_fast_path)
{
    static pipe p;
    ax_txn *t;
    ax_frame_counters fc;
    ax_value v;
    uint64_t ms;
    const ax_transform2d turned = {0.0f, 1.0f, -1.0f, 0.0f, 12.0f, -4.0f}; /* 90 degrees */
    AX_REQUIRE(pipe_start(&p));
    p.clock.now = 110 * MS;
    t = ax_txn_begin(p.w);
    ax_txn_set_transform(t, p.card[1], AX_PROP_TRANSFORM, turned);
    commit(&p, t);
    frame_at(&p, 132 * MS);
    ax_world_get_frame_counters(p.w, &fc, true);
    frame_at(&p, 200 * MS);
    /* Mid-way: a rotation (orthonormal columns), not a shrinking blend of matrices. */
    AX_CHECK_RESULT(ax_world_presentation(p.w, p.card[1], AX_PROP_TRANSFORM, &v), AX_OK);
    AX_CHECK_NEAR_F(v.transform2d.a * v.transform2d.a + v.transform2d.b * v.transform2d.b, 1.0f,
                    1e-4f);
    AX_CHECK(v.transform2d.b > 0.1f && v.transform2d.b < 0.99f);
    for (ms = 216; ms < 1200; ms += 16) {
        frame_at(&p, ms * MS);
    }
    ax_world_get_frame_counters(p.w, &fc, true);
    AX_CHECK_EQ_U(fc.items_recorded, 0);
    AX_CHECK_RESULT(ax_world_presentation(p.w, p.card[1], AX_PROP_TRANSFORM, &v), AX_OK);
    AX_CHECK(memcmp(&v.transform2d, &turned, sizeof turned) == 0); /* the target, exactly */
    pipe_stop(&p);
}
