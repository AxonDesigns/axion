/*
 * Copyright 2026 Axon Works
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal SDK: being told that events were queued, for an event consumer that sleeps on its own
 * wait instead of ax_app_wait_event (the scene thread, which also waits for frame requests).
 *
 *     ax_app_set_event_notify(app, wake_me, ctx);   // the consumer
 *     for (;;) { while (ax_app_poll_event(app, &e) ok and e.type) handle(e); my_wait(); }
 *
 * The main thread calls fn after it queues events (once or more per batch, never while holding a
 * lock the consumer takes). fn must be short and must not block or call back into the app: wake
 * the consumer and return. Coalesced events (RESIZED, SCALE_CHANGED) that only update a marker
 * already queued do not call it again; the consumer reads the newest values when it takes the
 * marker.
 */
#ifndef AX_PLATFORM_EVENT_NOTIFY_H
#define AX_PLATFORM_EVENT_NOTIFY_H

#include "../ax_app.h"

AX_EXTERN_C_BEGIN

typedef void (*ax_event_notify_fn)(void *user);

/*
 * Sets the hook, or clears it with fn NULL. Returns once no call to the previous hook is in
 * progress, so the caller may free what `user` points to after clearing it. fn must not call this
 * function. Errors: AX_ERR_INVALID_HANDLE. thread: any
 */
AX_API ax_result ax_app_set_event_notify(ax_app app, ax_event_notify_fn fn, void *user);

AX_EXTERN_C_END

#endif /* AX_PLATFORM_EVENT_NOTIFY_H */
