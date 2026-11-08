Runtime: runs a world's frame pipeline (decision 77).

- `ax_runtime.h` (stable): create a runtime for a world, threaded or single-threaded, and
  `ax_host_frame` for hosts that drive frames themselves.
- `ax_runtime_sdk.h` (internal SDK): surfaces, the presenter interface the render side
  implements, frame requests, the scene stage and counters.

- `ax_runtime_window.h` (stable, platform builds): a runtime presenting an app's windows; its
  scene thread forwards their events to the world (decision 78).
- `ax_runtime_platform.h` (internal SDK, platform builds): the renderer and handshake options,
  per-window counters.

The scene stage and the single-threaded mode depend on core and the Render IR only. The threads
need base's threads; the platform presenter (windows, the renderer, decision 57's frame step) is
built with the platform module, and uses ax_render_wgpu when the gpu module is built, falling back
to ax_render_soft in shared-memory buffers when it is not or no GPU is usable (decision 84).
