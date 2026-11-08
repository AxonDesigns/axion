Runtime: runs a world's frame pipeline (decision 77).

- `ax_runtime.h` (stable): create a runtime for a world, threaded or single-threaded, and
  `ax_host_frame` for hosts that drive frames themselves.
- `ax_runtime_sdk.h` (internal SDK): surfaces, the presenter interface the render side
  implements, frame requests, the scene stage and counters.

Depends on core and the Render IR only; the platform presenter (windows, the renderer) is built
when the platform module is.
