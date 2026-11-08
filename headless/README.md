Headless host: runs a world without a window, a GPU or the platform module (decision 85).

- `ax_headless.h` (internal SDK for now): instances with virtual surfaces (size, scale, theme,
  reduce-motion), a manual clock and the runtime's single-thread mode by default (deterministic),
  real threads opt-in; frames, time, settle (until every commit is drawn and nothing animates),
  PNG snapshots, IR dumps. Draws with ax_render_soft,
  or ax_render_wgpu offscreen when the gpu module is built and a GPU is present.
- `ax_png.h` (internal SDK): AXION's PNG codec, for snapshots and golden images (decision 82).

Synthetic input, device profiles, locale and text settings arrive with input (M2) and text.
