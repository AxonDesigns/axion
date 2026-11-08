/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Text leaves in the frame (decision 99), before layout:
 *   1. the style pass resolves inherited text values (ax_scene_resolve_styles);
 *   2. each node with TEXT whose content or layout-affecting style changed gets a new paragraph,
 *      built from its content and computed style (the font matched by family, weight, style and
 *      width; the features parsed);
 *   3. the new paragraphs are shaped in frame jobs, in parallel, so HarfBuzz runs on the workers;
 *   4. each paragraph becomes its node's measure function's user data (decision 90's contract:
 *      the paragraph is immutable once shaped, so measuring is pure, thread-safe and touches
 *      nothing of the store).
 */
#include <math.h>
#include <string.h>

#include "world_internal.h"

#include <axion/base/ax_jobs.h>
#include <axion/base/ax_memory.h>
#include <axion/core/ax_layout.h>
#include <axion/core/ax_text_node.h>
#include <axion/text/ax_text.h>

void ax_world_set_text_system(ax_world world, struct ax_text_system *text)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_set_text_system: not the scene thread");
    if (w == NULL || w->text == text) {
        return;
    }
    if (w->text_owned) {
        AX_ASSERT_MSG(w->scene.store.components[w->scene.text_component].count == 0 ||
                          text == NULL,
                      "ax_world_set_text_system: set it before the first text");
        ax_text_system_destroy(w->text);
    }
    w->text = text;
    w->text_owned = false;
}

static ax_text_system *text_system(world_impl *w)
{
    if (w->text == NULL) {
        ax_text_system_desc d;
        memset(&d, 0, sizeof d);
        d.allocator = &w->allocator;
        if (ax_text_system_create(&d, &w->text) == AX_OK) {
            w->text_owned = true;
        }
    }
    return w->text;
}

ax_text_system *ax_world_text_system(ax_world world)
{
    world_impl *w = ax__world_from_handle(world);
    AX_ASSERT(w != NULL);
    AX_ASSERT_MSG(ax__world_on_scene_thread(w), "ax_world_text_system: not the scene thread");
    return w != NULL ? text_system(w) : NULL;
}

void ax__text_free(world_impl *w)
{
    if (w->text_owned && w->text != NULL) {
        ax_text_system_destroy(w->text);
    }
    w->text = NULL;
    w->text_owned = false;
    ax_mem_free(&w->allocator, w->text_batch,
                sizeof(ax_text_paragraph *) * w->text_batch_capacity);
    w->text_batch = NULL;
    w->text_batch_capacity = 0;
}

/* ---- The measure function -------------------------------------------------------------------- */

static void text_measure(void *user, ax_node node, float width, ax_layout_mode width_mode,
                         float height, ax_layout_mode height_mode, float *out_width,
                         float *out_height)
{
    ax_text_paragraph *p = (ax_text_paragraph *)user;
    float w = 0.0f, h = 0.0f;
    (void)node;
    ax_text_paragraph_measure(p, width_mode == AX_LAYOUT_UNDEFINED ? NAN : width, &w, &h);
    *out_width = width_mode == AX_LAYOUT_EXACT ? width : w;
    *out_height = height_mode == AX_LAYOUT_EXACT ? height : h;
}

/* ---- Building paragraphs --------------------------------------------------------------------- */

static ax_text_paragraph *build(ax_text_system *ts, const ax_node_text *t)
{
    const ax_text_computed *c = &t->computed;
    ax_font_query q;
    ax_text_style style;
    ax_text_paragraph_desc d;
    ax_text_paragraph *p = NULL;
    ax_str content = ax_shared_str_view(t->content);
    memset(&q, 0, sizeof q);
    q.family = ax_shared_str_view(c->family);
    q.weight = c->weight;
    q.style = c->style == AX_FONT_STYLE_VALUE_ITALIC ? AX_FONT_STYLE_ITALIC : AX_FONT_STYLE_NORMAL;
    q.width = c->width;
    memset(&style, 0, sizeof style);
    if (ax_text_match_font(ts, &q, &style.font) != AX_OK) {
        return NULL;
    }
    style.size = c->size;
    style.line_height = c->line_height;
    style.feature_count = ax_text_parse_features(ax_shared_str_view(c->features), style.features,
                                                 AX_TEXT_MAX_FEATURES, NULL);
    memset(&d, 0, sizeof d);
    d.text = content.ptr;
    d.length = content.len;
    d.styles = &style;
    d.style_count = 1;
    switch (c->align) {
    case AX_TEXT_ALIGN_VALUE_CENTER:
        d.align = AX_TEXT_ALIGN_CENTER;
        break;
    case AX_TEXT_ALIGN_VALUE_END:
        d.align = AX_TEXT_ALIGN_END;
        break;
    default:
        d.align = AX_TEXT_ALIGN_START;
        break;
    }
    if (ax_text_paragraph_create(ts, &d, &p) != AX_OK) {
        return NULL;
    }
    return p;
}

