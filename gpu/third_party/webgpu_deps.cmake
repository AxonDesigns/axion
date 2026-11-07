# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Pinned WebGPU implementations: the only place their versions, URLs and checksums live.
# Provenance, licences and the update procedure are in README.md next to this file.
#
# Every archive is an official release asset of the upstream project, verified against the
# SHA-256 below before it is extracted. The digests match the ones GitHub records for each asset.

# ---- Dawn (primary) ----------------------------------------------------------------------------
# https://github.com/google/dawn/releases/tag/v20261022.205350 (published 2026-10-23)
set(AX_DAWN_VERSION "v20261022.205350")
set(AX_DAWN_COMMIT  "537308541a072a4b348a6bdd4d54e808f4239a7a")
set(_ax_dawn_base
    "https://github.com/google/dawn/releases/download/${AX_DAWN_VERSION}/Dawn-${AX_DAWN_COMMIT}")

# Release builds only: the Debug packages are 250-850 MB, and Dawn keeps its API validation in
# release builds anyway. AXION's Debug configurations link the Release package (decision 43).
set(AX_DAWN_URL_Linux_x86_64   "${_ax_dawn_base}-ubuntu-latest-Release.tar.gz")
set(AX_DAWN_SHA256_Linux_x86_64
    "b0ef62006c4937b67833a3646f72632a0b64ab509675085fa6b505fb36599e60")
set(AX_DAWN_URL_Darwin_arm64   "${_ax_dawn_base}-macos-latest-Release.tar.gz")
set(AX_DAWN_SHA256_Darwin_arm64
    "44f779436db8d1cb4f06270284b6fdcf4eef6e643d1aa266ffb5fd0bf521c8c2")
set(AX_DAWN_URL_Darwin_x86_64  "${_ax_dawn_base}-macos-15-intel-Release.tar.gz")
set(AX_DAWN_SHA256_Darwin_x86_64
    "e1106bc1a743019c9f463fcc72ea43727257c9576036d0905e7bb4527da58e67")

# ---- wgpu-native (alternative, CI only) --------------------------------------------------------
# https://github.com/gfx-rs/wgpu-native/releases/tag/v29.0.1.1 (published 2026-06-23)
set(AX_WGPU_NATIVE_VERSION "v29.0.1.1")
set(AX_WGPU_NATIVE_COMMIT  "6aed50955d934ac36049ba8d002034841633ae02")
set(_ax_wgpu_base
    "https://github.com/gfx-rs/wgpu-native/releases/download/${AX_WGPU_NATIVE_VERSION}")

set(AX_WGPU_NATIVE_URL_Linux_x86_64 "${_ax_wgpu_base}/wgpu-linux-x86_64-release.zip")
set(AX_WGPU_NATIVE_SHA256_Linux_x86_64
    "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a")
