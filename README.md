# AXION

AXION is a UI runtime written in C11 around a stable C ABI. Renderers, platform backends and
language bindings are modules on top of that ABI. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for the design and [docs/decisions.md](docs/decisions.md) for the decisions made while building it.

**Status:** M0 is done: `base/`, the Wayland backend in `platform/`, `gpu/` (Dawn, with
wgpu-native kept compiling in CI), the Render IR and `ax_render_wgpu`, shown by
`examples/m0_window`. M1, the thread split, is in progress. Done so far:

- `base/`: the job system and deterministic math.
- `core/`: the node store, the property system with dirty propagation, transactions applied
  atomically on the scene thread, and the app thread's mirror. Also the layout engine with flex
  (Yoga-compatible), clips for `overflow: hidden`, and implicit animations: springs, timing curves
  and the `resize` layout preset.
- `runtime/`: the scene and render threads. A window presents nothing at rest, and an app thread
  blocked for 500 ms does not stop running animations (`examples/m1_nodes`).
- `render/soft/`: the software renderer, deterministic on every platform.
- `headless/`: runs a world without a window or a GPU.
- `text/`: Latin text: HarfBuzz shaping with a shared cache, libunibreak's line breaks, greedy
  wrapping that re-wraps without shaping again, glyphs rasterized by FreeType on worker threads,
  the bundled Noto Sans and the system's fonts through fontconfig. Text leaves are measured by
  layout and drawn by both renderers from glyph runs in the Render IR (1.1).

Still to come in M1: the blending space (issue #4) and the accessibility tree. See the milestones in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Building

AXION builds with CMake (3.21 or newer) and Ninja. Each configuration is a preset, and the same
name configures, builds and tests:

```sh
cmake --preset gcc-debug
cmake --build --preset gcc-debug
ctest --preset gcc-debug
```

| Host    | Presets                                                    |
| ------- | ---------------------------------------------------------- |
| Linux   | `gcc-debug`, `gcc-release`, `gcc-asan`, `clang-debug`, `clang-release`, `clang-asan`, `clang-tsan` |
| macOS   | `clang-debug`, `clang-release`, `clang-asan`               |
| Windows | `msvc-debug`, `msvc-release` (run from a shell with `vcvars64` loaded) |
| Web     | `emscripten` (needs `EMSDK` set; builds the libraries only), `emscripten-tests` (runs the tests in Node) |

### Web build

The web presets need an activated emsdk (`EMSDK` set, for example by `source emsdk_env.sh`). CI
uses the Emscripten version in [tools/ci/emsdk-version](tools/ci/emsdk-version), and local builds
should use the same one: versions differ in ways the tests notice (Emscripten 5 passes Node's
environment to programs and 4.0.10 does not; decision 86). `emscripten-tests` runs the tests of
core, the Render IR, the runtime, the software renderer and the headless host in Node:

```sh
./emsdk install $(cat tools/ci/emsdk-version) && ./emsdk activate $(cat tools/ci/emsdk-version)
cmake --preset emscripten-tests && cmake --build --preset emscripten-tests
ctest --preset emscripten-tests
```

`*-asan` presets run with AddressSanitizer and UndefinedBehaviorSanitizer, `clang-tsan` with
ThreadSanitizer. Build options:
`AX_BUILD_TESTS` (on when AXION is the top-level project), `AX_SANITIZE`, and `AX_WEB_THREADS`
(Emscripten only: build with `-pthread`).

## Repository layout

Each top-level directory is one module of the layer stack described in the architecture document.
Modules that have no code yet contain a one-line README. Public headers live in
`<module>/include/axion/`: the stable C ABI sits directly there, and the internal SDK shared
between AXION's own modules sits in `axion/<module>/`. Tests live in `tests/`.

## Golden images

Tests that draw compare their images with PNGs in `tests/goldens/` (decision 82), exactly for the
software renderer. A failure writes the actual, expected and difference images to
`golden-failures/` (or `AX_TEST_FAILURE_DIR`). When a change of the pictures is intended, rewrite
the goldens with `tools/update_goldens.sh [preset]`, review the new PNGs and commit them; CI never
updates them.

## Benchmarks

Reference benchmarks build with the tests but never run in CI. Run them on a Release preset from
`build/<preset>/tests/bench/`; [docs/decisions.md](docs/decisions.md) records the numbers:

| Benchmark              | Measures                                         | Decision |
| ---------------------- | ------------------------------------------------ | -------- |
| `bench_core`           | transactions, the mirror and the job system      | 70       |
| `bench_soft [workers]` | the software renderer                            | 87       |
| `bench_layout`         | full and incremental flex layout of 10 000 nodes | 92       |
| `bench_anim`           | the frame's stages while 10 000 nodes animate    | 94       |
| `bench_text`           | shaping, line layout, a width animation of text  | 99, 101  |

## Repository checks

CI's first job needs no build: every tracked text file ends in a newline
(`tools/ci/check_final_newline.sh`), and each decision's status in the index of
[docs/decisions.md](docs/decisions.md) agrees with its entry (`tools/ci/check_decision_status.py`).
Both run locally from anywhere in the repository.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