static void shape_job(void *arg)
{
    (void)ax_text_paragraph_shape((ax_text_paragraph *)arg);
}

void ax__text_prepare(world_impl *w)
{
    ax_scene *scene = &w->scene;
    ax_node_store *s = &scene->store;
    ax_component_storage *cs;
    ax_text_system *ts;
    uint32_t i, batch = 0;
    if (s->components[scene->text_component].count == 0) {
        return; /* no text anywhere */
    }
    (void)ax_scene_resolve_styles(scene);
    ts = text_system(w);
    if (ts == NULL) {
        return;
    }
    cs = &s->components[scene->text_component];
    for (i = 0; i < cs->count; ++i) {
        ax_node_text *t = (ax_node_text *)(void *)(cs->data + cs->stride * i);
        const uint32_t slot = cs->owner_slot[i];
        const ax_node node = ax_node_store_handle(s, slot);
        ax_text_paragraph *p;
        if (!t->paragraph_stale) {
            continue;
        }
        t->paragraph_stale = false;
        if ((t->own & AX_TEXT_PROP_BIT(AX_PROP_TEXT)) == 0) {
            /* No longer a text leaf. */
            if (t->paragraph != NULL) {
                (void)ax_layout_set_measure(scene, node, NULL, NULL);
                ax_text_paragraph_release(t->paragraph);
                t->paragraph = NULL;
            }
            continue;
        }
        p = build(ts, t);
        if (p == NULL) {
            t->paragraph_stale = true; /* try again next frame */
            continue;
        }
        if (ax_layout_set_measure(scene, node, text_measure, p) != AX_OK) {
            ax_text_paragraph_release(p);
            t->paragraph_stale = true;
            continue;
        }
        /* The component array does not move during this loop: nothing adds or removes text
         * components here (ax_layout_set_measure uses another component). */
        if (t->paragraph != NULL) {
            ax_text_paragraph_release(t->paragraph);
        }
        t->paragraph = p;
        if (batch == w->text_batch_capacity) {
            uint32_t cap = w->text_batch_capacity < 64u ? 64u : w->text_batch_capacity * 2u;
            ax_text_paragraph **fresh = (ax_text_paragraph **)ax_mem_alloc(
                &w->allocator, sizeof(ax_text_paragraph *) * cap, AX_ALIGNOF(void *));
            if (fresh == NULL) {
                (void)ax_text_paragraph_shape(p); /* out of memory: shape it here */
                continue;
            }
            if (w->text_batch != NULL) {
                memcpy(fresh, w->text_batch, sizeof(ax_text_paragraph *) * batch);
                ax_mem_free(&w->allocator, w->text_batch,
                            sizeof(ax_text_paragraph *) * w->text_batch_capacity);
            }
            w->text_batch = fresh;
            w->text_batch_capacity = cap;
        }
        w->text_batch[batch++] = p;
    }
    if (batch == 0) {
        return;
    }
    /* Shaping on the workers, before layout measures. */
    if (w->jobs != NULL && batch > 1u) {
        ax_job_counter done = AX_JOB_COUNTER_INIT;
        uint32_t k = 0;
        while (k < batch) {
            ax_job jobs[64];
            uint32_t n = 0;
            while (n < 64u && k < batch) {
                jobs[n].fn = shape_job;
                jobs[n].arg = w->text_batch[k++];
                n++;
            }
            ax_jobs_submit(w->jobs, jobs, n, &done);
        }
        ax_jobs_wait(w->jobs, &done);
    } else {
        for (i = 0; i < batch; ++i) {
            (void)ax_text_paragraph_shape(w->text_batch[i]);
        }
    }
}
