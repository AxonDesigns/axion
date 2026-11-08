Core: node store, transactions, style, layout, animation, gestures, scrolling, virtual layouts, hit testing and the accessibility tree.

Implemented so far (M1): the node store, properties and dirty propagation (`ax_node_store.h`,
`ax_prop_registry.h`, `ax_scene.h`), the world and transactions (`ax_world.h`, `ax_txn.h`, applied
atomically on the scene thread), the app thread's mirror (`ax_mirror.h`), and the events the scene
thread sends the app thread with `ax_txn_on_presented` (`ax_world_poll_event`), and the frame
stages after applying: window roots, layout, and recording the Render IR from the store with a
per-node cache and clip layers for overflow hidden (`ax_frame.h`). Core depends on base and the
Render IR only. Decisions 62 to 69, 75, 76 and 89 to 95.

Layout (`ax_layout.h`, `src/layout/`): an engine by protocols, flex first, with Yoga-compatible
semantics (508 of 508 supported fixtures of Yoga's conformance suite pass:
`tests/unit/core/test_layout_yoga.c`, fixtures ported by `tools/yoga/port_fixtures.py`). Relayout
boundaries limit what an edit lays out, independent boundaries run as frame jobs, and frames are
rounded to the window's pixel grid. `tests/bench/bench_layout.c` measures 10 000 nodes.

Animation (`core/ax_anim.h`, `src/anim/`, decision 94): every change a transaction makes animates
implicitly on the scene thread. Timing curves (cubic-bezier, steps, linear), springs in closed form
(perceptual or physical) and decay, all deterministic; transitions per node, per world and per
effect; springs keep their velocity when retargeted and timing curves blend. Frames animate too:
a node that changes size lays its children out again at its presentation size each frame (the
`resize` preset). `ax_world_animate` samples at the frame's present time; the mirror reflects
targets, `ax_world_presentation` the values on screen. `tests/bench/bench_anim.c` measures it.
