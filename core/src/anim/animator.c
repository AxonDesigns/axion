/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * The animator (decision 94): implicit animations of paint, composite and frame channels, started
 * when a transaction or a layout changes a target, sampled at each frame's predicted present time.
 */
#include <math.h>
#include <string.h>

#include "../world_internal.h"
#include "anim_internal.h"

#include <axion/base/ax_assert.h>
#include <axion/base/ax_math.h>
#include <axion/base/ax_memory.h>

#define PI_F 3.14159265f

static const uint8_t COMPONENTS[AXA_CHANNELS] = {4, 4, 1, 1, 1, 1, 1, 6};

/* Rest thresholds per component: displacement and velocity (per second). */
static float rest_x(int ch, int k)
{
    switch (ch) {
    case AXA_FRAME:
        return 0.01f;
    case AXA_BACKGROUND:
    case AXA_OPACITY:
        return 1e-4f;
    case AXA_TRANSFORM:
        return k < 2 ? 0.01f : 1e-4f;
    default:
        return 0.01f; /* radii */
    }
}

size_t ax__anim_node_size(void);
size_t ax__anim_node_size(void)
{
    return sizeof(axa_node);
}

AX_STATIC_ASSERT(sizeof(axa_transitions) == sizeof(uint16_t) * 8u, "scene.c registers this size");

int axa_channel_of(ax_prop prop)
{
    switch (prop) {
    case AX_PROP_LAYOUT:
        return AXA_FRAME;
    case AX_PROP_BACKGROUND_COLOR:
        return AXA_BACKGROUND;
    case AX_PROP_CORNER_RADIUS_TOP_LEFT:
    case AX_PROP_CORNER_RADIUS_TOP_RIGHT:
    case AX_PROP_CORNER_RADIUS_BOTTOM_RIGHT:
    case AX_PROP_CORNER_RADIUS_BOTTOM_LEFT:
        return AXA_RADIUS + (int)(prop - AX_PROP_CORNER_RADIUS_TOP_LEFT);
    case AX_PROP_OPACITY:
        return AXA_OPACITY;
    case AX_PROP_TRANSFORM:
        return AXA_TRANSFORM;
    default:
        return -1;
    }
}

/* ---- Transforms: CSS's 2D decomposition --------------------------------------------------- */

void axa_decompose(ax_affine2 m, float out[AXA_MAX_COMPONENTS])
{
    float r0x = m.a, r0y = m.b, r1x = m.c, r1y = m.d;
    float sx = sqrtf(r0x * r0x + r0y * r0y);
    float sy, skew;
    out[0] = m.tx;
    out[1] = m.ty;
    if (sx > 0.0f) {
        r0x /= sx;
        r0y /= sx;
    }
    skew = r0x * r1x + r0y * r1y;
    r1x -= r0x * skew;
    r1y -= r0y * skew;
    sy = sqrtf(r1x * r1x + r1y * r1y);
    if (sy > 0.0f) {
        r1x /= sy;
        r1y /= sy;
        skew /= sy;
    }
    if (r0x * r1y - r0y * r1x < 0.0f) {
        sx = -sx;
        r0x = -r0x;
        r0y = -r0y;
    }
    out[2] = ax_atan2f(r0y, r0x);
    out[3] = sx;
    out[4] = sy;
    out[5] = skew;
}

ax_affine2 axa_recompose(const float c[AXA_MAX_COMPONENTS])
{
    float s, co;
    ax_affine2 m;
    ax_sin_cos(c[2], &s, &co);
    /* translate * rotate * skew(x by y) * scale */
    m.a = co * c[3];
    m.b = s * c[3];
    m.c = (co * c[5] - s) * c[4];
    m.d = (s * c[5] + co) * c[4];
    m.tx = c[0];
    m.ty = c[1];
    return m;
}

/* ---- The world's state --------------------------------------------------------------------- */

typedef struct frame_change {
    uint32_t slot;
    uint32_t generation;
    ax_rect prev;         /* the target frame before the first change since the last sample */
    uint32_t prev_parent; /* its parent then */
    bool had_frame;
} frame_change;

