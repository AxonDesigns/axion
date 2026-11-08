# AXION Architecture Design (C11 + WebGPU)

Nov 6, 2026 · @Brayan

## Goals, non-goals and principles

AXION (`ax_` prefix in C, `Axion` namespace in managed languages) is a C11 UI runtime built around a stable C ABI; every renderer, platform backend and language binding is a module on top of that ABI, so any of them can be swapped without touching the core.

**Goals**

- Runs standalone (owns the event loop and windows) or embedded (inside a host's native window, a host texture, or a host's WebGPU device).
- A complete windowing API: frameless and custom-chrome windows, transparency and system backdrops, window levels, multi-window and multi-monitor control, and native drag and drop within, between and out of windows, as far as each OS allows.
- WebGPU as the primary GPU API, reaching D3D12, Metal, Vulkan and the browser through its implementations, with a WebGL2/GLES 3 renderer and a CPU renderer as fallbacks.
- Antialiasing on everything drawn (shapes, paths, text, images, clips, shaders, effects, 3D), analytic in 2D and MSAA in 3D, consistent across all renderers.
- SVG and Lottie as first-class content, themeable and drivable by animations and gestures.
- Smooth at 60–120 Hz even when app code stalls: animations, scrolling and gestures never wait on user code or a garbage collector.
- Animations on by default for every animatable property, layout included, always interruptible and drivable by gestures.
- Headless: components ship behavior, state and accessibility, never a look. Accessibility is first-class: every component is complete for screen readers and keyboards on every platform before it ships.
- Authoring in TS/TSX, C#, Lua and Python, all generated from one interface description.
- Hot module replacement for code, styles, shaders and assets on every target, keeping app state across edits. Usable by developers, automated tools and AI agents alike: a headless host for tests, an automation protocol and an MCP server.
- Ships to Windows, macOS, Linux, Android, iOS and the web.

**Non-goals for v1**

- Wrapping native OS widgets.
- An immediate-mode API (one can be layered on the retained core later).
- Full feature parity on fallback renderers: user compute shaders need WebGPU, and the CPU renderer skips custom shaders.
- Full CSS compatibility; styling borrows CSS ideas, not its cascade.

**Principles**

1. Single writer per data structure. Threads exchange immutable snapshots and command queues, never shared mutable state.
2. Handles, not pointers, across every boundary: threads, bindings and the render IR.
3. No hidden allocation. Every module takes an allocator; per-frame work uses arenas reset each frame.
4. Data-oriented storage. Hot node data lives in dense arrays, cold data in side tables.
5. Executor-agnostic stages. Each pipeline stage is a plain function that can run on its own thread, as a job, or cooperatively on one thread (web without threads, embedded hosts, tests).
6. Determinism. A frame is a pure function of previous state, transactions, input and frame time, which enables record/replay and golden-image tests.
7. C11 for AXION's own code and public API; dependencies may be written in any language (C, C++, Rust) as long as they expose a C API and build for all six targets. Thin glue to an API that C can't reach comfortably (Objective-C for AppKit and UIKit, Kotlin or Java for Android, the C++ ThorVG shim) may use that API's language, kept behind a C interface.

## Layered architecture

The layers stack strictly downward: each module calls only the layers beneath it, and the scene (Core) and the renderers never see each other, only the Render IR between them.

&#91;embedded content: Kite layer stack, top to bottom\]

**Dependency rules**

- Base depends only on the C standard library and the OS threading primitives it wraps (pthreads, Win32); Platform depends only on Base; GPU depends on Base and on Platform's internal surface handles.
- Text depends only on Base plus the OS font APIs it calls directly (DirectWrite, Core Text, fontconfig, Canvas 2D on the web), never on Platform, so Core can use it for measurement and renderers for glyph rasterization.
- Core produces Render IR snapshots and renderers consume them. Neither includes the other's headers, which is what makes renderers interchangeable. Runtime wires them together: it depends on Core and the Render IR and reaches surfaces and renderers only through a presenter interface, so its single-threaded mode builds without Platform (the web, tests), while the platform presenter builds with it (decision 77).
- Kit is built only on Core's public C API, the same API the bindings use, so anything a built-in component does, an app can do too.
- Bindings and apps touch only the stable C ABI, never internal headers.
- Every module has two header levels: the stable ABI in `<module>/include/axion/`, with opaque types, and an internal SDK in `<module>/include/axion/<module>/` that only other runtime modules use, where types such as arenas, pools and mutexes are embeddable structs.

## Threading model and frame pipeline

Each thread owns its data outright, and only queues and immutable snapshots cross between them, so a slow app thread can delay *content* but never *motion*: scrolling, gestures and running animations keep their frame rate even during a GC pause.

&#91;embedded content: Threads and what crosses between them in one frame\]

The vsync starts each scene frame. It reaches whichever thread the platform delivers it to: on Wayland the render thread, which asks the scene for frame N+1 with its predicted present time as soon as it presents frame N (decision 78); the scene thread never waits on the app thread, and the render thread always draws the newest snapshot.

**Thread roles**

| Thread | Owns | Does each frame | Never |
| --- | --- | --- | --- |
| Main | OS windows and the event pump | Forwards input, and the vsync where the platform delivers it to this thread (CADisplayLink, Choreographer); creates windows (required on macOS, iOS and Android) | Runs app code or layout |
| App | The language VM and app state | Runs callbacks, builds and commits transactions | Touches scene or GPU state |
| Scene | Retained tree, styles, layout, animations, gesture state | Applies transactions, handles input, lays out, animates, records the Render IR | Waits on the app thread |
| Render | GPU device, pipelines, atlases, swap chains | Waits on every surface's vsync at once (on Wayland), encodes the render graph, submits, presents, and asks the scene for the next frame | Mutates the scene |
| Workers | Nothing persistent | Shaping, glyph rasterization, image decode, tessellation | Blocks on another thread |

**One frame, step by step**

1. Vsync for a surface arrives with the predicted present time T.
2. The scene thread drains the transaction queue. Commits that arrived before the deadline apply atomically; later ones wait for the next frame.
3. Input since the last frame is hit-tested, the gesture arena resolves, and driver-graph values update.
4. Style and layout run on dirty subtrees; independent relayout boundaries become parallel jobs.
5. Animations are sampled at T, including layout animations from the new frames.
6. Changed layers are recorded into the Render IR and published to a triple buffer with an atomic index swap.
7. The render thread takes the newest IR, encodes, submits and presents. If it is still busy, the scene overwrites the pending slot (mailbox style) rather than queueing, so latency stays bounded.

The scene works on frame N+1 while the render thread encodes frame N, so input reaches the screen within about two vsync intervals.

**Queues.** Lock-free single-producer rings carry transactions (app to scene) and events (scene to app); multi-producer queues carry worker results. Every transaction gets a sequence number, and `ax_txn_on_presented(seq, cb)` tells the app when that change is on screen.

**Single-threaded and hosted modes.** The same stage functions run in order on one thread through a cooperative scheduler for the web without cross-origin isolation, for hosts that call `ax_host_frame`, and for deterministic tests. In this mode smoothness depends on app code staying short, and the docs say so.

**Multiple surfaces.** One scene thread serves every surface, ticking each root on its own vsync, so a 60 Hz and a 120 Hz monitor each get their own cadence; the render thread presents each surface's swap chain separately.

## Core runtime conventions (C11)

The core targets the subset of C11 that MSVC, Clang, GCC and Emscripten all compile, and wraps whatever that subset lacks.

**Portability rules**

- No VLAs: they are optional in C11 and absent in MSVC.
- `<threads.h>` is missing on macOS and older MSVC, so `ax_thread`, `ax_mutex` and `ax_cond` wrap pthreads and Win32.
- `<stdatomic.h>` sits behind `ax_atomic.h`, because MSVC's C11 atomics still need `/experimental:c11atomics`; the wrapper falls back to Interlocked intrinsics.
- `_Static_assert`, `_Alignas`, `_Alignof` and `_Generic` are allowed; compiler extensions only behind macros.

**Handles, not pointers.** Every public object is a typed 64-bit handle (32-bit slot index, 32-bit generation). A stale handle fails a generation check instead of touching freed memory, which matters when garbage-collected languages release objects late or from finalizers.

```c
typedef struct ax_node    { uint64_t h; } ax_node;
typedef struct ax_surface { uint64_t h; } ax_surface;
typedef struct ax_font    { uint64_t h; } ax_font;
typedef struct ax_app    { uint64_t h; } ax_app;
typedef struct ax_window { uint64_t h; } ax_window;

typedef struct ax_str { const char *ptr; size_t len; } ax_str;  /* UTF-8, not NUL-terminated */

typedef struct ax_allocator {
    void *(*alloc)(void *ctx, size_t size, size_t align);
    void *(*resize)(void *ctx, void *ptr, size_t old_size, size_t new_size, size_t align);
    void  (*free)(void *ctx, void *ptr, size_t size);
    void  *ctx;
} ax_allocator;

typedef struct ax_window_desc {
    uint32_t    struct_size;     /* sizeof(ax_window_desc): lets the struct grow without breaking the ABI */
    const void *next;            /* optional extension chain, WebGPU style */
    ax_str      title;
    float       width, height;   /* logical units */
    float       min_width, min_height;
    uint32_t    decorations;     /* ax_decorations */
    uint32_t    backdrop;        /* ax_backdrop */
    uint32_t    level;           /* ax_window_level */
    uint32_t    flags;
} ax_window_desc;

typedef enum ax_result {
    AX_OK = 0,
    AX_ERR_INVALID_HANDLE,
    AX_ERR_WRONG_THREAD,
    AX_ERR_OUT_OF_MEMORY,
    AX_ERR_DEVICE_LOST,
    AX_ERR_UNSUPPORTED,
    AX_ERR_ABI_MISMATCH,
} ax_result;
```

**Memory.** Three tiers: the allocator the host supplies; per-thread frame arenas reset at each frame boundary; fixed-size pools for node component storage. No module calls `malloc` directly, so a host (a game engine, a test harness) can account for every byte.

**Errors.** Functions return `ax_result`, with detail from a thread-local `ax_last_error()`. No `setjmp`/`longjmp`. Debug builds assert on misuse (wrong thread, stale handle); release builds return the error. Bindings turn results into exceptions or error values.

**ABI.** Opaque types only. Descriptor structs carry `struct_size` plus a `next` extension chain. Every chained struct starts with an `ax_chain` header (`next`, a type tag and its own `struct_size`); a consumer walks the whole chain first, rejects an unknown required type with `AX_ERR_UNSUPPORTED` and skips only types marked optional (decision 73). Minor versions only append functions and fields; `ax_version()` reports the runtime version so bindings can check compatibility at load.

**Thread contract.** Every function is tagged in the IDL with the thread it may run on (`any`, `app`, `scene`, `render`), and debug builds check the tag on entry.

**One interface description.** A single IDL (a small `.axidl` file or annotated JSON) generates the C headers, API docs and all four bindings, so the languages never drift apart.

**Job system.** A fixed worker pool (core count minus the dedicated threads, minimum one) with work-stealing deques and counter-based waits. No fibers, so it ports to WASM unchanged.

## Platform layer: surfaces, windows, input and hosting

The platform layer separates three ideas the rest of the system never confuses: an **app** (process-level loop and services), a **window** (an OS window, optional) and a **surface** (anything that can be rendered into and receive input). A scene **root** attaches to a surface, so a window, a host texture and an offscreen target all look identical to the core.

**Hosting modes**

| Mode | Who owns the loop | Surface comes from | Input comes from | Typical use |
| --- | --- | --- | --- | --- |
| Standalone | AXION | AXION-created OS window | OS events via AXION backend | Regular apps |
| Hosted window | Host | Host's native handle (HWND, NSView, CAMetalLayer, ANativeWindow, wl\_surface, canvas) | Host forwards events, or AXION subclasses the view | Plugin panels, IDE extensions, Qt/SDL apps |
| Hosted texture | Host | Host's `WGPUDevice` + `WGPUTexture` | Host injects events in surface coordinates | Game engines, 3D tools, in-world UI |
| Offscreen | Caller | AXION-created texture or CPU image | Synthetic events | Tests, thumbnails, server-side rendering |

In every hosted mode the host implements a small `ax_host` vtable: a clock, a "request frame" callback, and event injection. The host may drive frames itself with `ax_host_frame(runtime, root, target_time)`, naming the surface by its root until hosted backends exist (decision 77), instead of letting AXION run its threads.

**Backends**: Win32; AppKit; Wayland (primary) and X11 on Linux; Android (GameActivity or a plain `SurfaceView`); UIKit with `CAMetalLayer`; the web (a `<canvas>` plus DOM events through Emscripten).

**Multi-window.** One scene world can own many roots, each on its own surface, sharing one GPU device, font collection and image cache. That allows dragging nodes between windows, and on desktop lets menus, tooltips and popovers become their own borderless OS windows so they can escape the parent's bounds. Each surface keeps its own scale factor, color space and vsync source, so a window moving between a 1x and a 2x monitor re-rasterizes text for the new scale.

**Input model**

- Unified pointer events for mouse, touch and pen (pointer id, pressure, tilt, twist, coalesced and predicted samples where the OS provides them).
- Keyboard events carry both the physical key and the logical key, plus modifiers and repeat.
- Scroll events carry phase and momentum flags, so trackpad scrolling and OS momentum feel native.
- A text-input protocol for IME: the platform asks for surrounding text, selection and caret rectangle; the core answers from the focused text node. This covers CJK composition, dictation and Android's `InputConnection`.
- OS-level gestures (trackpad pinch and rotate, Android predictive back) arrive as gesture events, not synthesized pointers.

**Frame clock.** Each surface gets its vsync from the native source: `CADisplayLink` on Apple, `Choreographer` on Android, DXGI waitable swap chains or `DwmFlush` on Windows, Wayland frame callbacks and presentation-time feedback, `requestAnimationFrame` on the web. The clock reports the *predicted present time* of the next frame, which is what animations sample, so 120 Hz ProMotion and variable refresh rates work without special cases.

**Accessibility.** The core keeps an accessibility tree (roles, names, states, actions, text ranges) next to the node tree; the platform layer bridges it to UI Automation, NSAccessibility, AT-SPI, Android `AccessibilityNodeInfo`, UIAccessibility, and a hidden ARIA-annotated DOM on the web. Since Rust dependencies are allowed, the plan is to adopt AccessKit through its C API wherever it has a platform adapter and write our own bridge only where it has none; iOS and web coverage is confirmed at M1.

