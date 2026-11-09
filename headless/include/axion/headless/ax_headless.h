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
 *     ax_headless_settle(h, 100, NULL);          // until every transaction is shown, at rest
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
#include <axion/ax_mirror.h>
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
#define AX_HEADLESS_SYSTEM_FONTS                                    \
    0x2u /* reserved: the system's fonts instead of the bundled set \
          */
#define AX_HEADLESS_NO_SEMANTICS                                                  \
    0x4u /* no accessibility tree: queries find nothing (the tree is on by default, \
          * as an assistive technology reading every surface) */

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
    /* How the software renderer blends (ax_render_blend, issue #4's comparison; 0: the default).
     * AX_HEADLESS_RENDER_WGPU draws the default. */
    uint32_t blend;
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
 * presented `count` more frames. Errors: those of the presenter; AX_ERR_TIMEOUT (threads) when
 * the frames did not come within 10 s plus four refreshes per frame. */
AX_API ax_result ax_headless_step(ax_headless *h, uint32_t count);

/* Advances the clock by ns, running a frame at every vsync the interval reaches (with threads:
 * sleeps ns). */
AX_API ax_result ax_headless_advance_time(ax_headless *h, uint64_t ns);

/* Runs frames until every committed transaction is applied (ax_world_last_applied_seq reaches
 * ax_world_last_committed_seq) and drawn, and nothing animates (decision 94), at most max_frames
 * (with threads: for at most max_frames refreshes of time). *out_frames (may be NULL) gets how
 * many ran. Errors: AX_ERR_TIMEOUT when it did not settle. */
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

/* ---- the semantic tree (decision 108) --------------------------------------------------------
 *
 * Each surface's accessibility tree, as an assistive technology sees it: the host keeps the copy
 * its updates build (AccessKit's shape, ax_a11y.h), as of the last frame. Queries find elements the
 * way users and assistive technologies do, in tree order: by role and accessible name, by text,
 * or by test id. Elements are nodes (text runs are not returned). With the host's threads the
 * copy is read under a lock, consistent with one frame. */

/* Name and text matching: exact by default (after trimming spaces at both ends). */
#define AX_HEADLESS_MATCH_SUBSTRING   0x1u
#define AX_HEADLESS_MATCH_IGNORE_CASE 0x2u /* ASCII letters */

/* Elements with the role (ax_role) whose name matches (NULL: any name), into out (at most
 * capacity); returns how many match. A root without a surface, or without semantics, finds 0. */
AX_API uint32_t ax_headless_get_by_role(ax_headless *h, ax_node root, uint32_t role,
                                        const char *name, uint32_t match, ax_node *out,
                                        uint32_t capacity);

/* Text leaves whose text matches. */
AX_API uint32_t ax_headless_get_by_text(ax_headless *h, ax_node root, const char *text,
                                        uint32_t match, ax_node *out, uint32_t capacity);

/* Elements whose TEST_ID is exactly test_id. */
AX_API uint32_t ax_headless_get_by_test_id(ax_headless *h, ax_node root, const char *test_id,
                                           ax_node *out, uint32_t capacity);

typedef struct ax_headless_element {
    uint32_t struct_size; /* set by the caller */
    uint32_t role;        /* ax_role, resolved */
    uint32_t flags;       /* AX_A11Y_* (ax_a11y.h) */
    uint32_t actions;     /* AX_ACTION_* */
    uint32_t checked, expanded, level;
    float value, value_min, value_max;
    ax_rectf bounds; /* in the window's logical units: the box around the transformed bounds */
    bool focused;
} ax_headless_element;

/* An element's semantics. Errors: AX_ERR_INVALID_HANDLE (not in the tree), AX_ERR_ABI_MISMATCH. */
AX_API ax_result ax_headless_get_element(ax_headless *h, ax_node root, ax_node element,
                                         ax_headless_element *out);

/* Its accessible name (NUL-terminated, truncated to capacity); *out_length (may be NULL) gets the
 * whole length. Errors: AX_ERR_INVALID_HANDLE. */
AX_API ax_result ax_headless_get_name(ax_headless *h, ax_node root, ax_node element, char *buffer,
                                      size_t capacity, size_t *out_length);

/* ax_headless_dump_semantics flags. By default the dump is compact, as the MCP server's get_tree:
 * generic containers are left out (their children take their place) and text leaves show no
 * lines. */
#define AX_HEADLESS_DUMP_ALL    0x1u /* generic containers and text runs too */
#define AX_HEADLESS_DUMP_BOUNDS 0x2u /* each element's bounds in the window */

/* The semantic tree as indented text, for diffs and golden files: a line per element,
 *     list_item "Shaping" test-id="card-0" actions=default,focus @20,212 350x96
 * Errors: AX_ERR_INVALID_HANDLE (no surface), AX_ERR_UNSUPPORTED (no tree yet). */
AX_API ax_result ax_headless_dump_semantics(ax_headless *h, ax_node root, uint32_t flags,
                                            ax_ir_write_fn write, void *ctx);

/* What an assistive technology would ask for: an action on an element (ax_world_post_a11y_action),
 * applied by the next frame and passed to the app as AX_WORLD_EVENT_A11Y_ACTION. Errors:
 * AX_ERR_INVALID_HANDLE (no surface), AX_ERR_OUT_OF_MEMORY (the queue is full). */
AX_API ax_result ax_headless_a11y_action(ax_headless *h, ax_node root, ax_node element,
                                         uint32_t action, float value);

AX_EXTERN_C_END

#endif /* AX_HEADLESS_H */
