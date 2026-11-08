/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK (for now): the headless host and its C testing API (decision 85). It runs a world
 * without a window, a GPU or the platform module: virtual surfaces of any size, scale, theme and
 * reduce-motion setting, drawn by the software renderer (or ax_render_wgpu offscreen when a GPU
 * is available and asked for).
 *
 * Deterministic by default: a manual clock (the world's injectable clock, decision 67), the
 * runtime's cooperative single-thread mode (decision 77, ax_host_frame), the inline job system and
 * the software renderer, so the same calls give the same pixels on every platform. Real threads
 * and the real clock are opt-in (AX_HEADLESS_THREADS). Many instances can run in one process,
 * each with its own world (up to AX_MAX_WORLDS), from different threads.
 *
 *     ax_headless *h;
 *     ax_headless_desc d = ax_headless_default_desc();
 *     ax_headless_create(&d, &h);
 *     ax_headless_surface_desc s = ax_headless_default_surface_desc();   // 390 x 844 at 3x ...
 *     ax_node root;
 *     ax_headless_add_surface(h, &s, &root);
 *     ... transactions on ax_headless_world(h) under root ...
 *     ax_headless_settle(h, 100, NULL);          // frames until every transaction is shown
 *     ax_headless_write_png(h, root, "shot.png");
 *     ax_headless_destroy(h);
 *
 * Bindings' testing APIs (C#, TS, Python) are built on this in M2.
 *
 * thread: an instance belongs to the thread that created it (the world's app thread).
 */
#ifndef AX_HEADLESS_H
#define AX_HEADLESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_node.h>
#include <axion/ax_world.h>
#include <axion/render/ax_render_ir.h>
#include <axion/runtime/ax_runtime_sdk.h>

AX_EXTERN_C_BEGIN

typedef struct ax_headless ax_headless;

/* ax_headless_desc.flags */
#define AX_HEADLESS_THREADS                                            \
    0x1u /* the runtime's scene and render threads, the real clock and \
          * a threaded job system: not deterministic */
#define AX_HEADLESS_SYSTEM_FONTS 0x2u /* reserved: the system's fonts instead of the bundled set \
                                       */

typedef enum ax_headless_renderer {
    AX_HEADLESS_RENDER_SOFT = 0, /* ax_render_soft (the default; deterministic) */
    AX_HEADLESS_RENDER_WGPU = 1  /* ax_render_wgpu offscreen (builds with the gpu module) */
} ax_headless_renderer;

typedef struct ax_headless_desc {
    uint32_t struct_size;          /* sizeof(ax_headless_desc) */
    const void *next;              /* extension chain; none defined yet */
    const ax_allocator *allocator; /* NULL: the default; must be thread-safe with THREADS */
    uint32_t flags;                /* AX_HEADLESS_* */
    uint32_t renderer;             /* ax_headless_renderer */
    uint64_t start_time_ns;        /* the manual clock's first value (default 1 s) */
    uint64_t refresh_ns;           /* the virtual display's frame interval (default 1/60 s) */
    /* The font set text uses (decision 85's hook). NULL: the bundled set, which arrives with
     * text later in M1; nothing reads it yet. */
    const void *fonts;
    /* Examples and benchmarks only (ax_runtime_sdk.h): moves nodes at every tick until
     * implicit animations exist. */
    ax_rt_scene_driver driver;
    void *driver_user;
} ax_headless_desc;

AX_API ax_headless_desc ax_headless_default_desc(void);

typedef enum ax_headless_theme {
    AX_HEADLESS_THEME_LIGHT = 0,
    AX_HEADLESS_THEME_DARK = 1
} ax_headless_theme;

/* A virtual surface: a window root without a window. Theme and reduce-motion are kept and read
 * back; styles and animations consume them when they exist. */
typedef struct ax_headless_surface_desc {
    uint32_t struct_size; /* sizeof(ax_headless_surface_desc) */
    float width, height;  /* logical units */
    float scale;          /* device pixels per logical unit, fractional allowed */
    uint32_t theme;       /* ax_headless_theme */
    bool reduce_motion;
    ax_color background; /* behind everything until the root draws (default opaque black) */
} ax_headless_surface_desc;

AX_API ax_headless_surface_desc ax_headless_default_surface_desc(void);

/* Errors: AX_ERR_ABI_MISMATCH, AX_ERR_INVALID_ARGUMENT (renderer, refresh),
 * AX_ERR_UNSUPPORTED (AX_HEADLESS_THREADS without threads, or the GPU renderer in a build or on a
 * machine without one), AX_ERR_OUT_OF_MEMORY (also when AX_MAX_WORLDS worlds exist). */
AX_API ax_result ax_headless_create(const ax_headless_desc *desc, ax_headless **out);
AX_API void ax_headless_destroy(ax_headless *h);

/* The instance's world: transactions, events and the mirror (ax_mirror.h) as with any world. */
AX_API ax_world ax_headless_world(ax_headless *h);

/* ---- surfaces -------------------------------------------------------------------------------- */

/* Creates a root node (in a transaction of its own) and attaches it to a new virtual surface.
 * At most AX_RUNTIME_MAX_SURFACES. Errors: AX_ERR_INVALID_ARGUMENT (size or scale),
 * AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_headless_add_surface(ax_headless *h, const ax_headless_surface_desc *desc,
                                         ax_node *out_root);
AX_API ax_result ax_headless_remove_surface(ax_headless *h, ax_node root);

/* A new size or scale, as a window resize: the next frames record and draw at it. */
AX_API ax_result ax_headless_resize(ax_headless *h, ax_node root, float width, float height,
                                    float scale);

typedef struct ax_headless_surface_info {
    uint32_t struct_size; /* set by the caller */
    float width, height, scale;
    uint32_t pixel_width, pixel_height;
    uint32_t theme;
    bool reduce_motion;
    uint64_t frames;   /* frames presented */
    uint64_t shown_ir; /* mailbox sequence of the snapshot drawn last (0: none yet) */
} ax_headless_surface_info;

/* Errors: AX_ERR_INVALID_HANDLE (no surface for root). */
AX_API ax_result ax_headless_get_surface_info(ax_headless *h, ax_node root,
                                              ax_headless_surface_info *out);

/* ---- time ------------------------------------------------------------------------------------ */

/* The world's clock: the manual one, or the real one with AX_HEADLESS_THREADS. */
AX_API uint64_t ax_headless_now(ax_headless *h);

/* Runs `count` frames on every surface. Manual clock: the virtual display presents at fixed
 * vsyncs, refresh_ns apart from start_time_ns; each frame moves the clock to its vsync and applies
 * the transactions committed up to one refresh before it (decision 77's latch point), so a commit
 * made right after a frame shows at the next one. With threads: waits until every surface
 * presented `count` more frames. Errors: those of the presenter. */
AX_API ax_result ax_headless_step(ax_headless *h, uint32_t count);

/* Advances the clock by ns, running a frame at every vsync the interval reaches (with threads:
 * sleeps ns). */
AX_API ax_result ax_headless_advance_time(ax_headless *h, uint64_t ns);

/* Runs frames until every committed transaction is applied (ax_world_last_applied_seq reaches
 * ax_world_last_committed_seq) and drawn, at most max_frames (with threads: for at most
 * max_frames refreshes of time). *out_frames (may be NULL) gets how many ran. Animations will
 * join the condition when they exist. Errors: AX_ERR_UNSUPPORTED when it did not settle. */
AX_API ax_result ax_headless_settle(ax_headless *h, uint32_t max_frames, uint32_t *out_frames);

/* ---- snapshots ------------------------------------------------------------------------------- */

/* Premultiplied RGBA8 pixels, rows packed (stride width * 4). */
typedef struct ax_headless_image {
    uint32_t width, height;
    size_t stride;
    uint8_t *rgba;
} ax_headless_image;

/* A copy of the newest frame drawn for the surface; free it with ax_headless_image_free.
 * Errors: AX_ERR_INVALID_HANDLE, AX_ERR_UNSUPPORTED (nothing drawn yet), AX_ERR_OUT_OF_MEMORY. */
AX_API ax_result ax_headless_capture(ax_headless *h, ax_node root, ax_headless_image *out);
AX_API void ax_headless_image_free(ax_headless *h, ax_headless_image *image);

/* The newest frame as a PNG file (decision 82's codec, straight alpha). */
AX_API ax_result ax_headless_write_png(ax_headless *h, ax_node root, const char *path);

/* The Render IR snapshot drawn last, as text (ax_ir_dump), or in the binary form (ax_ir_serialize)
 * into a buffer the caller frees with ax_headless_free. Errors: AX_ERR_INVALID_HANDLE,
 * AX_ERR_UNSUPPORTED (nothing drawn yet). */
AX_API ax_result ax_headless_dump_ir(ax_headless *h, ax_node root, ax_ir_write_fn write, void *ctx);
AX_API ax_result ax_headless_save_ir(ax_headless *h, ax_node root, uint8_t **out_data,
                                     size_t *out_size);
AX_API void ax_headless_free(ax_headless *h, void *data, size_t size);

AX_EXTERN_C_END

#endif /* AX_HEADLESS_H */
