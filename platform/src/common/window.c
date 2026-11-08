/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * ax_window and ax_surface: handle table, main-thread entry points, snapshots, hit maps, and the
 * helpers backends use to publish configures and window state.
 */
#include <string.h>

#include "axion/base/ax_assert.h"
#include "axion/base/ax_memory.h"
#include "platform_internal.h"

static ax_atomic_u32 g_window_generation = AX_ATOMIC_INIT(1);

ax_window_impl *ax__window_from_handle(uint64_t raw)
{
    uint32_t slot = ax_handle_slot(raw);
    ax_app_impl *app;
    uint32_t index;
    ax_window_impl *w;

    if (raw == 0) {
        return NULL;
    }
    app = ax__app_from_slot(slot >> AX_WINDOW_INDEX_BITS);
    index = slot & ((1u << AX_WINDOW_INDEX_BITS) - 1u);
    if (app == NULL || index >= app->max_windows) {
        return NULL;
    }
    w = &app->windows[index];
    return ax_atomic_load_u64(&w->handle, AX_MO_ACQUIRE) == raw ? w : NULL;
}

static ax_result invalid_window(const char *fn)
{
    return ax_set_error(AX_ERR_INVALID_HANDLE, "%s: invalid or destroyed ax_window", fn);
}

uint32_t ax__pixels(float logical, float scale)
{
    float px = logical * scale + 0.5f;
    if (px < 1.0f) {
        return 1u;
    }
    return px >= 4294967040.0f ? 0xFFFFFF00u : (uint32_t)px;
}

/* ---- create / destroy --------------------------------------------------------------------------
 */

typedef struct create_ctx {
    ax_app_impl *app;
    const ax_window_desc *desc;
    uint64_t handle;
} create_ctx;

static ax_result create_on_main(void *p)
{
    create_ctx *ctx = (create_ctx *)p;
    ax_app_impl *app = ctx->app;
    ax_window_impl *w = NULL;
    uint32_t i;
    uint32_t generation;
    uint64_t raw;
    ax_result r;

    for (i = 0; i < app->max_windows; ++i) {
        if (!app->windows[i].open) {
            w = &app->windows[i];
            break;
        }
    }
    if (w == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY,
                            "ax_window_create: all %u windows of ax_app_desc.max_windows are open",
                            app->max_windows);
    }
    do {
        generation = ax_atomic_fetch_add_u32(&g_window_generation, 1, AX_MO_RELAXED);
    } while (generation == 0);
    raw = ax_handle_make((app->slot << AX_WINDOW_INDEX_BITS) | w->index, generation);

    w->open = true;
    w->flags = ctx->desc->flags;
    w->decorations = ctx->desc->decorations;
    w->backend = NULL;
    memset(&w->info, 0, sizeof w->info);
    w->info.width = ctx->desc->width;
    w->info.height = ctx->desc->height;
    w->info.scale = 1.0f;
    memset(w->hit, 0, sizeof w->hit);
    ax_tb_init(&w->hit_tb);
    memset(w->config, 0, sizeof w->config);
    ax_tb_init(&w->config_tb);
    ax_atomic_store_u32(&w->commit_requested, 0, AX_MO_RELAXED);
    w->presenter_acked_serial = 0;
    w->presenter_has_config = false;
    w->presenter_commit_id = 0;
    w->presented_commit_id = 0;
    w->presented_commit_ns = 0;
    ax_frame_clock_init(&w->clock, 0);

    ax_evq_window_open(&app->events, w->index, raw);
    r = app->ops->window_create(app, w, ctx->desc);
    if (r != AX_OK) {
        ax_evq_window_close(&app->events, w->index);
        w->open = false;
        return r;
    }
    /* Publish the handle last: other threads can only find the window once it is ready. */
    ax_atomic_store_u64(&w->handle, raw, AX_MO_RELEASE);
    ctx->handle = raw;
    return AX_OK;
}

ax_result ax_window_create(ax_app app, const ax_window_desc *desc, ax_window *out_window)
{
    ax_app_impl *impl = ax__app_from_handle(app);
    create_ctx ctx;
    ax_result r;

    if (out_window != NULL) {
        out_window->h = 0;
    }
    if (impl == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_window_create: invalid or destroyed ax_app");
    }
    if (desc == NULL || out_window == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_window_create: desc and out_window are "
                                                   "required");
    }
    if (desc->struct_size < sizeof(ax_window_desc)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_window_create: struct_size %u < %u",
                            desc->struct_size, (unsigned)sizeof(ax_window_desc));
    }
    ctx.app = impl;
    ctx.desc = desc;
    ctx.handle = 0;
    r = ax__app_call(impl, create_on_main, &ctx);
    out_window->h = ctx.handle;
    return r;
}

