# AXION

AXION is a UI runtime written in C11 around a stable C ABI. Renderers, platform backends and
language bindings are modules on top of that ABI. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for the design and [docs/decisions.md](docs/decisions.md) for the decisions made while building it.

**Status:** M0 is done: `base/`, the Wayland backend in `platform/`, `gpu/` (Dawn, with
wgpu-native kept compiling in CI), the Render IR and `ax_render_wgpu`, shown by
`examples/m0_window`. M1, the thread split, is in progress: so far the job system in `base/`, and in
`core/` the node store, the property system with dirty propagation, transactions applied atomically
on the scene thread, and the app thread's mirror; `runtime/` with the scene and render threads
(`examples/m1_nodes`); `render/soft/`, the software renderer, deterministic on every platform;
and `headless/`, which runs a world without a window or a GPU. See the milestones in
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

Reference benchmarks build with the tests but never run in CI. Run them on a Release preset, for
example `build/gcc-release/tests/bench/bench_core` or `bench_soft [workers]`;
[docs/decisions.md](docs/decisions.md) records the numbers (decisions 70 and 87).

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
