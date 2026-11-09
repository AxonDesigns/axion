# AccessKit coverage: which platforms get AccessKit, which get our own bridge

M1, prompt 6 (decision 110). Status of AccessKit's adapters, its C API and its license as of
2026-11-09, with a recommendation for each of AXION's six targets. The architecture's accessibility
paragraph ("Platform layer") plans to adopt AccessKit through its C API wherever it has a platform
adapter and to write our own bridge where it has none; it asks M1 to confirm iOS and the web.

## Summary

| Target | AccessKit adapter (crate, version in accesskit-c 0.23.2) | In the C API | Prebuilt in the release | Recommendation |
| --- | --- | --- | --- | --- |
| Windows | `accesskit_windows` 0.35.2, UI Automation | yes | x86, x86_64, arm64 (MSVC); x86, x86_64 (MinGW) | **AccessKit** (M3) |
| macOS | `accesskit_macos` 0.27.2, NSAccessibility | yes | arm64, x86_64 | **AccessKit** (M3) |
| Linux | `accesskit_unix` 0.24.1, AT-SPI over D-Bus (zbus) | yes | x86, x86_64 (no aarch64) | **AccessKit** (proof of concept done in M1; full bridge M2) |
| Android | `accesskit_android` 0.9.1, `AccessibilityNodeInfo` through JNI | yes | arm64-v8a, x86_64 | **AccessKit** (M4), checked against TalkBack before committing |
| iOS | `accesskit_ios` 0.2.2, UIAccessibility | yes | arm64, simulator arm64 and x86_64 | **AccessKit** (M4), with a fallback plan: see below |
| Web | none (listed as "planned") | no | no | **Our own bridge**: a hidden ARIA-annotated DOM |

**iOS: confirmed, with a caveat.** AccessKit has an iOS adapter in its official repository, the C
API exposes it and the release ships iOS binaries. It is young: the crate's first release was
0.1.0 on 2026-05-11, six releases in six months. We adopt it, check it with VoiceOver when the iOS
port lands (M4), and keep our tree's shape (AccessKit's) so that, should it fall short, a bridge of
our own consumes the same updates.