ax_result ax__window_destroy_on_main(ax_window_impl *w)
{
    ax_app_impl *app = w->app;
    AX_ASSERT_MSG(ax_atomic_load_u32(&w->presenting, AX_MO_ACQUIRE) == 0,
                  "ax_window_destroy: a thread is still presenting the window's surface");
    ax_atomic_store_u64(&w->handle, 0, AX_MO_RELEASE);
    app->ops->window_destroy(app, w);
    ax_evq_window_close(&app->events, w->index);
    w->backend = NULL;
    w->open = false;
    return AX_OK;
}

typedef struct window_ctx {
    uint64_t handle;
    const char *text;
    float width, height;
} window_ctx;

/* Re-validates on the main thread: the window may have been destroyed while the call waited. */
static ax_window_impl *window_on_main(const window_ctx *ctx)
{
    return ax__window_from_handle(ctx->handle);
}

static ax_result destroy_on_main(void *p)
{
    ax_window_impl *w = window_on_main((window_ctx *)p);
    if (w == NULL) {
        return invalid_window("ax_window_destroy");
    }
    return ax__window_destroy_on_main(w);
}

ax_result ax_window_destroy(ax_window window)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    window_ctx ctx;
    if (w == NULL) {
        return invalid_window("ax_window_destroy");
    }
    ctx.handle = window.h;
    return ax__app_call(w->app, destroy_on_main, &ctx);
}

/* ---- properties ---------------------------------------------------------------------------------
 */

static ax_result set_title_on_main(void *p)
{
    window_ctx *ctx = (window_ctx *)p;
    ax_window_impl *w = window_on_main(ctx);
    if (w == NULL) {
        return invalid_window("ax_window_set_title");
    }
    return w->app->ops->window_set_title(w, ctx->text);
}

ax_result ax_window_set_title(ax_window window, ax_str title)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    window_ctx ctx;
    char *copy;
    ax_result r;

    if (w == NULL) {
        return invalid_window("ax_window_set_title");
    }
    /* The backend wants a NUL-terminated string; build it here, on the calling thread. */
    copy = (char *)ax_mem_alloc(&w->app->allocator, title.len + 1, 1);
    if (copy == NULL) {
        return ax_set_error(AX_ERR_OUT_OF_MEMORY, "ax_window_set_title: out of memory");
    }
    if (title.len != 0) {
        memcpy(copy, title.ptr, title.len);
    }
    copy[title.len] = '\0';
    ctx.handle = window.h;
    ctx.text = copy;
    r = ax__app_call(w->app, set_title_on_main, &ctx);
    ax_mem_free(&w->app->allocator, copy, title.len + 1);
    return r;
}

static ax_result set_min_size_on_main(void *p)
{
    window_ctx *ctx = (window_ctx *)p;
    ax_window_impl *w = window_on_main(ctx);
    if (w == NULL) {
        return invalid_window("ax_window_set_min_size");
    }
    return w->app->ops->window_set_min_size(w, ctx->width, ctx->height);
}

ax_result ax_window_set_min_size(ax_window window, float width, float height)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    window_ctx ctx;
    if (w == NULL) {
        return invalid_window("ax_window_set_min_size");
    }
    ctx.handle = window.h;
    ctx.width = width;
    ctx.height = height;
    return ax__app_call(w->app, set_min_size_on_main, &ctx);
}

ax_result ax_window_get_surface(ax_window window, ax_surface *out_surface)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    if (out_surface == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_window_get_surface: out_surface is NULL");
    }
    out_surface->h = 0;
    if (w == NULL) {
        return invalid_window("ax_window_get_surface");
    }
    out_surface->h = window.h; /* a window's surface shares its handle value */
    return AX_OK;
}

ax_result ax_window_get_info(ax_window window, ax_window_info *out_info)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    if (out_info == NULL || out_info->struct_size < sizeof(ax_window_info)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_window_get_info: struct_size must be >= %u",
                            (unsigned)sizeof(ax_window_info));
    }
    if (w == NULL) {
        return invalid_window("ax_window_get_info");
    }
    ax_mutex_lock(&w->info_lock);
    out_info->width = w->info.width;
    out_info->height = w->info.height;
    out_info->scale = w->info.scale;
    out_info->server_decorated = w->info.server_decorated;
    out_info->focused = w->info.focused;
    out_info->occluded = w->info.occluded;
    out_info->configured = w->info.configured;
    ax_mutex_unlock(&w->info_lock);
    return AX_OK;
}

