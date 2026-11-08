#!/bin/sh
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Captures how a KDE application draws text, for tuning AXION's text coverage correction
# (decision 101): a Qt Widgets window (kdialog --textbox, the desktop's own text rendering) showing
# a fixed sample in the desktop's general font, captured by Spectacle on the monitor it opens on.
#
#   tools/diag/kde_text_capture.sh OUT.png
#
# Reproducible: the sample is below; the font, size and rendering settings are the desktop's
# (printed with the capture: KDE's general font, fontconfig's match and its hinting); the window
# opens where KWin puts new windows (move the pointer to the monitor to capture first). Then
# tools/diag/text_weight compares the capture with AXION's rendering of the same text:
#
#   build/diag/tools/diag/text_weight --capture OUT.png --scale 1 (or 1.325)
#
# Do not touch the desktop for the two seconds the window is open.
set -eu
out=${1:?usage: kde_text_capture.sh OUT.png}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cat > "$dir/sample.txt" <<'TEXT'
The quick brown fox jumps over the lazy dog
Sphinx of black quartz, judge my vow
0123456789 office AV To Wa
TEXT
echo "KDE general font: $(kreadconfig6 --group General --key font 2>/dev/null || true) (empty: Noto Sans,10)"
echo "fontconfig: $(fc-match -f '%{family} %{style} hintstyle=%{hintstyle} rgba=%{rgba} antialias=%{antialias}' sans-serif)"
echo "color scheme: $(kreadconfig6 --group General --key ColorScheme 2>/dev/null || true)"
kdialog --title "AXION text reference" --textbox "$dir/sample.txt" 700 260 &
pid=$!
sleep 2
spectacle --background --nonotify --activewindow --output "$out"
sleep 1
kill "$pid" 2>/dev/null || true
echo "captured: $out"
