Core: node store, transactions, style, layout, animation, gestures, scrolling, virtual layouts, hit testing and the accessibility tree.

Implemented so far (M1): the node store, properties and dirty propagation (`ax_node_store.h`,
`ax_prop_registry.h`, `ax_scene.h`), the world and transactions (`ax_world.h`, `ax_txn.h`, applied
atomically on the scene thread), the app thread's mirror (`ax_mirror.h`), and the events the scene
thread sends the app thread with `ax_txn_on_presented` (`ax_world_poll_event`), and the frame
stages after applying: window roots, positioning until flex, and recording the Render IR from the
store with a per-node cache (`ax_frame.h`). Core depends on base and the Render IR only. Decisions
62 to 69, 75 and 76.
