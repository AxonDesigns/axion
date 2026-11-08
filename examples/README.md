Example apps (Linux/Wayland).

- `m0_window/`: one window presented by a separate thread, with shared-memory buffers or the
  Render IR recorded by hand (M0).
- `m1_nodes/`: the same picture built as a node tree by the app thread and drawn through the M1
  pipeline: the runtime's scene thread applies transactions at each vsync, positions and records
  the Render IR from the tree; its render thread presents with ax_render_wgpu (decisions 76 to
  78). `--block-ms=500` blocks the app thread every `--block-every` seconds (2 by default):
  motion must not notice. Until implicit animations exist, a scene driver that only this example
  has moves the turning, sliding and timed parts and places what depends on the window's size.
  `--stats` prints presentations, missed refreshes and how long a color change takes from commit
  to screen.

      build/gcc-release/examples/m1_nodes/m1_nodes --stats --block-ms=500
