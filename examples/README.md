Example apps (Linux/Wayland).

- `m0_window/`: one window presented by a separate thread, with shared-memory buffers or the
  Render IR recorded by hand (M0).
- `m1_nodes/`: a heading and a list of cards with titles and paragraphs (11 to 48 px, the
  system's Noto Sans through fontconfig) laid out with flex (decision 90) that reorder, grow,
  change color and change width through transactions every 1.2 s, each change animated implicitly
  by the runtime's scene thread (decision 94): a reorder moves every card with the default spring,
  a card that grows shows its detail paragraph as its height animates behind its rounded clip, a
  color eases, and a narrower list re-wraps every paragraph at each interpolated width without
  shaping it again (decision 99). At rest the
  window presents nothing (decision 95). `--block-ms=500` blocks the app thread every
  `--block-every` seconds (2 by default): running animations must not notice. `--stats` prints
  presentations, missed refreshes and how long a change takes from commit to screen.
  `--renderer=soft` draws with ax_render_soft into shared-memory buffers.

      build/gcc-release/examples/m1_nodes/m1_nodes --stats --block-ms=500