## Windowing API

AXION offers one windowing API across platforms, with every feature reported through capability flags, because Wayland, mobile and the browser each restrict a different part of it; apps check a flag instead of checking the OS.

**Window kinds.** Normal; utility (no taskbar entry); popup (menus and tooltips: never takes focus, dismisses on outside click); dialog (owned by a parent, optionally modal); splash; overlay (transparent, click-through, always on top, for HUDs); layer surface (desktop-shell surfaces: panels, docks, launchers, notifications, wallpapers and HUD overlays). Owned windows minimize with their owner and stay above it.

**Layer surfaces.** A layer surface is anchored to a monitor's edges and stacked in one of four layers around normal windows, which ordinary windows cannot ask for. On Wayland it is a `zwlr_layer_shell_v1` surface; an `ext-` layer shell is preferred once one lands upstream. Other platforms leave `AX_CAP_WINDOW_LAYER_SURFACE` unset in v1, and AXION does not emulate it with ordinary windows: creating a window with a chained layer description where the capability is missing fails with `AX_ERR_UNSUPPORTED`, so the app picks its own fallback. An app asks for one by chaining an `ax_window_layer_desc` through `ax_window_desc.next`, with the chain header every extension struct uses (decision 73 in the decisions log):

- `layer`: `BACKGROUND`, `BOTTOM`, `TOP` or `OVERLAY`.
- `anchor`: any combination of the top, bottom, left and right edges.
- `width`, `height`: the size in logical units. 0 on an axis stretches the surface between the two opposite edges of that axis, which must both be anchored.
- `margin`: top, right, bottom and left, in logical units, from the anchored edges.
- `exclusive_zone`: the space other surfaces must leave free along the anchored edge (a panel's height); 0 for none, -1 to ignore other surfaces' zones.
- `keyboard`: `NONE`, `EXCLUSIVE` (takes all keyboard input while on `TOP` or `OVERLAY`, as a launcher does) or `ON_DEMAND` (focus follows normal clicks).
- `output`: the monitor to appear on, or none to let the compositor choose.
- `layer_namespace`: a fixed string naming the surface's purpose (`"panel"`, `"notifications"`), which compositors use for rules and effects. The field avoids the name `namespace`, which C++ reserves.

```c
ax_window_layer_desc layer = {
    .chain = {.type = AX_STYPE_WINDOW_LAYER_DESC, .struct_size = sizeof layer},
    .layer = AX_LAYER_TOP,
    .anchor = AX_ANCHOR_TOP | AX_ANCHOR_LEFT | AX_ANCHOR_RIGHT,
    .height = 32,                 /* width 0: stretched between the left and right edges */
    .exclusive_zone = 32,
    .keyboard = AX_LAYER_KEYBOARD_ON_DEMAND,
    .layer_namespace = AX_STR("panel"),
};
ax_window_desc desc = {.struct_size = sizeof desc, .next = &layer};
```

The title, decorations, level, size, minimum size and `AX_WINDOW_RESIZABLE` do not apply to a layer surface: it is never decorated, and its size comes from the layer desc and its anchors. A width or height of 0 needs both opposite edges anchored; `ax_window_create` and `ax_window_set_layer` reject anything else with `AX_ERR_INVALID_ARGUMENT` rather than let the compositor end the connection with a protocol error. `ax_window_set_layer` changes the layer, anchors, margins, exclusive zone, keyboard mode and size after creation. These changes are double-buffered: the main thread sends them and requests a commit, and they reach the compositor with the presenter's next commit (decision 35). `output` and `layer_namespace` are fixed at creation, because the protocol cannot change them.

The configure contract of decisions 34 and 54 does not change. `zwlr_layer_surface_v1.configure` publishes an `ax_surface_config` like `xdg_surface.configure`, and the presenter acknowledges it, through a per-role function, in the same commit as a buffer of that size. Caption and resize hit regions do nothing on a layer surface, since the compositor places it. Layer surfaces send no occlusion events, because the protocol has no suspended state. `closed` maps to a close request, which the app cannot cancel: the compositor has already removed the surface, and the app destroys the window. A layer surface can draw a lock screen, but it does not lock the session; that needs `ext-session-lock-v1`, a separate decision.

**Decorations and custom chrome**

- `NATIVE`: the OS title bar and frame.
- `NONE`: fully frameless; the app draws everything, and AXION keeps the native shadow, rounded corners and snap behavior wherever the OS allows a frameless window to have them.
- `CUSTOM_TITLEBAR`: the app draws the title bar while native behavior stays. On Windows the client area extends into the frame, keeping the shadow, snap and minimize animations; on macOS the title bar turns transparent over full-size content and the traffic-light buttons can be repositioned; on Wayland AXION draws client-side decorations when the compositor offers none (GNOME), and requests server-side ones where it does (KDE).

**Hit-test regions.** Any node can declare itself a window region: caption (drags the window), resize edge or corner, minimize, maximize, close, or client. The scene thread publishes this region map with every frame to the main thread, so OS hit-test queries (such as `WM_NCHITTEST`) are answered instantly from the latest map without waiting on the scene or app thread. Tagging the maximize button as `MAXIMIZE` is what makes the Windows 11 snap-layouts flyout appear on hover; on macOS and Linux the same map triggers native window drags and resizes on pointer down, and a double-click on the caption follows the user's system preference.

```c
ax_window_desc desc = {
    .struct_size = sizeof desc,
    .title       = AX_STR("Studio"),
    .width = 1280, .height = 800, .min_width = 640, .min_height = 400,
    .decorations = AX_DECORATIONS_CUSTOM_TITLEBAR,   /* NATIVE | NONE | CUSTOM_TITLEBAR */
    .backdrop    = AX_BACKDROP_MICA,                 /* NONE | TRANSPARENT | BLUR | MICA | ACRYLIC | VIBRANCY */
    .level       = AX_LEVEL_NORMAL,                  /* NORMAL | FLOATING | ALWAYS_ON_TOP */
    .flags       = AX_WINDOW_RESIZABLE | AX_WINDOW_RESTORE_STATE,
};
ax_window win;
ax_window_create(app, &desc, &win);

/* Window regions are ordinary node properties. */
ax_txn_set_enum(t, titlebar, AX_PROP_WINDOW_REGION, AX_REGION_CAPTION);
ax_txn_set_enum(t, max_btn,  AX_PROP_WINDOW_REGION, AX_REGION_MAXIMIZE);  /* Windows 11 snap layouts */
```

**Appearance.** Per-pixel transparent windows through a premultiplied-alpha swap chain; system backdrops (Mica and Acrylic on Windows 11, vibrancy materials on macOS, compositor blur where a Wayland or X11 compositor offers it) that fall back to a translucent fill elsewhere; light or dark title bars that follow the theme; window opacity.

**Control and state.** Position, size, min/max size and aspect-ratio lock; minimize, maximize, restore; borderless fullscreen per monitor and macOS native fullscreen spaces; window levels; activate and request attention (taskbar flash, dock bounce); click-through; exclusion from screen capture; cursor shape, hiding, confinement and relative mouse mode for 3D cameras; window icon; taskbar and dock progress and badges. `AX_WINDOW_RESTORE_STATE` saves and restores position, size and maximized state, validated against the current monitor layout so a window never reopens off screen.

**Monitors.** Enumeration with bounds, work area, scale factor, refresh rate, HDR capability and color space, plus change notifications; per-monitor DPI awareness on Windows.

**Events.** Moved, resized, close requested (cancellable by the app), focus, occlusion (rendering pauses while a window is hidden), scale changed, theme changed, and drag-and-drop (see the next section). During live resize, which Windows and macOS run in a modal loop on the main thread, the main thread waits a bounded time for a frame at the new size, so content never stretches or lags behind the frame edge.

**Resize on Wayland.** There is no modal loop. After the first empty commit, only the presenter commits, and it acknowledges each configure in the same commit as a buffer of the new size. A frame never acknowledges a configure without content of that size: it waits for the scene up to half a refresh (2–8 ms), then draws the newest snapshot unstretched, anchored top-left. Pacing follows the compositor's frame callbacks with a MAILBOX swapchain by default. With FIFO, commits can queue behind the compositor's barrier, so a resize first drains them.

**Platform services** (desktop, a first-party plugin): tray and status-bar icons with menus, the macOS application menu bar, and global shortcuts. They live outside the core so headless and embedded builds don't carry them.

**What each platform allows**

| Feature | Windows | macOS | Linux X11 | Linux Wayland | Android / iOS | Web |
| --- | --- | --- | --- | --- | --- | --- |
| Frameless and custom title bar | Yes | Yes, native buttons kept | Yes | Yes, client-side decorations | Not applicable | Not applicable |
| Set global window position | Yes | Yes | Yes | No, the compositor places windows | No | No |
| System backdrop blur | Mica, Acrylic (Windows 11) | Vibrancy | Compositor-dependent | Compositor-dependent | No | No |
| Always on top | Yes | Yes | Yes | No standard protocol; a layer surface on the overlay layer, where the compositor supports layer-shell | No | No |
| Multiple windows | Yes | Yes | Yes | Yes | iPad scenes, Android multi-window | One canvas; extra windows become in-page layers |
| Exclude from screen capture | Yes | Yes | No | No | Android only | No |
| Layer surfaces (panels, docks, overlays) | Not in v1 (AppBars could cover docks and panels) | Not in v1 (window levels, no reserved space) | Not in v1 (EWMH dock windows with struts could cover panels) | Compositor-dependent (KDE, wlroots compositors, Hyprland, COSMIC, niri; not GNOME) | No | No |

## Drag and drop

One drag-and-drop API covers three scopes: within a window, between AXION windows, and to or from other apps and the OS. A drag moves between scopes without the app noticing, and inside AXION windows its preview is always a live, animated node.

**The three scopes**

- **Within a window.** Handled entirely on the scene thread: a gesture recognizer starts the drag, the preview is a live node in the overlay layer that lifts and settles with springs, and targets highlight, reorder and auto-scroll without an app-thread round trip.
- **Window to window.** The session stays inside AXION. The preview travels in the same transient overlay window used for cross-window heroes, and the payload passes by reference, so a node can move to another window intact (tear-off tabs, moving panels). Dropping outside every window fires `OnDropOutside`, where the app can open a new window at the drop point holding the dragged node. On Wayland, where no overlay window can follow the pointer between windows, the preview becomes the wl\_data\_device drag icon outside the source window, and a torn-off window opens wherever the compositor places it.
- **Other apps and the OS.** If the payload has exportable representations, AXION starts a native drag session at drag start. While the pointer is over AXION windows, AXION draws its own live preview; once it leaves, the OS drag image takes over. Drops from other apps (files, text, images, URLs) arrive through the same drop-target API.

**Payload.** A drag carries several representations at once: plain text, URLs, files, images, and app-defined types (`application/x-myapp-card`), with allowed operations copy, move and link. Expensive representations are produced lazily and asynchronously on the app thread, including file promises that are only written when another app accepts the drop (`NSFilePromiseProvider` on macOS, delayed rendering on Windows, item providers on iOS).

```csharp
new Card {
    Draggable = new DragSource {
        Data = () => new DragData()
            .Text(card.Title)
            .Custom("application/x-myapp-card", card.Id)
            .FilePromise("card.pdf", stream => card.ExportPdfAsync(stream)),
        Operations = DragOperations.Copy | DragOperations.Move,
        Preview    = DragPreview.Self,          // the live node, lifted with a spring
    },
};

new Column {
    DropTarget = new DropTarget {
        Accepts = ["application/x-myapp-card", DragType.Files],
        OnDrop  = e => board.InsertAsync(e.Data, e.InsertionIndex),
    },
};
```

**Threading.** Hover hit-testing, target highlighting and auto-scroll run on the scene thread; the drop event and data delivery go to the app thread. On Windows, the OS drag loop is modal on the main thread, which is fine because the scene and render threads keep animating.

**Accessibility and automation.** Every drag source and target is also reachable by keyboard (pick up, move between targets, drop or cancel) and through accessibility actions. The testing API and the MCP `drag` tool drive the same sessions, and the headless host can inject OS drops with files or text.

| Platform | In-window and window-to-window | To and from other apps | Native API |
| --- | --- | --- | --- |
| Windows | Yes | Yes | OLE `DoDragDrop`, `IDropTarget` |
| macOS | Yes | Yes | `NSDraggingSession`, `NSDraggingDestination` |
| Linux X11 | Yes | Yes | XDND |
| Linux Wayland | Yes | Yes | `wl_data_device`, with AXION supplying the drag-icon surface |
| Android | Yes | Between apps in split-screen and freeform windows | `View.startDragAndDrop`, `OnDragListener` |
| iOS, iPadOS | Yes | Between apps on iPad | `UIDragInteraction`, `UIDropInteraction` |
| Web | Within the canvas | Files and text in; limited types out | HTML drag-and-drop events, `DataTransfer` |

## GPU layer and decoupled renderer

Rendering is decoupled at one seam: the scene produces an immutable, versioned **Render IR** snapshot, and any renderer that implements `ax_renderer` turns it into pixels. Nothing above that seam calls WebGPU; above it, WGSL is only the shader source format every renderer accepts.

**WebGPU implementation.** The GPU layer codes against the standard `webgpu.h` C header. Decision: Dawn is the primary native implementation, because the browser build (Emscripten's `emdawnwebgpu` port) shares its code and header revisions; wgpu-native stays compiling in CI as the alternative, and at M1 Dawn's frame times, startup time and binary size are measured against it before the choice is locked. Through them we reach D3D12 on Windows, Metal on Apple, Vulkan on Linux and Android (GLES as a fallback where Vulkan drivers are poor), and the browser's WebGPU on the web.

**What `ax_gpu` adds on top of `webgpu.h`**

- Device lifecycle, including device-lost recovery: every GPU resource is recreated from its descriptor, which Android and iOS need after backgrounding.
- Pipeline cache keyed by a hash of pipeline state, with async pipeline creation and a warm-up list recorded on previous runs, so first-use shader compiles never cause a dropped frame.
- Upload ring buffers for per-frame vertex, instance and uniform data; bind-group caching.
- A per-frame **render graph**: passes declare their reads and writes (offscreen layers, blur chains, backdrop captures, 3D passes, user compute), and the graph orders them, culls unused ones and aliases transient textures.
- Surface management per window: format, present mode, color space (sRGB, Display P3, extended range for HDR where the platform allows).

**Render IR.** Plain-old-data, versioned, and serializable, so it can be recorded for replay tests, inspected by devtools, or sent to another process. It contains:

- A layer tree: each layer has a transform (2D affine or 3D with perspective), clip (rect, rounded rect or path), opacity, blend mode and an optional effect chain.
- Draw items per layer: rects and rounded rects with per-corner radii, borders, box shadows, gradients (linear, radial, conic), paths, images (with nine-slice), glyph runs (font handle, glyph ids, positions), user shader quads, backdrop-filter regions, and 3D viewports referencing a 3D scene.
- Damage information: which layers and regions changed since the previous snapshot.
- Encoding (IR 1.0, built in M0): little-endian records, with layers in pre-order carrying their parent and subtree end. Every layer and item record carries its own size, so a newer minor version can append fields and item types; a consumer skips an unknown item, or drops its whole layer when the item is flagged REQUIRED. Colors are premultiplied float RGBA in linear light with sRGB primaries and extended range, and each layer's blend space decides how they blend. Coordinates are logical units, and the snapshot records the logical size it was built for, which a renderer never stretches to another size.

```c
typedef struct ax_renderer_vtbl {
    uint32_t   struct_size;
    ax_result (*create)(const ax_renderer_desc *desc, void **out_self);
    void      (*destroy)(void *self);
    ax_result (*attach_surface)(void *self, ax_surface surface);
    void      (*detach_surface)(void *self, ax_surface surface);
    ax_result (*render)(void *self, ax_surface surface, const ax_render_ir *ir, ax_frame_info info);
    void      (*query_caps)(void *self, ax_renderer_caps *out_caps);  /* compute, HDR, MSAA, ... */
    /* Added in M0: how the render thread waits for its next frame depends on the renderer
       (the swapchain's pacing, and polling the device so a lost one is recreated). */
    ax_result (*wait_frame)(void *self, ax_surface surface, bool animating,
                            uint64_t timeout_ns, ax_wake_reason *out_reason);
} ax_renderer_vtbl;
```

**Renderers planned**

| Renderer | Approach | Purpose |
| --- | --- | --- |
| `ax_render_wgpu` (default) | Instanced SDF primitives for rects, rounded rects, borders and shadows; analytic-coverage paths (sparse strips); glyph atlas | Production on every target with WebGPU |
| `ax_render_gl` | The same primitive set on WebGL2 and GLES 3.0; user WGSL translated to GLSL ES | Fallback for browsers without WebGPU and for Android and Linux devices without usable Vulkan |
| `ax_render_soft` | CPU rasterizer, tiled, SIMD and multi-threaded | Last-resort fallback, golden-image tests, no-GPU CI and servers |
| `ax_render_compute` (later) | Fully compute-based 2D in the style of Vello | Heavy vector content, large path counts |

**Fallback chain.** Each surface picks its renderer at creation, WebGPU first, then GL (when the render\_gl plugin is linked), then software, unless the app pins one. The chosen renderer reports its capabilities (compute, HDR, MSAA, backdrop support, max texture size), and the core exposes them to style rules and app code, so a glass panel can declare a plain translucent fill for renderers without backdrops. Renderers can also be swapped at runtime, for example after a device loss, because they only ever see the IR.

The GL renderer talks to WebGL2 or GLES directly rather than through `ax_gpu`; it shares CPU-side glyph rasterization, image decoding and the Render IR with the other renderers. On native Android devices without usable Vulkan, AXION uses both paths: Dawn's compatibility mode on GLES 3.1+, which keeps the WebGPU renderer and its compute support, and the GL renderer on GLES 3.0-only devices.

**Damage and caching.** Static subtrees can be cached as retained layer textures and recomposited without re-drawing; composite-only changes (transform, opacity, shader uniforms) never re-record draw items. This is what keeps animations cheap.

## User shaders, backdrops, compute and 3D

Users write shaders in AXGL, the WGSL superset described in the next section, or as plain WGSL functions against a small prelude; AXION wraps them in its own vertex stage, bind-group layout and batching, so a custom shader is as easy to attach as a background color and its uniforms animate like any other property.

**Shader kinds**

| Kind | Entry point the user writes | Reads | Attaches to |
| --- | --- | --- | --- |
| Fill | `fn ax_fill(in: AxFill) -> vec4f` | Local coords, size, time, uniforms, textures | A node's background, foreground or mask |
| Backdrop | `fn ax_backdrop(in: AxFill, behind: texture_2d<f32>) -> vec4f` | Fill inputs plus a capture of what is behind the node | Frosted glass, refraction, magnifiers |
| Layer effect | `fn ax_effect(in: AxFill, content: texture_2d<f32>) -> vec4f` | The node's own rendered subtree | Ripples, distortion, dissolve, page curls |
| Compute | Any `@compute` entry point | Declared buffers and textures | A render-graph pass; outputs feed textures, vertices or particle buffers |
| Custom pass (C only) | A render-thread callback with an encoder and target | Anything | Escape hatch for native integrations |
| Material | `fn surface(inout s: Surface)` in a `shader material` | glTF material inputs, mesh UVs and normals, uniforms | A mesh in a Viewport3D; AXION's PBR lighting runs after it |

```wgsl
// User-authored fill shader: an animated ring. `progress` is a uniform
// that the animation system can drive with a spring or a gesture.
struct Params { color: vec4f, progress: f32, thickness: f32 };
@group(2) @binding(0) var<uniform> p: Params;

fn ax_fill(in: AxFill) -> vec4f {
    let uv = in.local / in.size - 0.5;
    let r  = length(uv) * 2.0;
    let a  = atan2(uv.y, uv.x) / 6.2831853 + 0.5;
    let aa   = 2.0 * in.aa / min(in.size.x, in.size.y);   // one device pixel in r units
    let ring = 1.0 - smoothstep(p.thickness - aa, p.thickness, abs(r - 0.8));
    return p.color * ring * step(a, p.progress);
}
```

**Uniform reflection.** AXION parses the WGSL `@group(2)` bindings and struct layout, then exposes each field as a typed node property (`shader.progress`, `shader.color`). Those properties go through the same transaction, style and animation paths as `opacity`, and changing them is composite-only: no layout, no re-recording.

**Backdrops cost a capture.** A backdrop node forces the renderer to finish drawing everything behind it, copy that region (plus padding for blur radius) to a texture, then run the shader. The renderer merges overlapping backdrop regions per frame and the docs state the cost model, because ten independent glass panels mean ten captures.

**Authoring other languages.** WGSL is the runtime language. Tooling can compile Slang, GLSL or HLSL to WGSL offline (Slang's WGSL target, Tint or Naga), and WESL-style `import` lets shaders share modules. Compile errors come back to the app thread with line numbers mapped to the user's source; dev builds hot-reload shaders on save.

**Shaders on fallback renderers.** The GL renderer gets user WGSL translated to GLSL ES 3.0 at build time (Naga or Tint), so fill, backdrop and layer-effect shaders run unchanged. WebGL2 has no compute shaders or storage buffers, so compute passes are skipped there and their nodes use a declared fallback; 3D scenes use environment maps prefiltered at export time instead of by compute. The software renderer skips custom shaders and draws each node's fallback style.

**3D and PBR.** A `Viewport3D` node renders a 3D scene into a layer that composites with the UI like any other layer, and UI subtrees can be placed in 3D as textured planes with ray-cast hit testing.

- Assets: glTF 2.0 through cgltf, KTX2/Basis Universal textures.
- Materials: glTF metallic-roughness PBR first; clearcoat, transmission and sheen extensions later. Users can adjust a material's surface inputs (base color, normal, roughness, metallic, emission) with a material shader, the same way as fill shaders.
- Lighting: directional, point and spot lights; image-based lighting with environment maps prefiltered by compute passes; cascaded shadow maps for the directional light.
- Output: linear-light rendering, then tone mapping (Khronos PBR Neutral as default for product shots, AgX or ACES selectable) into the surface's color space; MSAA 4x by default.
- Camera, lights, transforms and material parameters are ordinary animatable properties, so a 3D object can spring, follow a gesture, or fly as a hero between routes.

## AXGL: the AXION shader language

AXGL (`.axgl` files) is a strict superset of WGSL that removes the boilerplate around UI shaders: any valid WGSL is valid AXGL, and AXGL compiles to plain WGSL (and from there to GLSL ES for the GL renderer); only the physics kind (see Scrolling) compiles to scene-thread bytecode instead, so it never limits what a shader can do. That holds only if AXGL's new words (shader, import, color and the built-in inputs) are contextual, so a WGSL identifier with the same name never breaks.

**A fill shader in AXGL**

```axgl
// ring.axgl
shader fill Ring;

import axion.sdf;
import axion.math;

@prop color: color = #4f7cff;                   // themeable: accepts tokens like var(--accent)
@prop progress: f32 = 0.0 range(0, 1);          // becomes Ring.Progress, animatable
@prop thickness: f32 = 0.08 range(0.01, 0.5);
@prop glow: f32 = 0.0 range(0, 1);

fn main() -> vec4f {
    let r     = 0.5 * min(size.x, size.y);      // work in local units, where aa is one device pixel
    let p     = local - 0.5 * size;             // uv, local, size, aa, time: built-ins
    let d     = abs(sdf.circle(p, 0.8 * r)) - thickness * r;
    let ring  = aa_fill(d);                     // antialiased edge, one device pixel wide
    let sweep = step(math.angle01(p), progress);
    let halo  = glow * exp(-8.0 * max(d, 0.0) / r);
    return color * (ring * sweep + halo);
}
```

The compiler turns this into a WGSL module with AXION's prelude, the vertex stage, a correctly aligned uniform struct at `@group(2)`, the `ax_fill` entry point and reflection data for the bindings.

**What AXGL adds to WGSL**

| Addition | Example | Compiles to |
| --- | --- | --- |
| Shader header | `shader fill Ring;` (`fill`, `backdrop`, `effect`, `compute`, `material`, `physics`) | Entry point, prelude, vertex stage and bind-group layout for that kind |
| Properties | `@prop progress: f32 = 0.0 range(0, 1);` | Uniform struct field plus reflection (type, default, range, unit), exposed as a typed, animatable node property |
| Built-in inputs | `uv`, `local`, `size`, `aa`, `time`, `dt`, `pointer`, `dpr`, `opacity` | Fields of the prelude's input struct, no declarations needed |
| Color type and literals | `color`, `#4f7cff`, `rgb(...)`, `rgba(...)`, `oklch(...)` | Linear, premultiplied `vec4f`; theme tokens resolve at runtime |
| Images | `@prop pattern: image;` then `pattern.sample(uv)` | Texture and sampler pair; the property takes an asset path or another node's snapshot |
| Imports | `import axion.sdf;` or `import "./common.axgl";` | Inlined, namespaced, with unused functions removed |
| Ternary | `a > 0.5 ? x : y` | `select(y, x, a > 0.5)` (both branches are evaluated) |
| Default arguments and overloads | `fn glow(d: f32, k: f32 = 8.0)` | Name-mangled WGSL functions |
| Variants | `@variant hq: bool = false;` then `if (hq) { ... }` | Separate pipeline permutations with dead branches removed |
| Enum properties | `@prop mode: enum(Linear, Radial) = Linear;` | A variant, or a `u32` when marked `dynamic` |
| Compute and material entry points | `@workgroup(64)`, `fn main(i: u32)`, `@buffer`, `inout` parameters | `@compute @workgroup_size(64)` with `global_invocation_id`, storage-buffer bindings, `ptr<function, T>` parameters |

**Standard library** (all modules written in AXGL, with the AA helpers built on `aa`)

- `axion.sdf`: circles, boxes, rounded boxes, segments, polygons, arcs, smooth union, intersection and subtraction.
- `axion.noise`: value, Perlin, simplex and Worley noise, fBm.
- `axion.color`: sRGB and linear conversion, Oklab and Oklch mixing, HSV, contrast helpers.
- `axion.math`: remap, rotate, angle helpers, easing curves matching the animation system's.
- `axion.aa`: `aa_fill`, `aa_stroke`, `aa_step` (also in scope by default); their distances are in local units, the same space as aa.
- `axion.backdrop` (backdrop shaders only): `backdrop.sample(uv)`, `backdrop.blurred(uv, radius)` using the renderer's downsample chain, and `shape_sdf(local)` for the node's own rounded rectangle, plus backdrop.edge\_normal(d) for refraction and edge lighting.
- `axion.physics` (physics kind only): `in_bounds`, `spring_to_edge`, plus decay and rubber-band helpers matching the built-in presets.

**Other shader kinds**

```axgl
// glass.axgl
shader backdrop Glass;

@prop blur: f32 = 16.0 range(0, 64) unit(px);
@prop tint: color = rgba(255, 255, 255, 0.12);
@prop refraction: f32 = 0.0 range(0, 1);

fn main() -> vec4f {
    let n      = backdrop.edge_normal(shape_sdf(local));
    let behind = backdrop.blurred(uv + n * refraction * 0.02, blur);
    return tint + behind * (1.0 - tint.a);   // tint is premultiplied
}
```

```axgl
// particles.axgl
shader compute Particles;

struct Particle { pos: vec2f, vel: vec2f, life: f32 };
@buffer particles: array<Particle>;             // sized by the node
@prop gravity: vec2f = vec2f(0.0, 980.0);

@workgroup(64)
fn main(i: u32) {
    if (i >= arrayLength(&particles)) { return; }   // the dispatch rounds up to 64
    var p = particles[i];
    p.vel += gravity * dt;
    p.pos += p.vel * dt;
    p.life -= dt;
    particles[i] = p;
}
```

A `material` shader writes a `surface(inout s: Surface)` function that adjusts base color, normal, roughness, metallic and emission, and AXION's PBR lighting does the rest.

**Using shaders from each language.** The compiler's reflection generates a typed class per shader in every binding, so properties are checked at compile time and animate like any other property.

```csharp
var ring = new Shaders.Ring { Color = Tokens.Accent, Thickness = 0.06f };
new View { Fill = ring };
ring.Animate(r => r.Progress, 1f, Spring.Smooth);   // composite-only: no layout, no re-record
```

```tsx
import { Ring } from "./ring.axgl";
<View fill={<Ring color="var(--accent)" progress={progress()} />} />
```

```css
.loader { background: shader("ring.axgl", progress: 0.4, color: var(--accent)); }
```

**Compiler pipeline (`axglc`).** A C library plus a command-line front end:

1. Parse AXGL, resolve imports, check types.
2. Generate WGSL with the prelude, bindings and entry point, and validate it with Tint.
3. For the GL renderer, translate to GLSL ES 3.0; compute shaders are reported as unavailable there, and the node uses its declared fallback.
4. For the physics kind, emit sandboxed scene-thread bytecode (no allocation, bounded loops, deterministic) instead of WGSL.
5. Emit reflection (properties, types, defaults, ranges, units, variants) for the bindings, devtools and the MCP server.
6. Emit a source map, so every error and warning, including those from Tint, points at the original `.axgl` line.

In dev builds `axglc` runs inside the app for HMR. Release builds ship precompiled WGSL, GLSL and reflection, with no compiler inside, unless the app opts into runtime compilation.

**Tooling.** A language server gives completion, hover docs, go-to-definition and live diagnostics, with a VS Code extension and syntax highlighting for other editors. Devtools show each shader's properties as live sliders, and MCP agents can read the reflection to set or animate them.

## Text rendering

Text uses proven C-callable libraries for the hard Unicode work and owns only the parts that decide quality and speed: run caching, line layout, atlas management and the hybrid rasterization strategy.

**Pipeline** (shaping runs on worker threads; only atlas upload happens on the render thread)

1. **Itemize**: split the string into runs by bidi level (UAX #9), script, style span and font. Font fallback picks a font per grapheme cluster from the style's family list, then the platform fallback list.
2. **Shape**: each run goes through HarfBuzz to get glyph ids, advances, offsets and cluster mapping. Results are cached by (text, font, size, features, variation axes).
3. **Break and lay out lines**: line-break opportunities from UAX #14, then greedy or optimal wrapping. Lines are re-wrapped *without re-shaping* where HarfBuzz marks glyphs safe to break, so resizing a paragraph is cheap.
4. **Position**: alignment, justification, tab stops, baseline grid, inline objects (icons, attachments) and paragraph metrics for layout measurement.
5. **Rasterize**: glyphs are drawn into atlases on demand (see below) and the paragraph becomes glyph-run draw items in the Render IR.

**Libraries**

| Job | Library | Notes |
| --- | --- | --- |
| Font parsing, outlines, bitmaps, COLR paint | FreeType | Handles TrueType, CFF, variable fonts, COLRv0/v1, CBDT and sbix |
| Shaping | HarfBuzz | C API; also used for font subsetting in the export tool |
| Bidi | SheenBidi | Pure C, UAX #9 |
| Line and grapheme breaking | libunibreak | Pure C, UAX #14 and #29 |
| System fonts and fallback lists | DirectWrite, CoreText, fontconfig, Android `fonts.xml` | Enumeration and fallback; color emoji on Windows and Apple are also drawn through them (see Emoji), everything else is rendered by AXION |

On the web there is no system font access, so apps bundle fonts and AXION ships a subsetted Noto fallback set for missing scripts; emoji still come from the platform (see Emoji below).

**Hybrid rasterization**

- Small, stable text (up to roughly 48 device pixels): coverage glyphs rasterized at the exact pixel size, with 4 horizontal subpixel positions, light or no hinting, and gamma and contrast correction tuned to match platform text. This is where quality is won or lost.
- Large text, or text inside a scale or 3D transform: either multi-channel signed distance fields or direct GPU path rendering of outlines, chosen per renderer capability.
- During scale animations the cached raster is scaled and re-rasterized once the animation settles, with hysteresis so text does not flicker between modes.
- Color glyphs (emoji, COLRv1 gradients) go into a separate RGBA atlas; monochrome glyphs into A8 atlases. Atlases are paged with LRU eviction.
- LCD subpixel antialiasing is off by default, since it breaks under transforms and transparent layers, and can be enabled per surface on Windows.

**Emoji.** Emoji use the platform's own emoji font by default, so they look exactly like the rest of the OS, and each platform's color glyphs are rasterized with whichever API reads that font's format best.

| Platform | System emoji font | Color format | How AXION rasterizes it |
| --- | --- | --- | --- |
| Windows | Segoe UI Emoji | COLR color layers | DirectWrite color glyph runs |
| macOS, iOS | Apple Color Emoji | sbix bitmaps | Core Text drawing into the atlas |
| Android | Noto Color Emoji | COLRv1 on recent versions, CBDT bitmaps on older ones | FreeType for CBDT bitmaps; COLRv1 paint is read through FreeType and drawn by AXION's path rasterizer, since FreeType does not render COLRv1 |
| Linux | fontconfig's emoji family, usually Noto Color Emoji | CBDT or COLRv1, by distribution | FreeType for CBDT; COLRv1 drawn by AXION, as on Android |
| Web | The browser's emoji font | Not readable as a file | Canvas 2D `fillText` on an `OffscreenCanvas` in a worker, copied into the atlas |

- **Sequences.** Itemization follows Unicode emoji rules (UTS #51) and extended grapheme clusters, so ZWJ sequences, skin-tone modifiers, flags, keycaps and variation selectors stay one cluster and one glyph. Text presentation (VS15) uses a monochrome glyph when a font has one.
- **Missing emoji.** When the system font lacks a newer emoji, AXION does what the OS does and shows the component emoji, unless the app ships a fallback emoji font.
- **Emoji policy.** `emoji_font` is `system` (default), `bundled` (a shipped Noto Color Emoji, identical on every platform) or a custom font.
- **Sizes.** Bitmap emoji are drawn from the nearest larger strike and cached per device size; COLRv1 emoji stay sharp at any size and can scale in animations.
- **Input and editing.** The OS emoji pickers (Win+., the macOS Character Viewer, mobile keyboards) work through the text-input protocol, and caret movement, selection and deletion treat a whole emoji sequence as one character.
- **Accessibility.** Screen readers receive the emoji characters and announce their names as on any native control.

**Editing.** The text node supports selection, caret movement by grapheme and word, rich text spans, undo, and the IME protocol from the platform layer. Accessibility exposes text ranges so screen readers can read by character, word and line. Variable-font axes such as weight are animatable properties.

## Vector graphics: SVG and Lottie

AXION uses ThorVG to load and animate SVG and Lottie, but draws the result through its own renderers, so vector art gets the same antialiasing, batching, damage tracking and fallback renderers as everything else.

**Why ThorVG.** It is a compact, portable C++ vector engine with a C API, built for UI and embedded use, with loaders for SVG and Lottie and active work on Lottie conformance. Skia's Skottie would pull in all of Skia, and rlottie sees little active development.

**How it is integrated**

1. The asset loader parses SVG or Lottie with ThorVG on a worker thread.
2. A thin C++ shim walks ThorVG's scene (its `Accessor` API) and converts each shape into Render IR draw items: paths, fills, strokes, gradients, clips, masks, blend modes and images. A static SVG converts once.
3. For Lottie, a worker evaluates the animation at each frame's predicted present time, and only shapes that changed are re-emitted; damage tracking limits redraw to the changed region.
4. Anything the IR cannot express yet falls back to ThorVG's own rasterizer into a cached layer texture, and devtools flag it so the gaps stay visible.

**SVG**

- Usable as an image source (`<Image src="icon.svg"/>`) or as an `Svg` node whose parts are addressable: `currentColor` and named elements bind to node properties, so icons follow theme tokens and their colors, strokes and transforms animate like any other property.
- Covers static SVG as ThorVG supports it: paths and basic shapes, strokes with dashes, caps and joins, linear and radial gradients, clip paths, masks, opacity, transforms and embedded images. `<text>` is shaped by AXION's text engine so it matches the rest of the UI.
- Not supported: scripting, SMIL and CSS animations (use Lottie or AXION's own animations). Filter effects map to AXION effects where they exist (Gaussian blur, drop shadow) and fall back otherwise.
- Small static icons are rasterized once per device size into an icon atlas with the same coverage rasterizer as paths, like glyphs, so a list of hundreds of icons stays a handful of draw calls. Large, transformed or animated SVGs draw as vectors.

**Lottie**

- A `Lottie` node with play, pause, seek, speed, direction, loop count, segments and named markers.
- Progress is an animatable property, so a Lottie can follow a spring, be scrubbed by a gesture through the driver graph, or track a route transition's progress (animated tab icons, pull-to-refresh).
- Slots let the app replace colors, text and images at runtime, so animations follow the theme.
- Expressions work as far as ThorVG supports them; they are opt-in per build because they need a small embedded JavaScript engine.
- `.lottie` (dotLottie) archives are unpacked by the asset loader, including multiple animations and their images.
- Off-screen or occluded animations pause automatically. Heavy animations can opt into raster-cache mode: drawn into a texture at a capped frame rate and composited, instead of full vector at display rate.
- HMR reloads a changed `.json` or `.lottie` in place and keeps the playback position.

## Antialiasing and image quality

Everything AXION draws (shapes, paths, SVG, Lottie, text, images, clips, user shaders, effects and 3D) is antialiased by one of a few analytic techniques held to one quality bar, and the software renderer is the reference every GPU renderer is tested against for the content it draws (user shaders, which it skips, are checked against stored GPU goldens).

| Content | Technique | Result |
| --- | --- | --- |
| Rects, rounded rects, ellipses, borders | Signed-distance coverage computed in device space from screen-space derivatives | Smooth at fractional positions, under rotation and scale |
| Shadows | Closed-form blurred rounded rects | No blur pass, no banding |
| Paths: SVG, Lottie, user paths | Exact area coverage with sparse strips: edge pixels get analytic coverage, interior spans are filled solid | 256 coverage levels per pixel instead of 4–8 with MSAA; the same algorithm in the wgpu, GL and CPU renderers, so they match |
| Clips | Rounded-rect clips analytic; path clips as 8-bit coverage masks | No hard stencil edges |
| Images | Mipmaps built with a high-quality downsampling filter, trilinear or bicubic sampling, coverage on quad edges | Rotated images have smooth edges; heavy downscales don't shimmer |
| Text | Coverage glyphs with gamma and contrast correction; SDF or paths when large or transformed | See Text rendering |
| User shaders | Automatic edge coverage on the node's shape, prelude helpers, optional supersampling | Smooth custom SDF art, no sparkling procedural patterns |
| Blur and backdrops | Downsample chain plus separable blur in linear light, premultiplied alpha, pixel-aligned capture regions | No shimmer while content scrolls under glass |
| 3D | MSAA 4x by default (8x where supported), specular antialiasing, alpha-to-coverage for cutouts | TAA off by default, since it ghosts during UI motion |

**User shaders.** The WGSL prelude gives every shader `in.aa` (one device pixel in local units) and helpers such as `ax_aa_fill(d)` (coverage from a signed distance) and `ax_aa_stroke(d, width)` (written aa, aa\_fill and aa\_stroke in AXGL), so hand-written SDF shapes get the same edge as built-in ones. A per-node `shader_quality` of 2x or 4x evaluates the fragment at rotated-grid sample offsets and averages them, for high-frequency procedural shaders; it works on the GL renderer too.

**Motion and pixel snapping.** At rest, hairlines, 1-pixel borders and text snap to the device pixel grid for crispness. While moving, nothing snaps, so motion stays smooth without 1-pixel jitter, and snapping eases back in as the animation settles. Strokes thinner than one device pixel draw at 1 pixel with proportionally lower alpha instead of breaking up.

**Seams between touching shapes.** Coverage antialiasing leaves faint seams where two shapes share an edge, which is common in SVG and Lottie exports. Layers can opt into an exact vector mode that rasterizes with 16 samples per pixel before resolving, removing seams at extra cost.

**Blending and color.** Premultiplied alpha everywhere. UI blends in sRGB space by default to match browsers and design tools; blur, 3D and HDR content work in linear light; text coverage gets its own gamma correction. Layers can opt into linear blending. The sRGB default is built by drawing into a unorm view of the swapchain; linear layers use its sRGB view or an FP16 offscreen. Known limitation: sRGB-space blending makes thin rotated lines bead (roping), as in browsers. Whether the default changes is decided together with Latin text in M1 (issue #4).

**Coverage under affine transforms.** For 2D affine layers, the CPU inverts each instance's transform and passes the pixel's footprint in local space, so coverage is exact and identical across WebGPU implementations. Screen-space derivatives are used only once perspective transforms arrive. Until `ax_render_soft` exists (M1), coverage tests compare against a supersampled CPU reference.

**Verification.** Golden-image tests compare each GPU renderer to the software reference within a perceptual tolerance on antialiasing torture scenes: rotating hairlines, zone plates, thin text in motion, touching Lottie shapes and nested rounded clips. A renderer that drifts fails CI.

## Retained node tree, properties and layout

The scene thread owns one authoritative retained tree; apps never mutate it directly but submit atomic **transactions**, so the screen never shows a half-applied change and app code never blocks on layout.

**Node storage.** Nodes are rows in dense component arrays, ECS style: hierarchy (parent, first child, next sibling), style inputs, layout outputs, transforms, paint properties and dirty flags are hot arrays; text, image, scroll, shader and 3D data are optional side components. Traversals walk contiguous memory, and handles stay stable while storage compacts.

**Transactions**

```c
ax_txn *t = ax_txn_begin(world);
ax_node card  = ax_txn_create(t, AX_NODE_VIEW);
ax_txn_set_str(t, card, AX_PROP_CLASS, AX_STR("card"));
ax_txn_set_f32(t, card, AX_PROP_CORNER_RADIUS, 16.0f);
ax_txn_append_child(t, list, card);
ax_txn_set_transition(t, card, AX_PROP_LAYOUT, &spring_default);  /* optional override */
ax_txn_commit(t);  /* queued; applied atomically at the next frame boundary */
```

The app thread keeps a cheap mirror of handles and last-published layout results, so it can read sizes and positions from the most recent frame without a round trip. Synchronous measurement (rare) is an explicit `ax_measure_sync` call that blocks and is flagged in profiles.

**Property system.** Every property has an id, a value type, an inherited flag, an animatable flag and an *effect class*. The effect class decides how much of the pipeline a change touches:

| Effect class | Examples | Work triggered |
| --- | --- | --- |
| Layout | width, padding, flex, gap, font size, text content | Style, layout, paint record, composite |
| Paint | background, border color, corner radius, shadow | Paint record of that node, composite |
| Composite | transform, opacity, shader uniforms, filter amounts, scroll offset | Composite only |

Animations of composite-class properties are the fast path: they never re-run layout or re-record draw items.

**Dirty propagation.** Style changes mark layout dirty up to the nearest **relayout boundary** (a node whose size does not depend on its children, such as a fixed-size box or a scroll viewport), so editing a list item never relayouts the whole window.

**Layout engine.** Layout follows the constraints-down, sizes-up, parent-positions protocol: one pass over dirty subtrees, with intrinsic-size queries cached per constraint. Built-in protocols:

- Flex (Yoga-compatible semantics, so the CSS flexbox mental model carries over)
- Grid (a practical subset of CSS Grid: tracks with px, %, fr, auto, min/max; named areas)
- Stack and absolute positioning, with anchor positioning for popovers (flip and shift to stay on screen); sticky positioning against the nearest scroll view
- Text (paragraph measurement from the text module)
- Custom layouts written in C, or declaratively by bindings (custom layout code from managed languages would have to run on the scene thread, so it is C-only in v1)

The recommendation is to write this engine in C rather than wrap Yoga or Taffy, because layout animation, relayout boundaries and virtualization all need hooks inside the layout pass.

**Virtualized lists.** `ListView` and `GridView` virtualize their children. The full design (keyed data snapshots, size estimates with scroll anchoring, adaptive overscan, accessibility for unbuilt items, and virtual layouts) is in the Virtualization section.

**Hit testing and focus** run on the scene thread against the latest layout and transforms (3D included), with custom hit shapes and pointer-events flags. Focus scopes, traps and roving focus live in the core so every component gets correct keyboard behavior.

## Animation system

Every animatable property animates by default, and all animation runs on the scene thread against the frame's predicted present time, so motion stays smooth while app code, a GC pause or a Python call is busy.

**Implicit by default.** When a transaction changes an animatable property, the engine looks up its transition from style (per property, per class, or a global default) and animates from the current *presentation* value to the new target. A sensible default spring ships for layout, transform and opacity; colors and paint properties default to a short ease. Apps opt out per property or per transaction (`ax_txn_set_animated(t, false)`), and the OS reduce-motion setting swaps springs for crossfades or instant changes automatically.

**Curves**

- Timing: duration plus easing (cubic Bezier, steps, linear-with-stops).
- Spring: physical (stiffness, damping, mass) or perceptual (duration plus bounce), the same model SwiftUI and Material use.
- Decay: momentum with friction, for flings and scroll.
- Keyframes, sequences and staggers.

**Interruptible and retargetable.** Changing a target mid-flight never jumps: springs keep their current velocity and head to the new target. Timing curves blend into the new animation. This is what makes rapid toggles and drags feel physical.

**Layout animations.** After the layout pass, the animator compares each node's old and new frame and animates the presentation frame between them, FLIP-style but inside the engine, with three built-in presets that are shorthands for the programmable model below:

- `resize`: children relayout every frame at the interpolated size. Exact, costlier.
- `scale`: animate a transform from old to new frame, then snap layout. Cheap, distorts content.
- `crossfade`: snapshot the old content and fade to the new. Best for text that rewraps.

**Full control over layout transitions.** Underneath the presets, a layout transition is a spec the app builds in layers, from a single curve up to fully custom code:

1. **Per channel.** Position, size, corner radius, opacity and content each take their own curve, delay and duration, so a card can move on a snappy spring while it resizes on a softer one.
2. **Content handling.** Children of a resizing node can relayout every frame, counter-scale so text and borders don't distort (scale correction), crossfade, or pin to an edge or anchor point.
3. **Choreography.** Transition groups share one clock across a subtree: stagger children by index or by distance from a point, sequence phases (exits, then the container, then entries), and set the group's interrupt policy.
4. **Paths.** Move along an arc or a keyframed path instead of a straight line.
5. **Expressions.** Bind any presentation property to the driver graph, with the transition's progress, velocity and from/to frames as inputs, still evaluated on the scene thread with no VM.
6. **Native callbacks.** For anything else, a C function (in the app or a plugin) runs on the scene thread each frame and writes the presentation values itself.

```csharp
new Card {
    LayoutTransition = new() {
        Position = Spring.Snappy,
        Size     = Spring.Smooth with { Delay = 0.05 },
        Content  = ContentMode.CounterScale,     // text keeps its size while the card grows
        Children = Stagger.ByIndex(0.03),
        Path     = MotionPath.Arc(0.3),
        Interrupt = InterruptPolicy.Retarget,
    }
};
```

```c
/* Fully custom: runs on the scene thread every frame of the transition. */
static void card_transition(const ax_layout_transition_ctx *ctx, ax_presentation *out)
{
    float t = ax_spring_progress(ctx, &card_spring);
    out->frame         = ax_rect_lerp_arc(ctx->from, ctx->to, t, 0.3f);
    out->corner_radius = ax_lerpf(ctx->from_radius, ctx->to_radius, t);
}
```

**Keeping layout animation under control**

- **What animates.** Per node or per transaction, choose position, size or both, and ignore changes caused by window resizing or scrolling, so a live window resize never makes the whole UI wobble.
- **Coordinate spaces.** A layout-animation root makes its descendants animate relative to it, so a moving parent doesn't make every child animate a second time on top.
- **Interruption.** Retarget keeping velocity (default), restart from the current value, queue after the running transition, or snap to the end.
- **Budget.** A per-frame cost cap downgrades `resize` to `scale` for large subtrees and reports it in the profiler, instead of dropping frames.
- **Visibility.** Started, interrupted and finished events reach the app thread, and devtools show from/to frames, slow motion and scrubbing for every running layout transition.

Inserted nodes run an *enter* transition and removed nodes are kept alive as *exiting ghosts* until their exit transition ends; ghosts can optionally keep their space in layout. Hit testing uses presentation geometry by default, so what you see is what you tap; a per-node override hit-tests against the target geometry instead.

**Gestures on the scene thread.** Recognizers for tap, double tap, long press, pan, swipe, pinch, rotate, and drag-and-drop run where input arrives, without an app-thread round trip. Competing recognizers resolve through a gesture arena (the first to claim wins; others can require another to fail), so a horizontal swipe inside a vertical scroll view behaves correctly. Recognizers then post semantic events to the app thread.

**Driver graph.** To make gesture-driven animation work without running user code per frame, gestures expose live values (translation, velocity, scale, progress) as signals, and properties can be bound to a small dataflow graph that the scene thread evaluates: arithmetic, clamp, interpolate between ranges, rubber-band, spring-follow, mix colors. No language runtime or garbage collector runs per frame (AXGL physics bytecode, under Scrolling, is the one sandboxed exception), so every language can build drivers.

```ts
// TSX: a bottom sheet that follows the finger, rubber-bands past the top,
// and on release springs to the nearest snap point with the finger's velocity.
const drag = usePan({ axis: "y" });
<Sheet
  translateY={drag.y.rubberBand({ min: 0, max: 600 })}
  onPanEnd={(e) => sheet.springTo(snap(e.y, [0, 300, 600]), { velocity: e.velocityY })}
/>
```

The same mechanism scrubs any transition by progress: a route transition, a sheet, a shared-element flight. On release the transition completes or cancels with a spring that inherits the gesture velocity.

**Determinism and tooling.** Animations take time from an injectable clock, so tests can step frames exactly, and devtools can slow down, pause or scrub all running animations.

## Scrolling

All scrolling runs through one engine on the scene thread: every input source (touch, trackpad, mouse wheel, keyboard, scrollbars, code, accessibility actions) becomes a scroll delta or a target, and pluggable physics turns it into motion, so scrolling stays smooth when the app thread is busy and feels the same from every input.

**Browser-like smooth scrolling, including the mouse wheel.** Each input source keeps the behavior people expect from a good browser:

| Input | Default behavior |
| --- | --- |
| Notched mouse wheel | Each notch starts a short ease-out animation (about 150 ms) instead of a jump. Notches that arrive mid-animation add to the target and keep the current velocity, so a fast spin glides instead of stepping. Distance per notch follows the OS setting (lines per notch on Windows, for example). |
| Free-spinning and high-resolution wheels | Fine-grained deltas follow the same smoothing, with less of it as deltas get smaller. |
| Trackpad, Magic Mouse | Pixel deltas applied 1:1 with no added smoothing, since latency matters more here; OS momentum phases are followed where the OS provides them (macOS), and AXION momentum takes over where it doesn't. |
| Touch | 1:1 tracking with touch samples resampled to the frame's present time to avoid judder; release velocity starts a fling. |
| Keyboard | Arrows, Page Up and Down, Space, Home and End scroll with the same animated curve. |
| Scrollbar | Thumb drags track 1:1; track clicks page with an animation. |
| Middle-click autoscroll | On Windows and Linux, as in browsers. |
| Code and accessibility | `ScrollTo` by offset, node or index, with spring, timing or instant motion; focus changes scroll the minimum distance needed, smoothly. |

The engine tells notched wheels from precise devices using the delta mode and device type each platform reports (`WM_MOUSEWHEEL` deltas on Windows, `hasPreciseScrollingDeltas` on macOS, `axis_source` and `axis_value120` on Wayland, XInput2 scroll valuators on X11, `deltaMode` on the web). Reduce motion shortens wheel smoothing to a minimal ease.

**Physics.** Every scroll view has a physics model that decides fling deceleration, overscroll and settling. Each platform gets its native feel by default, and any preset can be used everywhere:

| Preset | Fling | Overscroll | Default on |
| --- | --- | --- | --- |
| `ios` | Exponential decay at 0.998 per ms (0.99 with the fast rate) | Rubber band with resistance `(1 − 1 / (x · 0.55 / d + 1)) · d` for drag distance x and viewport size d, then a critically damped spring back | iOS, iPadOS |
| `android` | Spline fling matching Android's `OverScroller` | Stretch (Android 12 and later) or edge glow | Android |
| `macos` | OS momentum phases | Elastic bounce, following the system setting | macOS |
| `desktop` | AXION decay for touch and trackpads without OS momentum | None | Windows, Linux |
| `clamped` | Decay | None | Opt-in |

On the web the default follows the visitor's OS. The presets are only starting points: physics and overscroll have no fixed menu, and every part of them can be replaced, as described under Custom scroll experiences below.

**Overscroll.** Modes are `none`, `bounce` (the iOS rubber band), `stretch`, `glow` or `custom`, per edge if needed. The overscroll distance on each edge is a live signal in the driver graph, so pull-to-refresh, stretchy parallax headers and Lottie pull indicators run on the scene thread with no app-thread work. `overscroll_behavior` (`auto`, `contain` or `none`, as in CSS) decides whether a scroll that hits its edge passes the rest to its parent.

```csharp
var scroll = new ScrollView {
    Physics        = ScrollPhysics.iOS,          // Platform (default), iOS, Android, MacOS, Desktop, Clamped
    Overscroll     = Overscroll.Bounce,          // None, Bounce, Stretch, Glow, Custom
    WheelSmoothing = WheelSmoothing.Browser,     // Browser (default), None, or a custom curve
    Snap           = ScrollSnap.Proximity(SnapAlign.Start),
    Chaining       = ScrollChaining.Contain,
};

// Stretchy header and pull-to-refresh, evaluated on the scene thread:
header.ScaleY     = scroll.OverscrollDistance.Top.Remap(0, 160, 1.0, 1.25);
refresh.Progress  = scroll.OverscrollDistance.Top.Remap(0, 80, 0, 1).Clamp(0, 1);
```

**Custom scroll experiences.** Nothing about scrolling is limited to the presets. A scroll experience is five replaceable parts, and each can be customized at whatever depth the app needs, from any language:

| Part | Decides | Built-in options |
| --- | --- | --- |
| Input | How wheel, trackpad, touch and keyboard deltas become motion: smoothing curve, distance per notch, acceleration, axis remapping (a vertical wheel scrolling horizontally) | Browser smoothing, 1:1, per-device curves |
| Drag | How content follows the finger inside the bounds | 1:1, damped, stepped |
| Fling | The simulation after release | Decay, spline, spring, keyframed curves |
| Bounds | What happens at each edge: how far content follows past it, and how it returns | Clamp, rubber band, spring, stretch, wrap-around for infinite carousels |
| Settle | Where motion ends | Free, snap points with a custom selection rule, paging |

Four levels of control, each building on the last:

1. **Tune and compose.** Combine built-in parts and set their parameters, per scroll view and per edge.
2. **Expressions.** Replace any part's curve (resistance, deceleration, smoothing) with a driver-graph expression, still evaluated on the scene thread with no VM.
3. **AXGL physics.** Write a part, or a whole physics model, in AXGL with the `physics` kind. It compiles to a small sandboxed bytecode that runs on the scene thread every frame (no allocation, bounded loops, deterministic), so C#, TS, Lua and Python developers write custom physics in the same language they use for shaders, without touching C.
4. **Native.** Implement `ax_scroll_physics` in C or a plugin for complete control, including over raw input events.

```csharp
scroll.Physics = ScrollPhysics.Compose(
    input:  WheelInput.Smooth(Spring.Snappy) with { LinesPerNotch = 4, Acceleration = 1.6f },
    drag:   DragResponse.OneToOne,
    fling:  Fling.Decay(rate: 0.997f),
    bounds: Bounds.PerEdge(top: Edge.RubberBand(0.4f), bottom: Edge.Spring(stiffness: 300), sides: Edge.Clamp),
    settle: Settle.Snap(points, select: (projected, velocity) => projected.Nearest()));

// or a whole model written in AXGL
scroll.Physics = Physics.Load("heavy-bounce.axgl");
```

```axgl
// heavy-bounce.axgl
physics HeavyBounce;

import axion.physics;   // in_bounds, spring_to_edge

@prop friction: f32 = 0.995 range(0.9, 0.9999);
@prop max_stretch: f32 = 0.4 range(0, 1);

// how far content follows a drag past an edge
fn resist(over: f32, viewport: f32) -> f32 {
    let limit = viewport * max_stretch;
    return limit * (1.0 - exp(-over / limit));
}

// one step after release; return false once settled
fn step(inout s: ScrollState, dt: f32) -> bool {
    if (in_bounds(s)) {
        s.velocity *= pow(friction, dt * 1000.0);
        s.offset   += s.velocity * dt;
        return length(s.velocity) > 1.0   // works whether velocity is f32 or vec2f;
    }
    return spring_to_edge(s, dt, 380.0, 30.0);
}
```

```c
typedef struct ax_scroll_physics_vtbl {
    uint32_t struct_size;
    void  (*on_input)(void *self, const ax_scroll_input *in, ax_scroll_state *st);  /* raw deltas in */
    float (*resist)(void *self, float over, float viewport, ax_scroll_edge edge);  /* drag past an edge */
    void  (*release)(void *self, ax_scroll_state *st, ax_vec2 velocity);           /* start a simulation */
    bool  (*step)(void *self, ax_scroll_state *st, double t, double dt);           /* false when settled */
} ax_scroll_physics_vtbl;
```

**Custom overscroll.** Overscroll is just as open:

- **Response:** any resistance curve, per edge, at any of the four levels above.
- **Visuals:** each edge exposes overscroll distance, velocity and phase (dragging, released, settling) as signals and as shader uniforms, so any property, Lottie or layer-effect shader can react: content that stretches through a custom AXGL effect, glows, elastic headers, particle bursts.
- **Hold states:** overscroll can be held at an inset, such as a pull-to-refresh spinner staying visible until app code releases it.
- **Thresholds and release:** actions and haptics fire at chosen distances, and the return to bounds uses any spring or simulation.
- **Chaining:** per edge, choose who takes leftover motion (this view, its parent, or a custom split between them).

**Last resort: app-controlled scrolling.** An app can also take over the scroll position completely and set it each frame from app code. This trades the scene thread's latency guarantees for total freedom, so it is opt-in and flagged in the profiler.

**Snapping and paging.** Snap points work like CSS scroll snap: mandatory or proximity, start, center or end alignment, and full-page paging. On release, the engine projects where the fling would stop, picks the snap target from that projection, and springs to it while keeping the fling's velocity, so snapping never feels like a separate jump.

**Nested scrolling.** Scroll views inside scroll views coordinate through the gesture arena, with axis locking for diagonal drags. For collapsing headers and similar patterns, a nested-scroll connection lets a parent take part of each delta before or after the child, like Android's nested scrolling and Flutter's nested scroll views.

**Scroll-linked effects.** Scroll offset and scroll progress are composite-only properties and driver-graph signals, so sticky headers, parallax, shrinking navigation bars and scroll-driven animations (like CSS scroll timelines) run on the scene thread. `position: sticky` is part of layout and updates without relayout.

**Scroll anchoring.** When content above the viewport changes size, such as images loading or items inserted above, the engine keeps a visible anchor node in place, as browsers do, so the content being read never jumps.

**Performance.** Scrolling is composite-only: the content layer moves, newly exposed areas are recorded, and static content stays cached as layer textures. Scroll events reach the app thread coalesced to one per frame, flagged as user or programmatic.

**Scrollbars** are headless like every component: AXION provides behavior (overlay or classic per platform, auto-hide, hover expansion, thumb sizing that stays stable with virtualized content) and the app styles them.

**Testing.** With the headless host's manual clock, every fling, bounce and snap is deterministic, and the testing API and the MCP `scroll` tool can scroll by distance, to an element, or with a given fling velocity.

## Virtualization

Virtualized lists and grids build only the items near the viewport, from data snapshots the scene thread reads directly, so they stay smooth with unknown item sizes, millions of items and a busy app thread, and stay navigable by screen readers and keyboards past what is built.

**Problems this design solves**

| Gap | Symptom | Fix |
| --- | --- | --- |
| Item data lives on the app thread | Fast flings outrun app code even with templates, because templates still need the data | Immutable, keyed data snapshots the scene thread reads directly |
| Unknown item sizes | The scrollbar jumps, and content shifts when size estimates are corrected | Size estimates, a measured-size cache and scroll anchoring |
| Linear offset lookups | Jumping to item 500,000 or dragging the scrollbar stalls | A balanced tree of item sizes with subtree sums: lookups, size corrections, inserts and removals in O(log n) |
| Fixed overscan | Blank rows during fast flings, wasted work when idle | Velocity-aware overscan biased toward the scroll direction, with a per-frame build budget |
| Recycled nodes carry state | Focus, selection, running animations or text selection appear on the wrong item | Identity by item key; state lives in the data or resets on rebind |
| Focused items get recycled | Keyboard focus and screen readers lose their place | Pinned, kept-alive items |
| Screen readers see only built rows | Wrong item counts, no way to navigate past the viewport | Virtual accessibility nodes with position and set size |
| Plain lists and grids only | Chats, tables and masonry each need their own workaround | Pluggable virtual layouts, including inverted, sectioned and 2D |

**Data model.** Lists bind to a data source of immutable, keyed snapshots. The app commits a new snapshot, or a keyed diff (insert, remove, move, update), inside a transaction, and the scene thread reads it without asking the app. Keyed diffs let inserts, removals and moves animate through the layout-animation system, for visible items only. Huge or remote collections use a paged source: unloaded ranges have a known count and show placeholders while the app thread loads pages around the viewport.

**Building items.** Most rows use item templates: a node subtree with bindings through the driver graph, instantiated on the scene thread from the snapshot. Rows that need arbitrary code use a builder callback on the app thread and show the template's skeleton until it returns. Nodes are pooled per item type with a cap, and rebinding resets any node state that isn't keyed to the item.

**Sizes and positions.** Each item starts with an estimated size (a per-type running average, or an `EstimatedSize` the app provides), replaced by its measured size once built and kept by key in a size cache that survives scrolling away. Offsets live in a balanced tree that stores subtree size sums (a Fenwick tree can't take inserts or removals in the middle without an O(n) rebuild), so offset-to-index and index-to-offset lookups, every size correction and every keyed insert or removal cost O(log n), even for millions of items. When a correction lands above the viewport, scroll anchoring shifts the offset so nothing visible moves, and the scrollbar thumb eases to its corrected size instead of jumping.

**Overscan and budgets.** Overscan grows with scroll velocity and leans toward the direction of motion. Item creation has a per-frame time budget: if a fling outruns it, a row shows a placeholder for a frame rather than the whole frame dropping. Workers pre-shape text and pre-decode images for items just beyond the overscan, so building them is cheap when they arrive.

**Focus, selection and accessibility.** Items that are focused, being dragged or holding a text selection stay alive while off screen. Selection lives in the data source, not in nodes. The accessibility tree exposes the whole collection, with item count and each item's position (like ARIA set size and position), and when a screen reader or keyboard navigation moves to an unbuilt item, the list scrolls it into view and builds it first. Find-in-list searches the data, not the built nodes.

**Virtual layouts.** Virtualization is a layout protocol: a virtual layout estimates sizes, returns the item range for a viewport and places items. Built in:

- Lists, vertical or horizontal.
- Grids with fixed or adaptive columns and item spans.
- Sectioned lists with sticky headers.
- Masonry.
- Inverted lists for chat: anchored to the newest item, loading older content above without a jump.
- 2D tables with rows and columns both virtualized, plus frozen rows and columns.
- Pagers and carousels with snapping.

Custom virtual layouts can be written in C or shipped in plugins.

```csharp
new ListView<Message> {
    Source        = chat.Messages,                // keyed, immutable snapshots
    Key           = m => m.Id,
    Layout        = VirtualLayout.Inverted(),     // chat: anchored to the newest message
    EstimatedSize = 72,
    Template      = MessageRow.Template,          // instantiated on the scene thread
    OnNearStart   = () => chat.LoadOlderAsync(),  // paging
};
```

**Integration with the rest of AXION.** Drag-and-drop reordering works on data indices with edge auto-scroll. HMR restores scroll position through the anchor item's key. The testing API's queries and the MCP `find` tool also search the data source and return virtual element refs, which scroll into view and build when acted on. Devtools show built versus virtual items and each frame's build cost.

## Navigation, routing and hero transitions

Navigation is a framework module built on two core primitives, **transitions driven by a progress value** and **shared-element groups**, so route changes, hero flights and in-place view transitions all use one mechanism and all can be scrubbed by a gesture.

**Router model**

- A `Navigator` node owns a stack of routes; navigators nest (tabs each with their own stack, a modal stack above everything, split views on tablets and desktop).
- Routes are path patterns with typed params (`/photos/:id`), resolved to a factory supplied by the binding. The same table serves deep links on mobile and URL sync through the History API on the web.
- System back is integrated: Android's predictive back gesture scrubs the pop transition before commit, the iOS-style edge swipe does the same, and the browser back button pops the stack.
- Route lifecycle events (will appear, did appear, will disappear), keep-alive policies for tabs, and state restoration after process death on mobile.

**Transitions.** A transition spec receives the outgoing and incoming route subtrees and a progress value from 0 to 1, and maps progress to their properties (slide, fade, scale, parallax, shader-based reveals). Progress comes from a spring, a timing curve or a gesture through the driver graph, so every route transition is interactive for free.

**Hero (shared element) flights**

1. Nodes carry a `hero` tag. When a navigation starts, the incoming route is built and laid out off screen for one frame so its hero frames are known.
2. The navigator matches tags between outgoing and incoming trees and hides both originals.
3. Each pair gets a flight in the navigator's transition overlay layer: frame, corner radius, transform, opacity and shader uniforms interpolate from source to destination, while content crossfades or uses a custom flight builder.
4. On completion the destination node is revealed; on cancel (a gesture released early) the flight runs backwards to the source.

Hero matching is not limited to navigation: a **matched geometry group** lets two nodes in the same view hand off position and size when one replaces the other (expanding a card into a detail panel), like SwiftUI's matched geometry.

**View transitions.** `ax_view_transition(txn, spec)` captures the current state, applies a transaction, and animates between the two, in the spirit of the web's View Transitions API: useful for theme switches, filter changes and reorderings that are not route changes.

```tsx
<Navigator>
  <Route path="/" component={Gallery} />
  <Route path="/photos/:id" component={Photo} transition={zoomFromHero} />
</Navigator>

// In Gallery:  <Image hero={`photo-${p.id}`} src={p.thumb} />
// In Photo:    <Image hero={`photo-${id}`}   src={photo.full} />
```

Heroes can fly between two OS windows, for example when tearing a tab into a new window or dragging a card to another one. For the flight, AXION opens a transient borderless, transparent, click-through overlay window covering both windows' screen area, renders the flight there, and hides the originals until it lands; drags between windows use the same overlay for their preview. On Wayland, where apps cannot position windows globally, the flight degrades to a matched exit and enter animation in each window.

## Deep links and URL schemes

Apps declare custom URL schemes (such as `axion://test` or `myapp://photos/42`) and verified https links in their `axion.toml`; the export tool registers them on every platform, and incoming links resolve straight into the router, so a link opens the right screen with the same transitions as in-app navigation. The axion scheme in these examples stands in for the app's own: each app registers a unique scheme, because two apps claiming the same one conflict on every platform.

```toml
[links]
schemes         = ["axion"]            # axion://test, axion://photos/42
domains         = ["example.com"]      # https://example.com/photos/42 opens the app
single_instance = true                 # desktop: forward links to the running instance

[links.routes]
"axion://test"                   = "/test"
"axion://photos/:id"             = "/photos/:id"
"https://example.com/photos/:id" = "/photos/:id"
"axion://checkout/:cart"         = "/checkout/:cart"
```

**Routing.** A link that matches a pattern becomes a route with typed params. On a cold start the link is resolved before the first frame, so the app never flashes its home screen first; on a warm start it navigates the running app. Apps that need control (sign-in first, a confirmation, an analytics hook) register a handler that can resolve, redirect, defer until ready, or reject the link.

```csharp
app.Links.Handle("axion://checkout/:cart", async (link, nav) =>
{
    if (!session.IsSignedIn) return LinkResult.DeferUntil(session.SignedIn);
    await nav.PushAsync("/checkout", new { cart = link.Params["cart"] });
    return LinkResult.Handled;
});
```

**Single instance.** On Windows and Linux, opening a link starts a second process (macOS hands it to the running app as an Apple Event); with `single_instance`, that process forwards the URL to the running instance (a named pipe on Windows, D-Bus on Linux) and exits, and the running window comes to the front.

**Security.** Incoming links are untrusted input. Only declared patterns are accepted, params are validated against their types, and a link never triggers a destructive action without in-app confirmation. Links also reach the app thread as ordinary events, so they show up in logs and replay traces.

**Opening links.** `ax_open_url` opens URLs in other apps or the browser through each platform's launcher (`ShellExecute`, `NSWorkspace`, `xdg-open`, Android intents, `UIApplication.open`, `window.open`).

**Tooling.** `axion link axion://test` fires a link at a running dev build on any target, the headless host accepts links in its launch options, and the MCP server has an `open_link` tool. `axion export` also generates the `apple-app-site-association` and `assetlinks.json` files to host for verified https links.

| Platform | Custom scheme registered through | Verified https links | Link delivered as |
| --- | --- | --- | --- |
| Windows | Registry keys at install, or the MSIX protocol extension | App URI handlers (MSIX) | Command-line argument, forwarded to the running instance |
| macOS | `CFBundleURLTypes` in Info.plist | Universal Links | Apple Event |
| Linux | `.desktop` file with `x-scheme-handler/<scheme>` | None standard | Command-line argument, forwarded over D-Bus |
| Android | Intent filter in the manifest | App Links (`assetlinks.json`) | Launch intent or `onNewIntent` |
| iOS | `CFBundleURLTypes` in Info.plist | Universal Links (`apple-app-site-association`) | Scene delegate URL callbacks |
| Web | `registerProtocolHandler` (only `web+` schemes) | The app's own URLs | The page URL and the History API |

## Headless components and styling

Components ship behavior, state, keyboard handling, gestures and accessibility, and expose named parts and state flags; every pixel of their look comes from the app's styles.

**Anatomy of a component**

- **Parts**: named slots the component creates or accepts, such as a slider's `track`, `range` and `thumb`.
- **State flags**: `hovered`, `pressed`, `focused`, `focus-visible`, `disabled`, `selected`, `checked`, `open`, `dragging`, `invalid`, plus component-specific ones. Flags are bits on the node, so style matching is a bitmask test.
- **Behavior**: a small state machine plus gesture recognizers and key bindings (arrow keys on a slider, typeahead in a list, Escape to close).
- **Accessibility**: role, name, value, and actions wired to the platform bridge.

An optional debug skin draws outlines so components are usable before any styling exists; it is never the default.

**Initial component set**: Pressable and Button; Toggle, Checkbox and Switch; RadioGroup; Slider and RangeSlider; TextField and TextArea; ScrollView; ListView and GridView (virtualized); Popover, Tooltip, Menu and ContextMenu (anchor positioning, separate OS windows on desktop); Dialog and Sheet (focus trap, scrim); Tabs; Select and Combobox; Splitter; drag-and-drop sources and targets.

**Styling model.** Rules match on node type, class, part and state flags, with a deliberately small selector set (no general descendant combinators), compiled into bitset matches so restyling thousands of nodes per frame stays cheap. Values can reference **design tokens** (variables) that themes swap at runtime; because token changes flow through the normal property path, theme switches animate with the default transitions.

```css
/* AXION stylesheet: a CSS-like subset parsed by the core */
:root { --accent: #4f7cff; --radius: 12px; }

Slider::track          { height: 4px; background: #2a2d36; corner-radius: 2px; }
Slider::range          { background: var(--accent); }
Slider::thumb          { size: 20px; corner-radius: 10px; background: white;
                         transition: scale spring(0.35s, 0.2); }
Slider::thumb:pressed  { scale: 1.2; }
Slider:focus-visible::thumb { outline: 2px solid var(--accent); }
```

The same rules can be built in code from any binding, and because classes are just strings, utility-class styling in the Tailwind style works in TSX without special support. Styles can also attach shaders (`background: shader("ring.wgsl")`) and set their uniforms.

## Language bindings

All four bindings are generated from the IDL and run user code only on the app thread; they differ in their runtime and their declarative layer, never in what the core can do.

**Shared rules for every binding**

- The language VM lives on the app thread. Scene and render threads never call into it; callbacks (events, gesture results, route factories) are queued and delivered there.
- Objects are handle wrappers. Finalizers or GC hooks enqueue a release; the core frees on the scene thread at the next frame.
- Every declarative layer compiles down to the same transactions, so a component written in Lua and one in TSX are indistinguishable to the core.
- Hot module replacement in dev builds (full design in its own section): the binding re-runs the changed module, and the reconciler applies the difference as one transaction, keeping app state where possible.

| Language | Runtime | Interop | Declarative layer | Platforms in v1 |
| --- | --- | --- | --- | --- |
| TypeScript + TSX | Node.js or Bun on desktop; QuickJS-ng embedded on Android and iOS; the browser's own JS on the web | A generated Node-API addon (runs in Node and Bun) and QuickJS glue; direct calls into the WASM core on the web | Custom JSX runtime with fine-grained signals (Solid-style): a signal updates one property, no virtual DOM diff | All six |
| C# | .NET 10 (LTS); NativeAOT on desktop and iOS, the .NET for Android runtime on Android | `[LibraryImport]` source-generated P/Invoke; handles as `readonly struct` | Source-generated component classes with a fluent builder; optional markup later | Desktop, Android, iOS; web later |
| Lua | Lua 5.4 by default; LuaJIT optional on desktop and Android (not on iOS, which forbids JIT) | Userdata plus metatables over handles | Table-based trees with reactive bindings | All six |
| Python | CPython 3.12+ embedded, or AXION as an extension module | C extension on the stable ABI (abi3), so one wheel per platform covers many Python versions | Context-manager builder (`with Column(): Text(...)`) plus signals | Desktop first; mobile and Pyodide on the web later |

**C# goes first.** The first vertical slice (M2) ships the C# binding, so its design is settled ahead of the others:

- **Process model.** A standalone app's `Main` calls `Axion.Run(app)`. AXION keeps the OS main thread for the event pump and starts the app thread, where all user code runs from then on. In hosted mode, the native host loads the C# app as a NativeAOT shared library instead.
- **async/await.** The app thread installs its own `SynchronizationContext`, so `await` continuations resume on the app thread and app code never needs locks or dispatch calls.
- **Interop.** `[LibraryImport]` source-generated P/Invoke over blittable structs and `readonly struct` handles; callbacks are `[UnmanagedCallersOnly]` function pointers, so events cost no delegate marshaling or pinning.
- **Lifetime.** Handle wrappers are value types. Objects that own native resources implement `IDisposable`, finalizers only enqueue a release, and unmounting a component disposes its subtree deterministically.
- **Reactivity.** A source generator turns `[Signal]` fields into observable properties; any binding that reads one becomes a fine-grained property update, the same model as TSX signals.
- **Tooling.** .NET Hot Reload (`MetadataUpdateHandler`) rebuilds changed components into a single transaction. The IDL code generator targets C# first.

```csharp
public partial class Counter : Component
{
    [Signal] private int _count;   // generates the observable Count property

    protected override Node Build() =>
        new Pressable { Class = "btn", OnPress = () => Count++ }
        {
            new Text(() => $"Tapped {Count} times"),   // only this text property updates
        };
}
```

```tsx
// TSX with signals: only the label's text property changes when count changes.
function Counter() {
  const [count, setCount] = createSignal(0);
  return (
    <Pressable class="btn" onPress={() => setCount(count() + 1)}>
      <Text>{`Tapped ${count()} times`}</Text>
    </Pressable>
  );
}
```

**JavaScript runtimes.** On desktop, a TS app runs in Node.js or Bun: AXION ships as a Node-API addon (`@axion/core`), so apps get the npm ecosystem, a JIT and the standard debuggers, and ship as one executable through Node's single executable applications or `bun build --compile`. With Node or Bun the JS thread is the process main thread, so AXION runs the OS event pump on it and integrates the JS event loop the way Electron does; the scene and render threads stay independent, so running animations keep their frame rate. The main thread still forwards input and answers OS hit-test queries, though, so a long JS task delays touches, scrolling and window drags; running app code in a Node worker thread would keep the main thread free, as the thread roles table assumes. Node and Bun can't ship on phones (iOS forbids JIT, and both are large), so Android and iOS embed QuickJS-ng with the same TSX runtime, and Node-only modules such as `fs` are replaced by portable AXION plugins. Node is the reference runtime; Bun is verified in CI because its Node-API and event-loop compatibility can differ. Hermes remains an option for mobile if QuickJS-ng falls short, and a React reconciler adapter can come later for teams that want React semantics, built on the same signals runtime.

## Hot module replacement

HMR is a first-class subsystem: `axion dev` runs a dev server that watches sources and pushes each change to every running app (desktop, phone, browser), which applies it as one transaction, keeping app state and never restarting the process.

**How a change flows**

1. The dev server watches code, styles, shaders and assets, and rebuilds only what changed: an esbuild module for TS, a .NET Hot Reload delta for C#, a validated shader, a processed texture or font.
2. It sends the update over a local WebSocket (forwarded over USB for phones) to the HMR client inside the app.
3. The client, on the app thread, routes the update to its owner: the binding for code, the style engine for stylesheets, the render thread for shaders, the asset cache for assets.
4. Affected components rebuild, the reconciler diffs them against the live tree, and one transaction commits, so the next frame shows the new version with no flash. Implicit animations are off for code-reload transactions by default.
5. If anything fails (a compile error, an exception in the new code), the app keeps running the last good version and an error overlay shows the message and source location. The overlay is its own top-level layer, outside the app's tree, and disappears once the file is fixed.

**State preservation.** A component's identity is its type, its position in the parent and its key. When identity survives a reload, the component keeps its signal state, transferred by field name and type; if a component's state shape changed incompatibly, only that component resets. Router stacks, scroll positions, focus, and text-field contents and selection always survive, so you stay on the screen you are editing.

**Per-language mechanics**

| Language | How new code is applied | Limits and fallbacks |
| --- | --- | --- |
| C# | .NET Hot Reload deltas applied in-process; AXION's `MetadataUpdateHandler` rebuilds the affected components | Dev builds run on the JIT runtime (Mono interpreter on iOS), since NativeAOT cannot hot reload. Edits the runtime rejects restart the app thread with a state snapshot instead of the process |
| TS/TSX | Changed modules and their importers re-evaluated in the running JS runtime (Node, Bun, QuickJS-ng or the browser), components re-registered by identity | Module-level state kept through an `import.meta.hot`-style API |
| Lua | Changed chunks re-run; functions in component tables swapped in place | State tables marked with `hmr.keep` are preserved |
| Python | `importlib.reload` of changed modules; live components switched to the new classes | Module globals reset unless registered as kept state |

**Styles, shaders and assets**

- Stylesheets are re-parsed and diffed, and only matching nodes restyle. Style updates animate through their transitions, which makes tuning a spring or an easing curve interactive.
- Shaders recompile off the render thread and the pipeline swaps in when ready, keeping uniform values; errors appear in the overlay with the original AXGL or WGSL line numbers.
- Images, fonts and glTF models reload by asset path while their handles stay valid; text re-measures when a font changes.
- Native C plugins (custom layouts, renderers) can be reloaded between frames in desktop dev builds (mobile and the web link plugins statically, so a native change there needs a rebuild) when their ABI version is unchanged; changes to the AXION core itself still need a restart.

**Devices.** `axion dev --target android|ios|web` installs a dev build and connects it; several devices can be connected at once and all receive each update. Embedded hosts enable the HMR client explicitly in their host configuration.

**Release builds.** The HMR client is compiled out (`AX_ENABLE_HMR=0`), so no socket and no code-loading path ship in production.

## Plugins

An AXION plugin is one folder holding a portable C core, native code per platform, and binding glue per language, in the spirit of Flutter plugins. The `axion` CLI builds the native parts into the app for each target and generates bindings from the plugin's IDL, so one plugin works from C#, TS, Lua and Python alike.

**Folder layout**

```text
axion_camera/
  axion-plugin.toml        manifest: name, version, AXION ABI range, platforms, permissions
  api/
    camera.axidl           public API: the single source for the C header and every binding
  src/                     portable C11 shared by all platforms
  include/axion_camera/    generated public header, plus hand-written extras
  platforms/
    windows/               C or C++ (Media Foundation), CMake fragment
    macos/                 Objective-C or Swift (AVFoundation)
    ios/                   Objective-C or Swift, required Info.plist keys
    linux/                 C (PipeWire, V4L2)
    android/               C over JNI plus Kotlin or Java sources, Gradle fragment
    web/                   JS glue for Emscripten (getUserMedia)
  bindings/
    csharp/                idiomatic wrappers over the generated P/Invoke layer
    ts/                    typings and JSX components
    lua/
    python/
  assets/                  shaders, fonts and images the plugin ships
  examples/
  tests/
```

**Manifest**

```toml
[plugin]
name    = "axion_camera"
version = "1.2.0"
axion   = ">=1.0, <2.0"          # plugin ABI range

[api]
idl = "api/camera.axidl"

[platforms]
windows = { sources = "platforms/windows", link = ["mfplat", "mf"] }
macos   = { sources = "platforms/macos", frameworks = ["AVFoundation"] }
ios     = { sources = "platforms/ios", frameworks = ["AVFoundation"], requires_plist = ["NSCameraUsageDescription"] }
android = { sources = "platforms/android", gradle = "build.gradle", permissions = ["android.permission.CAMERA"] }
linux   = { sources = "platforms/linux", pkg_config = ["libpipewire-0.3"] }
web     = { sources = "platforms/web" }

[unsupported]
policy = "stub"                  # "stub": calls return AX_ERR_UNSUPPORTED; "error": fail the build
```

The export tool merges plugin permissions and settings into each platform project (AndroidManifest, Info.plist, entitlements, web permissions policy). Keys whose wording belongs to the app, such as the iOS camera usage text, are declared as required and filled in from the app's own manifest.

**Direct calls, not message channels.** Unlike Flutter's method channels, plugins expose plain C functions, so calls cost nothing and work from every binding. Each function is tagged in the IDL with the threads it may run on, like the core API. Work the OS finishes asynchronously (a permission dialog, a captured photo) comes back as an event on the app thread; high-rate data such as camera frames goes straight to the render thread as an external texture.

**Extension points.** A plugin's init function registers what it adds:

```c
AX_PLUGIN_EXPORT ax_result ax_plugin_init(ax_plugin_registry *reg, const ax_plugin_info *info)
{
    if (!ax_abi_compatible(info->abi_version, AX_PLUGIN_ABI)) return AX_ERR_ABI_MISMATCH;
    ax_register_node_type(reg, &camera_preview_node);          /* node type, properties, layout */
    ax_register_external_texture_source(reg, &camera_frames);  /* zero-copy frames for the renderer */
    ax_register_service(reg, AX_STR("camera"), &camera_api);
    return AX_OK;
}
```

| Extension point | Typical plugins |
| --- | --- |
| Services (plain C APIs) | Camera, sensors, notifications, file pickers, secure storage |
| Node types with their own properties and layout | Charts, map views, video players |
| External textures (D3D shared handles, IOSurface, AHardwareBuffer, WebCodecs frames) | Video, camera preview, other GPU producers |
| Platform views (native widgets embedded in the tree) | Web views, native ads, OS maps |
| Layout protocols and gesture recognizers | Radial or flow layouts, custom gestures |
| Render IR items with a handler per renderer, shader libraries, whole renderers | Particle systems, effects packs, alternative renderers |
| Asset loaders | Image formats, font formats, 3D formats |
| Devtools panels and HMR hooks | Inspectors, state viewers |

**Platform views** are the hard case, because AXION draws everything on the GPU while a native web view or map draws itself. Two strategies are offered per platform: render the native view into a texture that composites like any layer (correct clipping, transforms and z-order, extra cost), or place the native view in a window layer above or below AXION's surface, positioned by layout every frame (cheap, but it cannot be clipped or overlapped by AXION content).

**Linking.** Plugins link statically into release builds, which iOS and the web require anyway. Desktop dev builds load them as shared libraries so native plugin code can hot reload, as described in the HMR section.

**Distribution.** An app lists plugins in its `axion.toml` from a registry, a git URL or a local path, with a lockfile. `axion plugin new` scaffolds the folder above and `axion plugin add` installs one. The CLI turns each plugin's bindings into normal local packages for the app's language (a NuGet package for C#, an npm-style module for TS), so IDEs and tooling see ordinary dependencies. Platform implementations can be federated: a separate package that declares `implements = "axion_camera"` can add or replace one platform's code without forking the plugin.

**First-party plugins.** The platform services module (tray, menu bar, global shortcuts), Scene3D, the GL renderer, and video playback ship as plugins built on this same API, which keeps the API honest from day one.

## Headless host, automation and MCP

AXION is built to be driven by three kinds of users, developers, automated tools and AI agents, through one stack: a headless host that runs any app without a window or GPU, an automation protocol that any running app can expose, and an MCP server that puts both in front of AI agents.

**Headless host (`ax_headless`)**

- No OS window and no platform dependency: virtual surfaces of any size, scale factor, theme, locale, text direction and font scale, plus safe-area insets and reduce-motion settings that mimic a chosen device profile.
- Renders with the software renderer (no GPU, ideal for CI) or the WebGPU renderer offscreen when a GPU is available.
- Deterministic by default: a manual clock, the cooperative single-thread scheduler and a bundled font set, so the same inputs always give the same pixels. Real threads and system fonts are opt-in.
- Synthetic input with exact timing: pointer, multi-touch, pen, keyboard, IME composition, scroll with momentum, and gestures such as swipes and pinches.
- Platform services are faked through plugin test doubles (clipboard, file pickers, permissions, notifications), so platform-dependent screens are testable.
- Many independent instances can run in one process, so test suites parallelize.

**Testing API.** Available from C, C#, TS, Lua and Python, so tests run in each language's usual framework (xUnit, Vitest, pytest).

- Queries find elements the way users and assistive tech see them: by role and accessible name, by text, or by test id, over the accessibility tree.
- Reads: layout rectangles, property values, computed styles, the current route.
- Time control: step frames, advance time, run animations to completion, or `Settle()` until no animations or transactions are pending.
- Snapshots: PNG images against goldens, the semantic tree as text for diffs, Render IR snapshots, and record-and-replay traces of input and transactions for reproducing bugs.
- Performance: frame timings and dropped-frame reports when running on a GPU.

```csharp
[Fact]
public async Task Counter_increments()
{
    await using var app = await HeadlessApp.StartAsync<MyApp>(new() { Size = (390, 844), Scale = 3, Theme = "dark" });

    await app.GetByRole(Role.Button, name: "Tapped 0 times").TapAsync();
    await app.SettleAsync();   // run frames until animations and transactions finish

    Assert.NotNull(app.GetByText("Tapped 1 times"));
    await app.Screenshot().ShouldMatchGoldenAsync("counter-after-tap.png");
}
```

**Automation protocol.** JSON-RPC over a local WebSocket, a named pipe or stdio, served by the headless host and, in dev builds, by any running app on any target, phones and browsers included. It offers the same operations as the testing API (inspect, query, act, screenshot, control time) plus logs, errors, performance traces and route navigation. AXION's own devtools use this protocol, so it is always complete.

**MCP server (`axion mcp`).** A Model Context Protocol server, over stdio or streamable HTTP, that lets AI agents launch, inspect and drive AXION apps, and look up the AXION API while writing code.

| MCP tool | What it does |
| --- | --- |
| `launch` | Start the project headless or on a target, with size, theme, locale and device profile |
| `sessions`, `close` | List and stop running apps |
| `get_tree` | Compact semantic tree, filtered by subtree, depth or interactivity to keep token use low |
| `find` | Elements by role, name, text or test id, returned as stable element refs |
| `screenshot` | The whole surface or one element, as an image |
| `tap`, `type_text`, `press_key`, `scroll`, `drag`, `gesture` | Act like a user; each returns a short diff of what changed (tree, route, new errors) |
| `navigate` | Go to a route directly |
| `wait_for`, `settle` | Wait for a condition, or until the UI is idle |
| `inspect` | Layout, properties and computed styles of an element |
| `advance_time` | Step the clock, pause or slow down animations |
| `logs`, `errors`, `perf` | Console output, HMR and runtime errors, frame timings and jank sources |
| `run_tests` | Run the project's tests and return failures with snapshots |

The server also exposes MCP resources: the API reference generated from the IDL for the installed AXION version, the component catalog with each component's parts and state flags, the app's route table, and its style tokens. Agents therefore write code against accurate signatures, not memory.

```json
{ "mcpServers": { "axion": { "command": "axion", "args": ["mcp", "--project", "."] } } }
```

**Designed for agents**

- **Stable element refs.** Elements are addressed by test id or by identity-derived keys, not coordinates, and refs survive re-renders as long as the node's identity does.
- **Token-aware output.** The tree comes as compact indented text with only visible and interactive nodes by default; bounding boxes and properties are opt-in.
- **Realistic or semantic actions.** Actions default to synthetic pointer and key input at the element, exercising gestures and hit testing like a real user; accessibility actions are available for speed.
- **Reproducibility.** Headless sessions with a fixed clock let an agent replay exactly what it saw, and any session can be exported as a replay trace for a human to debug.

**Security.** Release builds compile out the automation endpoint unless the app opts in. When enabled, it listens only locally, requires a per-session token, and grants read, act and eval permissions separately. The MCP server only drives apps it launched or apps that hand it a token, and it exposes nothing beyond the app (no shell, no file system).

## Build and export targets

CMake is the one canonical build for the C core and native dependencies, wrapped by an `axion` command-line tool that creates, runs, hot-reloads and exports apps for each target without users touching CMake, Gradle or Xcode.

| Target | Windowing backend | GPU via WebGPU | Toolchain | Package |
| --- | --- | --- | --- | --- |
| Windows | Win32 | D3D12 (Vulkan optional) | MSVC or clang-cl | `.exe`, MSIX |
| macOS | AppKit | Metal | Clang / Xcode | `.app`, signed and notarized |
| Linux | Wayland, X11 fallback | Vulkan (GL fallback) | GCC or Clang | AppImage, Flatpak |
| Android | GameActivity or SurfaceView | Vulkan (GLES fallback) | NDK Clang via Gradle | AAB, APK |
| iOS | UIKit + CAMetalLayer | Metal | Clang / Xcode | `.ipa` |
| Web | `<canvas>` + DOM events | Browser WebGPU, WebGL2 fallback | Emscripten | Static bundle (`.wasm` + `.js`) |

**Dependencies.** HarfBuzz, FreeType, SheenBidi, libunibreak, ThorVG, cgltf, image decoders (libpng, libjpeg-turbo, libwebp, or stb\_image for small builds), QuickJS-ng and Lua build from source through CMake. Dawn is consumed as the official prebuilt binaries from the Dawn project's releases, pinned by version and SHA-256, because building it from source is slow and needs its own toolchain; wgpu-native's official binaries are pinned the same way for the CI job that keeps it compiling. On Linux the platform backend links libwayland-client and libwayland-cursor, and vendors the wayland-protocols XML it generates code from.

**Export pipeline** (`axion export <target>`)

- Bundles the app's scripts (esbuild for TS/TSX), compiles C# with NativeAOT, or embeds Lua/Python sources.
- Subsets fonts to the glyphs the app can reach, plus the fallback set.
- Compresses textures to KTX2/Basis, compiles AXGL with axglc, validates every WGSL shader, and precompiles pipeline warm-up lists.
- Generates the platform project (Gradle, Xcode, MSIX manifest, web `index.html` with the headers threads need) and signs where credentials are configured.

**Web specifics.** Multi-threading in the browser needs `SharedArrayBuffer`, which requires the page to be served cross-origin isolated (COOP and COEP headers). The export tool emits those headers for its dev server and documents them for hosting; without them AXION runs the cooperative single-thread scheduler from the threading section.

**Licensing.** AXION is licensed under Apache 2.0, and every dependency in this design uses a permissive license compatible with it: Dawn (BSD 3-Clause), HarfBuzz (MIT-style), FreeType (used under its BSD-style FreeType License), SheenBidi (Apache 2.0), libunibreak (zlib), ThorVG (MIT), AccessKit (MIT or Apache 2.0), cgltf (MIT), QuickJS-ng (MIT), Lua (MIT), LuaJIT (MIT), CPython (PSF License), libpng (libpng License), libjpeg-turbo (IJG and BSD 3-Clause), libwebp (BSD 3-Clause), stb\_image (MIT or public domain), Basis Universal (Apache 2.0), JerryScript for Lottie expressions (Apache 2.0) and the Noto fonts (SIL Open Font License). `axion export` generates the third-party notices file each app ships, including the credit the FreeType License requires. New dependencies must come from the same permissive allow-list: no GPL or LGPL code in shipped binaries.

## Repository layout, milestones and decisions

The repository mirrors the layer stack one directory per module, and the milestones build the thinnest vertical slice first (one window, one rect, one thread split) before widening.

```text
axion/
  base/          allocators, arenas, containers, handles, atomics, threads, jobs, math
  platform/      win32/ appkit/ wayland/ x11/ android/ uikit/ web/ host/  (windowing, input, IME)
  headless/      headless host: virtual surfaces, deterministic clock, synthetic input, device profiles
  gpu/           webgpu.h wrapper, pipeline cache, render graph, uploads
  render/        ir/ (Render IR contract)  raster/ (sparse-strip path coverage, shared)  wgpu/  soft/  compute/ (later)
  shaders/       AXGL compiler (axglc), standard library, prelude, language server
  text/          itemize, shape cache, line layout, atlases, fallback, emoji
  vector/        ThorVG integration, SVG and Lottie nodes, icon atlas
  core/          node store, transactions, style, layout, animation, gestures, scrolling, virtual layouts, AXGL physics runtime, hit test, a11y tree
  runtime/       scene and render threads, cooperative scheduler, ax_host_frame, presenters (wires core, renderers and platform)
  kit/           headless components, navigator and router, heroes, view transitions
  plugin/        plugin ABI, registry, extension points, platform-view host
  plugins/       first-party plugins: platform_services/ scene3d/ render_gl/ video/
  bindings/      idl/  ts/  cs/  lua/  py/  (plus each language's testing API)
  hmr/           dev server protocol, runtime client, state transfer, error overlay
  automation/    automation protocol, semantic queries, replay traces, MCP server
  tools/         axion CLI, IDL codegen, plugin scaffolding, asset pipeline, devtools
  tests/         unit, golden images and antialiasing torture scenes, replay traces
  examples/
```

**Milestones**

1. **M0, skeleton**: base, one desktop platform backend, `ax_gpu` clearing a window, the Render IR, and rounded rects. Exit: a window resizes smoothly at the display's refresh rate. Done on Nov 7, 2026, with the Wayland backend, measured on KWin 6.8 with an NVIDIA GPU: at the refresh rate when still on 72 Hz and 260 Hz monitors; when resizing, 0–0.3 missed vblanks a second at 72 Hz and a mean of 4.8 at 260 Hz (issue #2).
2. **M1, the thread split**: node store, transactions, the app/scene/render pipeline, flex layout, implicit animations, Latin text (shaping plus atlas), the software renderer for golden tests, the headless host, and the accessibility tree, with AccessKit's iOS and web coverage confirmed. Exit: an app thread blocked for 500 ms does not drop animation frames. Carried over from M0: the job system; resizing at 260 Hz within 1 missed vblank a second (issue #2); the blend-space decision, taken with text (issue #4); and re-measuring the resize handshake's bound with real layout and text. Progress: the node store and transactions (PR #6, decisions 60–71) and the thread split (PR #8, decisions 74–79) are done. Pending before M2: zero frames at rest, so a window presents only while the scene has new content or something animates (decision 78).
3. **M2, interaction**: pointer and keyboard input, gesture arena, the scroll engine (wheel smoothing, physics presets, overscroll) and virtualized lists, the first headless components, the C# binding, HMR for C# and stylesheets, the automation protocol with the C# testing API, and the accessibility bridge on the first platform. Exit: a styled settings screen built entirely in C# and edited live without restarting, and fully usable with a screen reader and keyboard.
4. **M3, depth**: full text (bidi, fallback, emoji, IME, TextField), AXGL with fill and backdrop shaders, AXGL scroll physics, SVG, the full windowing API (custom chrome, hit-test regions, system backdrops, multi-window, and layer surfaces with the Wayland pieces they depend on: `wl_output` tracking to choose an output, and `xdg_popup` for menus from a layer surface through `zwlr_layer_surface_v1.get_popup`), accessibility bridges on all desktop platforms, the plugin system with the first-party platform-services plugin, native drag and drop within and between windows and apps (this milestone builds the transient overlay window that M4's cross-window heroes reuse), and the MCP server. Exit: a chat app with mixed-script text and frosted panels.
5. **M4, motion and reach**: layout animations, navigator with deep links and URL schemes, heroes, view transitions, Lottie playback; Android and iOS ports, each shipping with its accessibility bridge. Exit: the gallery-to-detail hero demo running on phones with an interactive back gesture.
6. **M5, breadth**: 3D PBR, user compute, the web target with the WebGL2 renderer, TS/TSX, Lua and Python bindings with HMR, devtools, and the export tool for all six targets.

Implementation decisions and their measurements are logged in `docs/decisions.md` in the AxonDesigns/axion repository; this document records the architecture they implement.

**Decisions**

- [x] Dependencies: any language is acceptable behind a C API that builds for all six targets.
- [x] Without WebGPU: a WebGL2/GLES 3 renderer, then the software renderer.
- [x] First binding: C#.
- [x] WebGPU implementation: Dawn, measured against wgpu-native at M1 before the choice is locked.
- [x] GL fallback on native Android: both. Dawn's compatibility mode on GLES 3.1+, our own GL renderer on GLES 3.0-only devices.
- [x] C# on Android: the .NET for Android runtime.
- [x] Accessibility: a first-class priority. AccessKit's iOS and web coverage is confirmed at M1, and we build our own bridges where it has none.
- [x] JavaScript runtime: Node.js or Bun on desktop, QuickJS-ng embedded on Android and iOS, the browser on the web.
- [x] Hit testing: presentation geometry by default, with a per-node override to target geometry.
- [x] Cross-window hero flights and drag visuals: supported through a transient overlay window (see Navigation and Drag and drop).
- [x] License: Apache 2.0, with a permissive-only dependency allow-list (see Build and export).
- [x] Name and API prefix: AXION (`ax_`).
- [x] First desktop backend: Wayland on Linux. X11 and the other backends implement the same internal backend vtable.
- [x] Frame pacing: driven by the compositor's frame callbacks with a MAILBOX swapchain, pending validation with Mesa on AMD or Intel hardware.
- [x] Render IR colors: premultiplied float RGBA in linear light, with extended range.
- [x] Renderer interface: a seventh vtbl entry, `wait_frame`.