struct anim_state {
    bool enabled; /* the first ax_world_animate starts it: the first content never animates */
    ax_curve *curves;
    uint32_t curve_count, curve_capacity;
    uint16_t defaults[AXA_CHANNELS]; /* the world's transitions: curve + 1, 0: built-in */
    uint16_t builtin_spring, builtin_ease, builtin_none, reduced_fade;
    uint64_t change_time; /* when the changes being applied start */
    uint64_t last_time;   /* the last sample */
    bool any_animated;    /* an animated transaction applied since the last sample */
    bool viewport_changed;
    frame_change *changes;
    uint32_t change_count, change_capacity;
    ax_anim_counters counters;
};

static anim_state *state(world_impl *w)
{
    if (w->anim == NULL) {
        anim_state *a =
            (anim_state *)ax_mem_alloc(&w->allocator, sizeof *a, AX_ALIGNOF(anim_state));
        if (a == NULL) {
            return NULL;
        }
        memset(a, 0, sizeof *a);
        w->anim = a;
    }
    return w->anim;
}

void ax__anim_free(world_impl *w)
{
    anim_state *a = w->anim;
    if (a == NULL) {
        return;
    }
    ax_mem_free(&w->allocator, a->curves, sizeof(ax_curve) * a->curve_capacity);
    ax_mem_free(&w->allocator, a->changes, sizeof(frame_change) * a->change_capacity);
    ax_mem_free(&w->allocator, a, sizeof *a);
    w->anim = NULL;
}

static bool grow(world_impl *w, void **p, uint32_t *cap, uint32_t need, size_t elem)
{
    uint32_t c = *cap < 16u ? 16u : *cap;
    void *fresh;
    if (need <= *cap) {
        return true;
    }
    while (c < need) {
        c *= 2u;
    }
    fresh = ax_mem_alloc(&w->allocator, elem * c, AX_MAX_ALIGN);
    if (fresh == NULL) {
        return false;
    }
    if (*p != NULL) {
        memcpy(fresh, *p, elem * *cap);
        ax_mem_free(&w->allocator, *p, elem * *cap);
    }
    *p = fresh;
    *cap = c;
    return true;
}

/* A curve's id + 1, interned (curves are few: the defaults and the app's own). 0 when full. */
static uint16_t intern(world_impl *w, anim_state *a, const ax_curve *c)
{
    uint32_t i;
    for (i = 0; i < a->curve_count; ++i) {
        if (memcmp(&a->curves[i], c, sizeof *c) == 0) {
            return (uint16_t)(i + 1u);
        }
    }
    if (a->curve_count >= 65535u ||
        !grow(w, (void **)&a->curves, &a->curve_capacity, a->curve_count + 1u, sizeof(ax_curve))) {
        return 0;
    }
    a->curves[a->curve_count] = *c;
    return (uint16_t)++a->curve_count;
}

static const ax_curve *curve_at(const anim_state *a, uint16_t id)
{
    return &a->curves[id - 1u];
}

static void builtins(world_impl *w, anim_state *a)
{
    if (a->builtin_spring == 0) {
        ax_curve fade;
        ax_curve spring = ax_curve_default_spring();
        ax_curve ease = ax_curve_default_ease();
        ax_curve none = ax_curve_none();
        memset(&fade, 0, sizeof fade);
        fade.kind = AX_TRANSITION_TIMING;
        fade.easing = AX_EASING_CUBIC_BEZIER; /* all zeros: linear */
        fade.duration = 0.15f;
        a->builtin_spring = intern(w, a, &spring);
        a->builtin_ease = intern(w, a, &ease);
        a->builtin_none = intern(w, a, &none);
        a->reduced_fade = intern(w, a, &fade);
    }
}

/* The transition of a node's channel: its own, the world's, the built-in default; reduce motion
 * makes motion instant and other channels a short linear fade. */
static uint16_t lookup(world_impl *w, anim_state *a, uint32_t slot, int ch)
{
    ax_scene *scene = &w->scene;
    const axa_transitions *own = (const axa_transitions *)ax_node_store_peek_component(
        &scene->store, slot, scene->transitions_component);
    uint16_t id = 0;
    builtins(w, a);
    if (own != NULL && own->curve[ch] != 0) {
        id = own->curve[ch];
    } else if (a->defaults[ch] != 0) {
        id = a->defaults[ch];
    } else {
        id = (ch == AXA_FRAME || ch == AXA_OPACITY || ch == AXA_TRANSFORM) ? a->builtin_spring
                                                                           : a->builtin_ease;
    }
    if (ax_atomic_load_u32(&w->reduce_motion, AX_MO_RELAXED) != 0 &&
        curve_at(a, id)->kind != AX_TRANSITION_NONE) {
        id = (ch == AXA_FRAME || ch == AXA_TRANSFORM) ? a->builtin_none : a->reduced_fade;
    }
    return id;
}

