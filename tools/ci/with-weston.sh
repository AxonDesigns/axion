#!/usr/bin/env bash
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Runs a command with a headless weston compositor for the Wayland integration tests:
#
#   tools/ci/with-weston.sh ctest --preset gcc-asan
#
# Weston gets its own XDG_RUNTIME_DIR and socket, so it never touches a desktop session. The
# command sees AX_TEST_WAYLAND_DISPLAY (the tests connect only to that, never to WAYLAND_DISPLAY)
# and AX_REQUIRE_WAYLAND_TESTS=1 (a missing compositor fails the tests instead of skipping them).
set -euo pipefail

if ! command -v weston >/dev/null 2>&1; then
    echo "with-weston.sh: weston is not installed" >&2
    exit 1
fi

runtime_dir="$(mktemp -d "${TMPDIR:-/tmp}/axion-weston.XXXXXX")"
chmod 700 "$runtime_dir"
socket="axion-test-$$"
log="$runtime_dir/weston.log"

# Weston 14 renamed --use-pixman to --renderer=pixman; accept either.
if weston --help 2>&1 | grep -q -- '--renderer='; then
    renderer=(--renderer=pixman)
else
    renderer=(--use-pixman)
fi

XDG_RUNTIME_DIR="$runtime_dir" weston --backend=headless "${renderer[@]}" \
    --socket="$socket" --idle-time=0 --no-config --width=1280 --height=720 \
    >"$log" 2>&1 &
weston_pid=$!

cleanup() {
    kill "$weston_pid" 2>/dev/null || true
    wait "$weston_pid" 2>/dev/null || true
    rm -rf "$runtime_dir"
}
trap cleanup EXIT

for _ in $(seq 1 100); do
    [[ -S "$runtime_dir/$socket" ]] && break
    if ! kill -0 "$weston_pid" 2>/dev/null; then
        echo "with-weston.sh: weston exited early:" >&2
        cat "$log" >&2
        exit 1
    fi
    sleep 0.1
done
if [[ ! -S "$runtime_dir/$socket" ]]; then
    echo "with-weston.sh: weston did not create its socket in 10 s:" >&2
    cat "$log" >&2
    exit 1
fi

status=0
XDG_RUNTIME_DIR="$runtime_dir" AX_TEST_WAYLAND_DISPLAY="$socket" AX_REQUIRE_WAYLAND_TESTS=1 \
    "$@" || status=$?

if [[ $status -ne 0 ]]; then
    echo "---- weston log ----" >&2
    cat "$log" >&2
fi
exit $status
