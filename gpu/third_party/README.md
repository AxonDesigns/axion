# WebGPU implementations

gpu/ codes against the standard `webgpu.h` and links one prebuilt implementation, chosen with the
CMake cache variable `AX_WEBGPU`. Versions, URLs and checksums live only in
[`webgpu_deps.cmake`](webgpu_deps.cmake); `cmake/AxionWebGPU.cmake` downloads the archive for the
target, checks its SHA-256 and unpacks it into `.deps/` at the repository root (shared by every
build tree, ignored by git). `AX_DAWN_ROOT` / `AX_WGPU_NATIVE_ROOT` use an unpacked package
instead, for offline builds.

## Dawn (primary, `AX_WEBGPU=dawn`)

| | |
| --- | --- |
| Project | Dawn, Google's WebGPU implementation: <https://dawn.googlesource.com/dawn>, mirrored at <https://github.com/google/dawn> |
| Version | Release `v20261022.205350`, commit `537308541a072a4b348a6bdd4d54e808f4239a7a`, published 2026-10-23 |
| Source of the binaries | The official release assets the Dawn project's own CI builds and publishes at <https://github.com/google/dawn/releases/tag/v20261022.205350> |
| Packages used | `ubuntu-latest-Release` (Linux x86_64), `macos-latest-Release` (macOS arm64), `macos-15-intel-Release` (macOS x86_64) |
| What they contain | `libwebgpu_dawn.a` (static, C++), its headers and a CMake package (`dawn::webgpu_dawn`) |
| Licence | BSD 3-Clause ([`licenses/dawn-LICENSE`](licenses/dawn-LICENSE), copied from the same commit) |

`libwebgpu_dawn.a` also carries code Dawn vendors. All of it is permissively licensed:

| Component | Revision (from Dawn's `DEPS`) | Licence |
| --- | --- | --- |
| Tint (part of Dawn) | same commit | BSD 3-Clause |
| Abseil | `af5aede14a0b4013be6890148d34e0fd7b1fac69` | Apache 2.0 |
| SPIRV-Tools | `7651d9234622041b3f1b09e5c684c7c6eb352d08` | Apache 2.0 |
| SPIRV-Headers | `fbf86836a9b1966586faf7be7bb8a6440e156d62` | MIT-style (Khronos) |
| Vulkan-Headers (compiled in, header only) | `acbb9391c0a597272800cd3f4363661c00a27c77` | Apache 2.0 or MIT |

`axion export` will have to list these in the third-party notices it generates; until it exists,
nothing AXION ships links Dawn.

Notes:
- **Release packages for every configuration.** The Debug packages are 250-850 MB. Dawn keeps its
  API validation in release builds, so AXION's Debug and sanitizer builds link the Release
  package too.
- **C++ runtime.** Dawn is a static C++ library: gpu/ links libstdc++ (Linux) or libc++ (Apple)
  explicitly, because AXION links with the C compiler driver.
- **Vulkan on Linux.** The Linux package loads `libvulkan.so.1` at run time and supports
  `VK_KHR_wayland_surface` and Xlib surfaces; it links no Wayland or X11 library.
- **No fallback adapter.** This package has no SwiftShader, which is the only adapter Dawn treats
  as a fallback, so `forceFallbackAdapter` finds nothing. gpu/'s software mode picks the CPU-type
  adapter instead (lavapipe in CI); see `docs/decisions.md`.

## wgpu-native (alternative, `AX_WEBGPU=wgpu-native`)

| | |
| --- | --- |
| Project | wgpu-native, C bindings for wgpu: <https://github.com/gfx-rs/wgpu-native> |
| Version | `v29.0.1.1`, commit `6aed50955d934ac36049ba8d002034841633ae02`, published 2026-06-23 |
| Source of the binaries | Official release assets: <https://github.com/gfx-rs/wgpu-native/releases/tag/v29.0.1.1> |
| Packages used | `wgpu-linux-x86_64-release.zip` |
| What it contains | `libwgpu_native.a` / `.so`, `webgpu.h` (upstream webgpu-headers) and `wgpu.h` (extensions) |
| Licence | MIT or Apache 2.0, at the user's option ([`licenses/`](licenses/)); the static library also contains Rust crates under permissive licences |

The architecture document keeps wgpu-native compiling in CI as the alternative to Dawn; it is
measured against Dawn at M1. Only the Linux package is pinned, for the CI job that builds gpu/
and runs its offscreen tests with it. AXION never ships it.

## Checksums (SHA-256)

Archives (also in `webgpu_deps.cmake`; they equal the digests GitHub shows for each asset):

```text
b0ef62006c4937b67833a3646f72632a0b64ab509675085fa6b505fb36599e60  Dawn-537308541a072a4b348a6bdd4d54e808f4239a7a-ubuntu-latest-Release.tar.gz
44f779436db8d1cb4f06270284b6fdcf4eef6e643d1aa266ffb5fd0bf521c8c2  Dawn-537308541a072a4b348a6bdd4d54e808f4239a7a-macos-latest-Release.tar.gz
e1106bc1a743019c9f463fcc72ea43727257c9576036d0905e7bb4527da58e67  Dawn-537308541a072a4b348a6bdd4d54e808f4239a7a-macos-15-intel-Release.tar.gz
95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a  wgpu-linux-x86_64-release.zip
```

Licence files:

```text
0493f897193af1796d5054659f45ec7d4c5af648fa67a99f01d30e55cc805abc  licenses/dawn-LICENSE
c7fea58d1cfe49634cd92e54fc10a9d871f4b275321a4cd8c09e449122caaeb4  licenses/wgpu-native-LICENSE.MIT
a6cba85bc92e0cff7a450b1d873c0eaa2e9fc96bf472df0247a26bec77bf3ff9  licenses/wgpu-native-LICENSE.APACHE
```

## Updating

1. Pick a release at least two weeks old from the project's own GitHub releases. Never use
   binaries from anyone else.
2. Update the version, commit, URLs and checksums in `webgpu_deps.cmake` and here, and the
   licence files from the same commit. Take the checksums from the downloaded archives and
   compare them with GitHub's digests (`gh api repos/google/dawn/releases/tags/<tag>`).
3. For Dawn, update the bundled-component table from that commit's `DEPS`.
4. Build and run the gpu/ tests with both implementations, and note the update in
   `docs/decisions.md`.