/* ---- Transitions from transactions ---------------------------------------------------------- */

void ax__anim_set_transition(world_impl *w, ax_node node, ax_prop prop, const ax_curve *c)
{
    anim_state *a = state(w);
    int ch = axa_channel_of(prop);
    uint16_t id;
    if (a == NULL || ch < 0) {
        return; /* properties registered at run time do not animate yet */
    }
    id = c->kind == AX_TRANSITION_DEFAULT ? 0 : intern(w, a, c);
    if (AX_HANDLE_IS_NULL(node)) {
        a->defaults[ch] = id;
        return;
    }
    {
        ax_scene *scene = &w->scene;
        axa_transitions *own = (axa_transitions *)ax_node_store_get_component(
            &scene->store, node, scene->transitions_component);
        if (own == NULL) {
            if (id == 0) {
                return;
            }
            if (ax_node_store_add_component(&scene->store, node, scene->transitions_component,
                                            (void **)&own) != AX_OK) {
                return; /* out of memory: the defaults apply */
            }
        }
        own->curve[ch] = id;
    }
}

/* ---- Tracks --------------------------------------------------------------------------------- */

static float seconds(uint64_t from, uint64_t to)
{
    return to > from ? (float)((double)(to - from) * 1e-9) : 0.0f;
}

/* Samples a track at time `t`: its offset and velocity per component. True when it has come to
 * rest (it is then idle and its offset 0). */
static bool sample(const anim_state *a, axa_track *tr, int ch, uint64_t t)
{
    const int n = COMPONENTS[ch];
    int k, i;
    bool rest = true;
    if (tr->kind == 0) {
        memset(tr->off, 0, sizeof tr->off);
        memset(tr->vel, 0, sizeof tr->vel);
        return true;
    }
    if (tr->kind == AX_TRANSITION_TIMING) {
        uint32_t live = 0;
        memset(tr->off, 0, sizeof tr->off);
        memset(tr->vel, 0, sizeof tr->vel);
        for (i = 0; i < tr->count; ++i) {
            float p, rate;
            bool done =
                ax_curve_progress(curve_at(a, tr->curve[i]), seconds(tr->t0[i], t), &p, &rate);
            for (k = 0; k < n; ++k) {
                tr->off[k] += tr->d[i][k] * (1.0f - p);
                tr->vel[k] -= tr->d[i][k] * rate;
            }
            if (!done) {
                if ((uint32_t)i != live) {
                    tr->curve[live] = tr->curve[i];
                    tr->t0[live] = tr->t0[i];
                    memcpy(tr->d[live], tr->d[i], sizeof tr->d[i]);
                }
                live++;
            }
        }
        tr->count = (uint8_t)live;
        if (live == 0) {
            tr->kind = 0;
            memset(tr->off, 0, sizeof tr->off);
            memset(tr->vel, 0, sizeof tr->vel);
            return true;
        }
        return false;
    }
    {
        const ax_curve *c = curve_at(a, tr->curve[0]);
        float dt = seconds(tr->t0[0], t) - c->delay;
        if (dt < 0.0f) {
            dt = 0.0f;
        }
        for (k = 0; k < n; ++k) {
            if (tr->kind == AX_TRANSITION_SPRING) {
                ax_spring_eval(c->stiffness, c->damping, c->mass, tr->d[0][k], tr->v0[k], dt,
                               &tr->off[k], &tr->vel[k]);
            } else {
                ax_decay_eval(c->rate, tr->d[0][k], dt, &tr->off[k], &tr->vel[k]);
            }
            if (fabsf(tr->off[k]) > rest_x(ch, k) || fabsf(tr->vel[k]) > 10.0f * rest_x(ch, k)) {
                rest = false;
            }
        }
    }
    if (rest) {
        tr->kind = 0;
        memset(tr->off, 0, sizeof tr->off);
        memset(tr->vel, 0, sizeof tr->vel);
    }
    return rest;
}

/* A channel's target moved from `old_target` to `new_target` at time t: retarget its track so the
 * presentation value does not jump (springs keep their velocity; timing curves add up). */