**Web: confirmed, our own bridge.** AccessKit has no web adapter (its README lists one for "apps
that render their own UI to a canvas" as planned), and the release has no wasm build. The web
bridge mirrors the same tree into a hidden DOM with ARIA roles, names and states, positioned over
the canvas for hit testing and focus. It consumes the same `ax_a11y_update` stream as the AccessKit
bridge, so it is independent of AccessKit's progress there.

## AccessKit's adapters

From the AccessKit README (<https://github.com/AccessKit/accesskit>) and the crates' published
versions (the ones accesskit-c 0.23.2's `Cargo.lock` pins):

- Windows (`accesskit_windows`): "implements UI Automation, the current Windows accessibility API".
- macOS (`accesskit_macos`): "implements the NSAccessibility protocols in the AppKit framework".
- Unix (`accesskit_unix`): "implements the AT-SPI D-Bus interfaces, using zbus, a pure-Rust
  implementation of D-Bus". Its API is blocking; it runs its D-Bus work on an executor of its own
  (async-io, no tokio needed) and calls back from its threads.
- Android (`accesskit_android`): "implements the Java-based Android accessibility API". Two layers:
  a low-level `Adapter` and an `InjectingAdapter` that injects accessibility into any view. First
  release 2025-03; 0.9.1 on 2026-10-25.
- iOS (`accesskit_ios`): "implements the UIAccessibility protocols in the UIKit framework". A
  low-level `Adapter` and a `SubclassingAdapter` (dynamic Objective-C subclassing of an existing
  view). First release 0.1.0 on 2026-05-11; 0.2.2 on 2026-10-25. Its development was funded through
  the NGI0 Commons Fund.
- Web: only under "Planned adapters".

The README says the released adapters "are all at rough feature parity" and "don't yet support
rich text or hypertext". Rich text accessibility (M3's text node) will need either AccessKit's
progress there or our own extension; nothing in M1 or M2 depends on it.

## The C API

accesskit-c (<https://github.com/AccessKit/accesskit-c>), release 0.23.2 (2026-11-01, "Latest"):

- Implemented in Rust, header generated with cbindgen (`include/accesskit.h`). MSRV: Rust 1.87.
- Covers every adapter above: `src/windows.rs`, `macos.rs`, `unix.rs`, `android.rs`, `ios.rs`.
  On Linux: `accesskit_unix_adapter_new(activation, action, deactivation)`,
  `accesskit_unix_adapter_update_if_active(factory)`, `..._update_window_focus_state`,
  `..._set_root_window_bounds` (X11 only: a Wayland client cannot know its window's position).
- The release asset is one archive, `accesskit-c-0.23.2.zip` (68 MB, SHA-256
  `c7fa84f8f34fca909f662375737a54f98b17cf9eae51fffd94ab3d9f662e3681`), with headers, static and
  shared libraries for every platform in the table, the sources, `Cargo.lock` and CMake files. The
  libraries are built by the project's CI (rustc 1.99.1 for 0.23.2, per the binaries' `.comment`).
- Built with LTO, `opt-level = "z"`, `panic = "abort"` and debug info.

## License

AccessKit and accesskit-c: MIT or Apache-2.0, at the user's option; code derived from Chromium
under BSD-3-Clause (`LICENSE.chromium`). All three are on AXION's allow-list.

The static library also carries its dependencies and the Rust standard library. For 0.23.2, over
every target the release ships: 112 crates (21 of the standard library), all usable under MIT,
Apache-2.0, Apache-2.0 with the LLVM exception (the standard library's `compiler_builtins`), Zlib
(`foldhash`) or Unicode-DFS-2016 (`unicode-ident`, pulled by `zvariant_utils` on Linux). The owner
added Unicode-DFS-2016 and its successor Unicode-3.0 to the allow-list on 2026-11-09. The list,
every copyright line and each license's text are in NOTICE, generated and checked by
`tools/licenses/accesskit_licenses.py` (decision 110 describes the procedure).

## How AXION consumes it

Decision 110. In short:

- The official release binary, pinned by version and SHA-256 and unpacked into `.deps/` like Dawn
  (decision 43), not cargo from CMake: no Rust toolchain for AXION's builds, and the release
  already covers five of the six targets.
- `AX_ACCESSKIT=AUTO|ON|OFF` (AUTO: on where a binary and a bridge exist, Linux x86_64 in M1).
  Without it everything builds and works the same: the tree, the headless queries, the tests.
- platform's adapter interface (`ax_a11y_adapter.h`) has its own types, since platform depends only
  on base; the runtime converts core's updates. The web's bridge will implement the same interface.

## Measured costs (Linux x86_64, AMD Ryzen 5 3400G, 8 threads)

| | |
| --- | --- |
| m1_nodes, Release, stripped | 17.81 MB without AccessKit, 19.76 MB with it: +1.95 MB |
| m1_nodes, Release, not stripped | 21.1 MB, 44.3 MB (the archive's debug info) |
| The prebuilt archive | 68 MB download (all platforms), cached by CI with the other pinned packages |
| Building accesskit-c from source instead (cargo, cold) | 1 min 08 s, 88 crates, 472 MB of `target/`; 24 s to rebuild after touching its `lib.rs` (LTO); a 1.6 GB Rust toolchain |

## What M1 checked on Linux

m1_nodes in a headless weston, with the session's AT-SPI bus enabled: the tree AT-SPI exposes is
the window (`frame "m1_nodes"`), the page heading and subtitle, and for each card an `article` named
by its title containing its `heading`, its paragraphs as `label`s and its `progress bar`. AT-SPI's
`click` action on a card reaches the app as `AX_WORLD_EVENT_A11Y_ACTION`, the card grows, and its
detail paragraph appears in AT-SPI's tree. Two gaps for M2's full bridge: AT-SPI does not show the
`expanded` state AccessKit receives for an article, and window coordinates are window-relative
under Wayland (the screen position is unknowable there; Orca's flat review is unaffected).
