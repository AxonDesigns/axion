/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: windows.
 *
 * A window is an OS window holding exactly one surface (ax_surface.h), which is what gets
 * rendered into. Window functions run on the app's main thread; called from another thread they
 * are handed to it ("any->main", see ax_app.h).
 */
#ifndef AX_WINDOW_H
#define AX_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#include "ax_app.h"
#include <axion/ax_config.h>
#include <axion/ax_handle.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

AX_DECLARE_HANDLE(ax_window);
AX_DECLARE_HANDLE(ax_surface);

typedef enum ax_decorations {
    AX_DECORATIONS_NATIVE = 0,         /* the system's frame and title bar */
    AX_DECORATIONS_NONE = 1,           /* frameless: the app draws everything */
    AX_DECORATIONS_CUSTOM_TITLEBAR = 2 /* the app draws the title bar, native behaviour stays */
} ax_decorations;

typedef enum ax_backdrop {
    AX_BACKDROP_NONE = 0,
    AX_BACKDROP_TRANSPARENT = 1,
    AX_BACKDROP_BLUR = 2,
    AX_BACKDROP_MICA = 3,
    AX_BACKDROP_ACRYLIC = 4,
    AX_BACKDROP_VIBRANCY = 5
} ax_backdrop;

typedef enum ax_window_level {
    AX_LEVEL_NORMAL = 0,
    AX_LEVEL_FLOATING = 1,
    AX_LEVEL_ALWAYS_ON_TOP = 2
} ax_window_level;

/* ax_window_desc.flags */
#define AX_WINDOW_RESIZABLE     (1u << 0)
#define AX_WINDOW_RESTORE_STATE (1u << 1)

typedef struct ax_window_desc {
    uint32_t
        struct_size;  /* sizeof(ax_window_desc): lets the struct grow without breaking the ABI */
    const void *next; /* optional extension chain, WebGPU style */
    ax_str title;
    float width, height; /* logical units */
    float min_width, min_height;
    uint32_t decorations; /* ax_decorations */
    uint32_t backdrop;    /* ax_backdrop */
    uint32_t level;       /* ax_window_level */
    uint32_t flags;       /* AX_WINDOW_* */
} ax_window_desc;

/*
 * Window regions, for the hit-test map a window publishes (axion/platform/ax_hit_regions.h;
 * later, the AX_PROP_WINDOW_REGION node property). They tell the system what a pointer press
 * means where the app draws its own frame: a caption drags the window, an edge resizes it.
 */
typedef enum ax_window_region {
    AX_REGION_CLIENT = 0, /* ordinary content */
    AX_REGION_CAPTION = 1,
    AX_REGION_RESIZE_TOP = 2,
    AX_REGION_RESIZE_BOTTOM = 3,
    AX_REGION_RESIZE_LEFT = 4,
    AX_REGION_RESIZE_RIGHT = 5,
    AX_REGION_RESIZE_TOP_LEFT = 6,
    AX_REGION_RESIZE_TOP_RIGHT = 7,
    AX_REGION_RESIZE_BOTTOM_LEFT = 8,
    AX_REGION_RESIZE_BOTTOM_RIGHT = 9,
    AX_REGION_MINIMIZE = 10,
    AX_REGION_MAXIMIZE = 11,
    AX_REGION_CLOSE = 12
} ax_window_region;

/* What a window currently is, as last configured by the system. */
typedef struct ax_window_info {
    uint32_t struct_size;  /* set by the caller: sizeof(ax_window_info) */
    float width, height;   /* logical size */
    float scale;           /* device pixels per logical unit */
    bool server_decorated; /* the system draws the frame (NATIVE was granted) */
    bool focused;
    bool occluded;
    bool configured; /* the system has configured the window at least once */
} ax_window_info;

/*
 * Creates a window and its surface. Fields this platform cannot honour are accepted and ignored;
 * the matching AX_CAP_* flag tells which. The window shows once its surface is first presented.
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_ABI_MISMATCH (struct_size), AX_ERR_OUT_OF_MEMORY (also
 * when ax_app_desc.max_windows are already open).
 * thread: any->main
 */
AX_API ax_result ax_window_create(ax_app app, const ax_window_desc *desc, ax_window *out_window);

/* Destroys the window. Its surface must no longer be presented by any thread.
 * thread: any->main */
AX_API ax_result ax_window_destroy(ax_window window);

/* thread: any->main */
AX_API ax_result ax_window_set_title(ax_window window, ax_str title);

/* Minimum size in logical units (0 for none). Takes effect with the surface's next commit; the
 * presenting thread is woken to make one. thread: any->main */
AX_API ax_result ax_window_set_min_size(ax_window window, float width, float height);

/* The window's surface; valid until the window is destroyed. thread: any */
AX_API ax_result ax_window_get_surface(ax_window window, ax_surface *out_surface);

/* thread: any */
AX_API ax_result ax_window_get_info(ax_window window, ax_window_info *out_info);

AX_EXTERN_C_END

#endif /* AX_WINDOW_H */