static void retarget(world_impl *w, anim_state *a, axa_node *an, int ch, uint16_t curve_id,
                     const float *old_target, const float *new_target, uint64_t t)
{
    axa_track *tr = &an->track[ch];
    const ax_curve *c = curve_at(a, curve_id);
    const int n = COMPONENTS[ch];
    float x0[AXA_MAX_COMPONENTS];
    int k;
    (void)w;
    (void)sample(a, tr, ch, t); /* the current offset and velocity */
    for (k = 0; k < n; ++k) {
        x0[k] = old_target[k] + tr->off[k] - new_target[k];
    }
    if (ch == AXA_TRANSFORM) {
        /* The shortest way round. */
        while (x0[2] > PI_F) {
            x0[2] -= 2.0f * PI_F;
        }
        while (x0[2] < -PI_F) {
            x0[2] += 2.0f * PI_F;
        }
    }
    if (c->kind == AX_TRANSITION_NONE) {
        memset(tr, 0, sizeof *tr);
        an->active &= ~(1u << ch);
        a->counters.snapped++;
        return;
    }
    if (tr->kind != 0) {
        a->counters.retargeted++;
    } else {
        a->counters.started++;
    }
    if (c->kind == AX_TRANSITION_TIMING) {
        if (tr->kind == AX_TRANSITION_TIMING) {
            /* Blend: the running curves keep their deltas; the new one adds this change's. */
            if (tr->count == AXA_CONTRIBUTIONS) {
                /* Fold the oldest into what remains of the next: the value stays continuous. */
                float p, rate;
                (void)ax_curve_progress(curve_at(a, tr->curve[0]), seconds(tr->t0[0], t), &p,
                                        &rate);
                for (k = 0; k < n; ++k) {
                    tr->d[1][k] += tr->d[0][k] * (1.0f - p);
                }
                memmove(&tr->curve[0], &tr->curve[1], sizeof tr->curve[0] * 2u);
                memmove(&tr->t0[0], &tr->t0[1], sizeof tr->t0[0] * 2u);
                memmove(&tr->d[0], &tr->d[1], sizeof tr->d[0] * 2u);
                tr->count--;
                a->counters.folded++;
            }
            for (k = 0; k < n; ++k) {
                tr->d[tr->count][k] = old_target[k] - new_target[k];
            }
            if (ch == AXA_TRANSFORM) {
                float dd = tr->d[tr->count][2];
                while (dd > PI_F) {
                    dd -= 2.0f * PI_F;
                }
                while (dd < -PI_F) {
                    dd += 2.0f * PI_F;
                }
                tr->d[tr->count][2] = dd;
            }
        } else {
            /* From a spring or rest: one curve from the current value. */
            tr->count = 0;
            memcpy(tr->d[0], x0, sizeof x0);
        }
        tr->curve[tr->count] = curve_id;
        tr->t0[tr->count] = t;
        tr->count++;
        tr->kind = AX_TRANSITION_TIMING;
    } else {
        /* Spring or decay: from the current offset, keeping the velocity (a spring's). */
        memcpy(tr->d[0], x0, sizeof x0);
        memcpy(tr->v0, tr->vel, sizeof tr->v0);
        if (c->kind == AX_TRANSITION_DECAY) {
            memset(tr->v0, 0, sizeof tr->v0);
        }
        tr->curve[0] = curve_id;
        tr->t0[0] = t;
        tr->count = 1;
        tr->kind = c->kind;
    }
    for (k = 0; k < n; ++k) {
        tr->off[k] = x0[k];
    }
    an->active |= 1u << ch;
}

static axa_node *anim_node(world_impl *w, ax_node node, bool create)
{
    ax_scene *scene = &w->scene;
    axa_node *an =
        (axa_node *)ax_node_store_get_component(&scene->store, node, scene->anim_component);
    if (an == NULL && create) {
        if (ax_node_store_add_component(&scene->store, node, scene->anim_component, (void **)&an) !=
            AX_OK) {
            return NULL;
        }
        memset(an, 0, sizeof *an);
    }
    return an;
}