ax_result ax_window_set_hit_regions(ax_window window, const ax_hit_region *regions, uint32_t count)
{
    ax_window_impl *w = ax__window_from_handle(window.h);
    ax_hit_map *map;
    if (w == NULL) {
        return invalid_window("ax_window_set_hit_regions");
    }
    if (count > AX_HIT_REGIONS_MAX || (count > 0 && regions == NULL)) {
        return ax_set_error(AX_ERR_UNSUPPORTED,
                            "ax_window_set_hit_regions: at most %u regions (got %u)",
                            AX_HIT_REGIONS_MAX, count);
    }
    map = &w->hit[ax_tb_write_index(&w->hit_tb)];
    map->window_handle = window.h;
    map->count = count;
    if (count > 0) {
        memcpy(map->regions, regions, sizeof(ax_hit_region) * count);
    }
    ax_tb_publish(&w->hit_tb);
    return AX_OK;
}

/* ---- surfaces
 * ------------------------------------------------------------------------------------ */

ax_result ax_surface_get_info(ax_surface surface, ax_surface_info *out_info)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    if (out_info == NULL || out_info->struct_size < sizeof(ax_surface_info)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_surface_get_info: struct_size must be >= %u",
                            (unsigned)sizeof(ax_surface_info));
    }
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_surface_get_info: invalid surface");
    }
    ax_mutex_lock(&w->info_lock);
    out_info->logical_width = w->info.width;
    out_info->logical_height = w->info.height;
    out_info->scale = w->info.scale;
    out_info->pixel_width = w->info.pixel_width;
    out_info->pixel_height = w->info.pixel_height;
    ax_mutex_unlock(&w->info_lock);
    return AX_OK;
}

ax_result ax_surface_get_native(ax_surface surface, ax_surface_native *out_native)
{
    ax_window_impl *w = ax__window_from_handle(surface.h);
    if (out_native == NULL || out_native->struct_size < sizeof(ax_surface_native)) {
        return ax_set_error(AX_ERR_ABI_MISMATCH, "ax_surface_get_native: struct_size must be >= %u",
                            (unsigned)sizeof(ax_surface_native));
    }
    if (w == NULL) {
        return ax_set_error(AX_ERR_INVALID_HANDLE, "ax_surface_get_native: invalid surface");
    }
    memset(&out_native->u, 0, sizeof out_native->u);
    w->app->ops->surface_native(w, out_native);
    return AX_OK;
}

/* ---- helpers for backends (main thread) ---------------------------------------------------------
 */

void ax__window_publish_config(ax_window_impl *w, const ax_surface_config *config,
                               bool size_changed, bool scale_changed)
{
    ax_evq_geometry g;

    w->config[ax_tb_write_index(&w->config_tb)] = *config;
    ax_tb_publish(&w->config_tb);

    ax_mutex_lock(&w->info_lock);
    w->info.width = config->logical_width;
    w->info.height = config->logical_height;
    w->info.scale = config->scale;
    w->info.pixel_width = config->pixel_width;
    w->info.pixel_height = config->pixel_height;
    w->info.configured = true;
    ax_mutex_unlock(&w->info_lock);

    g.width = config->logical_width;
    g.height = config->logical_height;
    g.pixel_width = config->pixel_width;
    g.pixel_height = config->pixel_height;
    g.scale = config->scale;
    g.time_ns = config->time_ns;
    ax_evq_set_geometry(&w->app->events, w->index, &g, size_changed, scale_changed);
    ax__app_wake_presenters(w->app);
}

void ax__window_request_commit(ax_window_impl *w)
{
    ax_atomic_store_u32(&w->commit_requested, 1, AX_MO_SEQ_CST);
    ax__app_wake_presenters(w->app);
}

void ax__window_set_focus(ax_window_impl *w, bool focused)
{
    bool changed;
    ax_mutex_lock(&w->info_lock);
    changed = w->info.focused != focused;
    w->info.focused = focused;
    ax_mutex_unlock(&w->info_lock);
    if (changed) {
        ax_evq_push_focus(&w->app->events, w->index, focused, ax_time_monotonic_ns());
    }
}

void ax__window_set_occluded(ax_window_impl *w, bool occluded)
{
    bool changed;
    ax_mutex_lock(&w->info_lock);
    changed = w->info.occluded != occluded;
    w->info.occluded = occluded;
    ax_mutex_unlock(&w->info_lock);
    if (changed) {
        ax_evq_push_occlusion(&w->app->events, w->index, occluded, ax_time_monotonic_ns());
    }
}

void ax__window_set_server_decorated(ax_window_impl *w, bool server_decorated)
{
    ax_mutex_lock(&w->info_lock);
    w->info.server_decorated = server_decorated;
    ax_mutex_unlock(&w->info_lock);
}

void ax__window_close_requested(ax_window_impl *w)
{
    ax_evq_push_close(&w->app->events, w->index, ax_time_monotonic_ns());
}

const ax_hit_map *ax__window_hit_map(ax_window_impl *w)
{
    const ax_hit_map *map = &w->hit[ax_tb_acquire(&w->hit_tb)];
    return map->window_handle == ax_atomic_load_u64(&w->handle, AX_MO_RELAXED) ? map : NULL;
}
