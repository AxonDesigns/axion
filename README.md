# AXION

AXION is a UI runtime written in C11 around a stable C ABI. Renderers, platform backends and
language bindings are modules on top of that ABI. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
for the design and [docs/decisions.md](docs/decisions.md) for the decisions made while building it.

**Status:** early M0. The `base/` module (allocators, arenas, handles and pools, atomics, threads,
errors) is in place; everything above it is still to come.

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
| Linux   | `gcc-debug`, `gcc-release`, `gcc-asan`, `clang-debug`, `clang-release`, `clang-asan` |
| macOS   | `clang-debug`, `clang-release`, `clang-asan`               |
| Windows | `msvc-debug`, `msvc-release` (run from a shell with `vcvars64` loaded) |
| Web     | `emscripten` (needs `EMSDK` set; builds the libraries only) |

`*-asan` presets run with AddressSanitizer and UndefinedBehaviorSanitizer. Build options:
`AX_BUILD_TESTS` (on when AXION is the top-level project), `AX_SANITIZE`, and `AX_WEB_THREADS`
(Emscripten only: build with `-pthread`).

## Repository layout

Each top-level directory is one module of the layer stack described in the architecture document.
Modules that have no code yet contain a one-line README. Public headers live in
`<module>/include/axion/`: the stable C ABI sits directly there, and the internal SDK shared
between AXION's own modules sits in `axion/<module>/`. Tests live in `tests/`.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