/* The target values of a channel, as components. */
static void target_of(const ax_node_store *s, uint32_t row, int ch, float out[AXA_MAX_COMPONENTS])
{
    memset(out, 0, sizeof(float) * AXA_MAX_COMPONENTS);
    switch (ch) {
    case AXA_FRAME:
        memcpy(out, &s->layout[row].frame, sizeof(ax_rect));
        break;
    case AXA_BACKGROUND:
        memcpy(out, &s->paint[row].background, sizeof(ax_color));
        break;
    case AXA_OPACITY:
        out[0] = s->composite[row].opacity;
        break;
    case AXA_TRANSFORM:
        axa_decompose(s->composite[row].transform, out);
        break;
    default:
        out[0] = s->paint[row].corner_radius[ch - AXA_RADIUS];
        break;
    }
}

static void value_components(int ch, const ax_value *v, float out[AXA_MAX_COMPONENTS])
{
    memset(out, 0, sizeof(float) * AXA_MAX_COMPONENTS);
    switch (ch) {
    case AXA_BACKGROUND:
        memcpy(out, &v->color, sizeof(ax_color));
        break;
    case AXA_TRANSFORM:
        axa_decompose(v->transform2d, out);
        break;
    default:
        out[0] = v->f32;
        break;
    }
}

void ax__anim_txn_begin(world_impl *w, uint64_t deadline)
{
    anim_state *a = state(w);
    if (a != NULL) {
        a->change_time = deadline != UINT64_MAX ? deadline : a->last_time;
    }
}

void ax__anim_txn_applied(world_impl *w, bool animated)
{
    anim_state *a = w->anim;
    if (a != NULL && animated) {
        a->any_animated = true;
    }
}

void ax__anim_on_set(world_impl *w, uint64_t seq, ax_node node, ax_prop prop, const ax_value *old,
                     bool animated)
{
    anim_state *a = w->anim;
    const ax_node_store *s = &w->scene.store;
    uint32_t row = ax_node_store_row(s, node);
    int ch = axa_channel_of(prop);
    float from[AXA_MAX_COMPONENTS], to[AXA_MAX_COMPONENTS];
    axa_node *an;
    uint16_t id;
    if (a == NULL || !a->enabled || ch < 0 || ch == AXA_FRAME || row == AX_NODE_NONE) {
        return;
    }
    an = anim_node(w, node, false);
    if (an != NULL && an->stamp_seq == seq && (an->stamp_mask & (1u << ch)) != 0) {
        return; /* this transaction's first change of the channel was handled */
    }
    value_components(ch, old, from);
    target_of(s, row, ch, to);
    if (memcmp(from, to, sizeof from) == 0) {
        return;
    }
    id = animated ? lookup(w, a, ax_handle_slot(node.h), ch) : 0;
    if (id == 0 || curve_at(a, id)->kind == AX_TRANSITION_NONE) {
        /* Instant: stop a running animation of the channel. */
        if (an != NULL && (an->active & (1u << ch)) != 0) {
            memset(&an->track[ch], 0, sizeof an->track[ch]);
            an->active &= ~(1u << ch);
            a->counters.snapped++;
        }
        return;
    }
    an = anim_node(w, node, true);
    if (an == NULL) {
        return; /* out of memory: the change is instant */
    }
    if (an->stamp_seq != seq) {
        an->stamp_seq = seq;
        an->stamp_mask = 0;
    }
    an->stamp_mask |= 1u << ch;
    retarget(w, a, an, ch, id, from, to, a->change_time);
}

void ax__anim_viewport_changed(world_impl *w)
{
    anim_state *a = state(w);
    if (a != NULL) {
        a->viewport_changed = true;
    }
}

void ax__anim_on_frame_changed(world_impl *w, uint32_t slot)
{
    anim_state *a = w->anim;
    const ax_node_store *s = &w->scene.store;
    uint32_t row, i;
    frame_change *c;
    if (a == NULL || !a->enabled) {
        return;
    }
    row = s->slots[slot].row;
    for (i = a->change_count; i-- > 0;) {
        if (a->changes[i].slot == slot && a->changes[i].generation == s->slots[slot].generation) {
            return; /* the first change since the last sample keeps its old frame */
        }
    }
    if (!grow(w, (void **)&a->changes, &a->change_capacity, a->change_count + 1u,
              sizeof(frame_change))) {
        return;
    }
    c = &a->changes[a->change_count++];
    c->slot = slot;
    c->generation = s->slots[slot].generation;
    c->prev = s->layout[row].prev_frame;
    c->prev_parent = s->layout[row].prev_parent;
    c->had_frame = (s->layout[row].had_frame & 2u) != 0;
}

