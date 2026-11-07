#!/bin/sh
# Builds repro.c with the protocol code generated from the installed wayland-protocols (>= 1.38).
set -eu
cd "$(dirname "$0")"
out=${1:-/tmp/weston-fifo-repro}
mkdir -p "$out"
p=$(pkg-config --variable=pkgdatadir wayland-protocols)
for x in "stable/xdg-shell/xdg-shell.xml" "staging/fifo/fifo-v1.xml"; do
    n=$(basename "$x" .xml)
    wayland-scanner client-header "$p/$x" "$out/$n-client-protocol.h"
    wayland-scanner private-code "$p/$x" "$out/$n-protocol.c"
done
cc -std=c11 -Wall -Wextra -I"$out" -o "$out/repro" repro.c "$out/xdg-shell-protocol.c" \
    "$out/fifo-v1-protocol.c" $(pkg-config --cflags --libs wayland-client)
echo "$out/repro"
