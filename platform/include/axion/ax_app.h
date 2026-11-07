/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI: the application object, its main thread and the platform's capabilities.
 *
 * An ax_app owns the connection to the windowing system and the event pump. The thread that
 * creates it becomes its main thread: it must run the pump (ax_app_run, or ax_app_pump from a
 * host's own loop), and it never runs app code or layout. App code runs on other threads and
 * talks to the main thread through the functions below, which are thread-safe as tagged.
 *
 * Thread tags. Every declaration in the platform module states where it may be called:
 *   any       from any thread;
 *   main      only on the app's main thread (debug builds assert; release builds return
 *             AX_ERR_WRONG_THREAD);
 *   any->main from any thread; the work runs on the main thread. Called on another thread, the
 *             call is handed to the main thread and waits for it, so the main thread must be
 *             pumping (inside ax_app_run or ax_app_pump) for the call to complete;
 *   consumer  from the one thread that consumes events (one at a time);
 *   presenter from the one thread presenting a surface (one at a time; see axion/platform/).
 */
#ifndef AX_APP_H
#define AX_APP_H

#include <stdbool.h>
#include <stdint.h>

#include <axion/ax_allocator.h>
#include <axion/ax_config.h>
#include <axion/ax_handle.h>
#include <axion/ax_types.h>

AX_EXTERN_C_BEGIN

AX_DECLARE_HANDLE(ax_app);

/* Which windowing system an app is connected to. */
typedef enum ax_backend { AX_BACKEND_NONE = 0, AX_BACKEND_WAYLAND = 1 } ax_backend;

typedef struct ax_app_desc {
    uint32_t struct_size; /* sizeof(ax_app_desc) */
    const void *next;     /* extension chain; NULL */

    /* Every allocation the platform module makes comes from here. Required. */
    const ax_allocator *allocator;

    /* Reverse-DNS application id ("net.axonworks.studio"), used by the desktop to match the
     * app's windows with its .desktop file and icon. May be empty. */
    ax_str app_id;

    /* Most windows open at once (0 selects 32). Per-window storage is reserved up front so the
     * main thread never allocates while delivering events. */
    uint32_t max_windows;

    /* Capacity of the event queue, rounded up to a power of two (0 selects 256). */
    uint32_t event_capacity;
} ax_app_desc;

/*
 * Capabilities: what the connected windowing system allows, so apps check a flag instead of the
 * OS. Bits are only ever appended. A feature whose flag is false is accepted and ignored.
 */
#define AX_CAP_WINDOW_SET_POSITION         (1ull << 0) /* place windows at global coordinates */
#define AX_CAP_WINDOW_ALWAYS_ON_TOP        (1ull << 1) /* AX_LEVEL_FLOATING / ALWAYS_ON_TOP */
#define AX_CAP_WINDOW_FRAMELESS            (1ull << 2) /* AX_DECORATIONS_NONE */
#define AX_CAP_WINDOW_CUSTOM_TITLEBAR      (1ull << 3) /* AX_DECORATIONS_CUSTOM_TITLEBAR */
#define AX_CAP_WINDOW_SERVER_DECORATIONS   (1ull << 4) /* the system can draw NATIVE frames */
#define AX_CAP_WINDOW_TRANSPARENCY         (1ull << 5) /* AX_BACKDROP_TRANSPARENT */
#define AX_CAP_WINDOW_BACKDROP_BLUR        (1ull << 6) /* BLUR, MICA, ACRYLIC, VIBRANCY */
#define AX_CAP_WINDOW_MULTIPLE             (1ull << 7) /* more than one window */
#define AX_CAP_WINDOW_EXCLUDE_FROM_CAPTURE (1ull << 8)
#define AX_CAP_WINDOW_RESTORE_STATE        (1ull << 9)  /* AX_WINDOW_RESTORE_STATE */
#define AX_CAP_WINDOW_HIT_REGIONS          (1ull << 10) /* move/resize driven by hit regions */
#define AX_CAP_FRACTIONAL_SCALE            (1ull << 11) /* non-integer scale factors */
#define AX_CAP_PRESENTATION_TIMING         (1ull << 12) /* measured present times and refresh */
#define AX_CAP_OCCLUSION_EVENTS            (1ull << 13) /* AX_EVENT_OCCLUSION is delivered */
#define AX_CAP_CURSOR_SHAPES               (1ull << 14) /* system cursor shapes by name */

/*
 * Connects to the windowing system. The calling thread becomes the app's main thread.
 * Errors: AX_ERR_UNSUPPORTED when no supported windowing system is reachable (for example no
 * Wayland compositor), AX_ERR_OUT_OF_MEMORY, AX_ERR_ABI_MISMATCH for a bad struct_size.
 * thread: any (the caller becomes main)
 */
AX_API ax_result ax_app_create(const ax_app_desc *desc, ax_app *out_app);

/* Closes the connection; every window must have been destroyed. thread: main */
AX_API ax_result ax_app_destroy(ax_app app);

/* Pumps events until ax_app_quit is called. thread: main */
AX_API ax_result ax_app_run(ax_app app);

/* Runs one iteration of the pump, waiting at most timeout_ns for something to do. For hosts that
 * own the loop, and for tests. thread: main */
AX_API ax_result ax_app_pump(ax_app app, uint64_t timeout_ns);

/* Makes ax_app_run return after its current iteration. thread: any */
AX_API ax_result ax_app_quit(ax_app app);

/* The capability flags (AX_CAP_*), or 0 for an invalid handle. thread: any */
AX_API uint64_t ax_app_caps(ax_app app);

/* The connected windowing system, or AX_BACKEND_NONE for an invalid handle. thread: any */
AX_API ax_backend ax_app_backend(ax_app app);

AX_EXTERN_C_END

#endif /* AX_APP_H */
