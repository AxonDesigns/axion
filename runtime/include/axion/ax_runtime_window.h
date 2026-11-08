/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Stable ABI (builds with the platform module): a runtime that presents an app's windows.
 *
 * The runtime's scene thread becomes the app's event consumer: window events reach the app thread
 * through ax_world_poll_event / ax_world_wait_event (with the window's root), not
 * ax_app_poll_event. Its render thread presents every window with the default renderer.
 *
 *     // app thread (main thread pumping elsewhere)
 *     ax_runtime_create_for_app(&desc, app, &rt);
 *     ax_runtime_add_window(rt, window, &root);
 *     ...
 *     ax_runtime_remove_window(rt, window);  // before ax_window_destroy
 */
#ifndef AX_RUNTIME_WINDOW_H
#define AX_RUNTIME_WINDOW_H

#include "ax_runtime.h"
#include <axion/ax_app.h>
#include <axion/ax_window.h>

AX_EXTERN_C_BEGIN

/*
 * As ax_runtime_create, presenting the app's windows. Threaded mode only.
 * Errors: those of ax_runtime_create, AX_ERR_INVALID_HANDLE (app), AX_ERR_UNSUPPORTED (single-
 * threaded mode). thread: app
 */
AX_API ax_result ax_runtime_create_for_app(const ax_runtime_desc *desc, ax_app app,
                                           ax_runtime *out_runtime);

/*
 * Creates the window's root node (committed in a transaction of its own) and attaches it to the
 * window's surface. The root is laid out at the window's size; build the tree under it with
 * transactions. Errors: AX_ERR_INVALID_HANDLE, AX_ERR_INVALID_ARGUMENT (already added),
 * AX_ERR_OUT_OF_MEMORY. thread: app
 */
AX_API ax_result ax_runtime_add_window(ax_runtime runtime, ax_window window, ax_node *out_root);

/*
 * Stops presenting the window; call it before ax_window_destroy. It waits until the runtime's
 * threads have let the window go, at most 1 s. The root node stays.
 * Errors:
 *   - AX_ERR_INVALID_HANDLE: invalid runtime; AX_ERR_INVALID_ARGUMENT: the window was not added;
 *   - AX_ERR_TIMEOUT: the threads did not confirm within 1 s. The window is then still in
 *     use (the render thread may still present it) and must not be destroyed. Call again until
 *     AX_OK; a retry queues nothing new, it only waits again.
 * thread: app
 */
AX_API ax_result ax_runtime_remove_window(ax_runtime runtime, ax_window window);

AX_EXTERN_C_END

#endif /* AX_RUNTIME_WINDOW_H */