static bool animated_ok(bool animate, const frame_change *c, const ax_node_store *s, uint32_t row)
{
    return animate && c->had_frame && c->prev_parent == s->links[row].parent;
}

/* Whether a node's frame change animates: an animated transaction caused it, not a viewport's
 * resize, and the node was laid out before under the same parent. */
static uint16_t frame_curve(world_impl *w, anim_state *a, bool animate, const frame_change *c,
                            uint32_t row)
{
    const ax_node_store *s = &w->scene.store;
    uint16_t id;
    if (!animated_ok(animate, c, s, row)) {
        return 0;
    }
    id = lookup(w, a, c->slot, AXA_FRAME);
    return curve_at(a, id)->kind == AX_TRANSITION_NONE ? 0 : id;
}

/* Whether one of the node's ancestors starts resizing in this cycle (listed in `resizing`). */
static bool under_resizing(const ax_node_store *s, const uint32_t *resizing, uint32_t count,
                           uint32_t row)
{
    uint32_t p = s->links[row].parent;
    while (p != AX_NODE_NONE) {
        uint32_t i;
        for (i = 0; i < count; ++i) {
            if (resizing[i] == p) {
                return true;
            }
        }
        p = s->links[s->slots[p].row].parent;
    }
    return false;
}

/* Frame changes since the last sample. A node under an ancestor that starts resizing in the same
 * cycle follows it through the resize preset's relayout and does not animate its own change: a
 * layout animation's descendants animate relative to it (decision 94). */
static void frames(world_impl *w, anim_state *a)
{
    const ax_node_store *s = &w->scene.store;
    const bool animate = a->any_animated && !a->viewport_changed;
    uint32_t i, resizing_count = 0, resizing_cap = 0;
    uint32_t *resizing = NULL;
    for (i = 0; i < a->change_count; ++i) {
        const frame_change *c = &a->changes[i];
        ax_node node = ax_node_store_handle(s, c->slot);
        uint32_t row = s->slots[c->slot].generation == c->generation ? ax_node_store_row(s, node)
                                                                     : AX_NODE_NONE;
        if (row != AX_NODE_NONE && s->links[row].first_child != AX_NODE_NONE &&
            (c->prev.w != s->layout[row].frame.w || c->prev.h != s->layout[row].frame.h) &&
            frame_curve(w, a, animate, c, row) != 0 &&
            grow(w, (void **)&resizing, &resizing_cap, resizing_count + 1u, sizeof(uint32_t))) {
            resizing[resizing_count++] = c->slot;
        }
    }
    for (i = 0; i < a->change_count; ++i) {
        const frame_change *c = &a->changes[i];
        ax_node node = ax_node_store_handle(s, c->slot);
        uint32_t row;
        float from[AXA_MAX_COMPONENTS], to[AXA_MAX_COMPONENTS];
        axa_node *an;
        uint16_t id;
        if (s->slots[c->slot].generation != c->generation ||
            (row = ax_node_store_row(s, node)) == AX_NODE_NONE) {
            continue;
        }
        an = anim_node(w, node, false);
        id = frame_curve(w, a, animate, c, row);
        if (id != 0 && under_resizing(s, resizing, resizing_count, row)) {
            id = 0;
        }
        if (id == 0) {
            if (an != NULL && (an->active & 1u) != 0) {
                memset(&an->track[AXA_FRAME], 0, sizeof an->track[AXA_FRAME]);
                an->active &= ~1u;
                a->counters.snapped++;
            }
            continue;
        }
        an = anim_node(w, node, true);
        if (an == NULL) {
            continue;
        }
        memset(from, 0, sizeof from);
        memcpy(from, &c->prev, sizeof(ax_rect));
        target_of(s, row, AXA_FRAME, to);
        retarget(w, a, an, AXA_FRAME, id, from, to, a->change_time);
    }
    if (resizing != NULL) {
        ax_mem_free(&w->allocator, resizing, sizeof(uint32_t) * resizing_cap);
    }
    a->change_count = 0;
}

/* ---- Sampling ------------------------------------------------------------------------------- */

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* The presentation values of a node's paint and composite channels: target + offset, clamped to
 * their valid ranges (a bouncing spring never makes opacity leave 0..1 or a radius negative). */
