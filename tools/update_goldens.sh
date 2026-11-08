#!/bin/sh
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Rewrites the golden images in tests/goldens from what the tests draw now (decision 82). Run it
# on purpose, after checking that a change of the pictures is intended, and review the new PNGs
# before committing them. CI never runs it, and the tests refuse to update when CI is set.
#
# Usage: tools/update_goldens.sh [configure preset]   (default: clang-release)
set -eu

if [ -n "${CI:-}" ]; then
    echo "update_goldens.sh: refusing to run in CI" >&2
    exit 1
fi
preset="${1:-clang-release}"
cd "$(dirname "$0")/.."
cmake --preset "$preset"
cmake --build --preset "$preset"
cmake --build --preset "$preset" --target update_goldens
git status --short tests/goldens