static void present(const ax_node_store *s, uint32_t row, axa_node *an)
{
    const ax_node_paint *p = &s->paint[row];
    const ax_node_composite *c = &s->composite[row];
    int k;
    an->background = p->background;
    if (an->active & (1u << AXA_BACKGROUND)) {
        const float *o = an->track[AXA_BACKGROUND].off;
        an->background.r += o[0];
        an->background.g += o[1];
        an->background.b += o[2];
        an->background.a = clamp01(an->background.a + o[3]);
    }
    for (k = 0; k < 4; ++k) {
        float r = p->corner_radius[k];
        if (an->active & (1u << (AXA_RADIUS + k))) {
            r += an->track[AXA_RADIUS + k].off[0];
        }
        an->radii[k] = r > 0.0f ? r : 0.0f;
    }
    an->opacity = c->opacity;
    if (an->active & (1u << AXA_OPACITY)) {
        an->opacity = clamp01(c->opacity + an->track[AXA_OPACITY].off[0]);
    }
    an->transform = c->transform;
    if (an->active & (1u << AXA_TRANSFORM)) {
        float d[AXA_MAX_COMPONENTS];
        axa_decompose(c->transform, d);
        for (k = 0; k < AXA_MAX_COMPONENTS; ++k) {
            d[k] += an->track[AXA_TRANSFORM].off[k];
        }
        an->transform = axa_recompose(d);
    }
}

uint32_t ax__anim_step(world_impl *w, uint64_t t, void (*on_root)(world_impl *, uint32_t))
{
    anim_state *a = state(w);
    ax_scene *scene = &w->scene;
    ax_node_store *s = &scene->store;
    ax_component_storage *cs;
    uint32_t i, active = 0;
    if (a == NULL) {
        return 0;
    }
    a->enabled = true;
    builtins(w, a);
    frames(w, a);
    cs = &s->components[scene->anim_component];
    for (i = cs->count; i-- > 0;) {
        axa_node *an = (axa_node *)(void *)(cs->data + (size_t)i * cs->stride);
        const uint32_t slot = cs->owner_slot[i];
        const uint32_t row = s->slots[slot].row;
        const uint32_t was = an->active;
        int ch;
        a->counters.samples++;
        for (ch = 0; ch < AXA_CHANNELS; ++ch) {
            if ((an->active & (1u << ch)) != 0 && sample(a, &an->track[ch], ch, t)) {
                an->active &= ~(1u << ch);
                a->counters.finished++;
            }
        }
        present(s, row, an);
        /* What changed on screen: the frame and the composite only move layers; paint records the
         * node's items again. */
        if (was & ((1u << AXA_FRAME) | (1u << AXA_OPACITY) | (1u << AXA_TRANSFORM))) {
            s->dirty[row] |= AX_DIRTY_COMPOSITE;
        }
        if (was & ((1u << AXA_BACKGROUND) | (0xFu << AXA_RADIUS))) {
            s->dirty[row] |= AX_DIRTY_PAINT;
        }
        if (an->active == 0) {
            ax_node n = ax_node_store_handle(s, slot);
            (void)ax_node_store_remove_component(s, n, scene->anim_component);
            continue;
        }
        active++;
        if (on_root != NULL) {
            uint32_t top = slot;
            while (s->links[s->slots[top].row].parent != AX_NODE_NONE) {
                top = s->links[s->slots[top].row].parent;
            }
            on_root(w, top);
        }
    }
    a->any_animated = false;
    a->viewport_changed = false;
    if (t > a->last_time) {
        a->last_time = t;
    }
    a->counters.active = active;
    return active;
}

const axa_node *ax__anim_node(world_impl *w, uint32_t row)
{
    const ax_scene *scene = &w->scene;
    const ax_node_store *s = &scene->store;
    if (w->anim == NULL || (s->component_mask[row] & (1u << scene->anim_component)) == 0) {
        return NULL;
    }
    return (const axa_node *)ax_node_store_peek_component(s, s->slot_of_row[row],
                                                          scene->anim_component);
}

void ax__anim_get_counters(world_impl *w, ax_anim_counters *out, bool reset)
{
    memset(out, 0, sizeof *out);
    if (w->anim != NULL) {
        *out = w->anim->counters;
        if (reset) {
            uint32_t active = w->anim->counters.active;
            memset(&w->anim->counters, 0, sizeof w->anim->counters);
            w->anim->counters.active = active;
        }
    }
}

uint64_t ax__anim_time(world_impl *w)
{
    return w->anim != NULL && w->anim->enabled ? w->anim->last_time : 0;
}
