#!/bin/sh
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Fails, listing them, when tracked text files do not end in a newline (.editorconfig:
# insert_final_newline = true). Skipped, as .editorconfig does: binary files (git's own test: a NUL
# byte in the first 8000 bytes), empty files, and tests/goldens/ (golden images, decision 82).
#
# Usage: tools/ci/check_final_newline.sh   (from anywhere in the repository; needs git and sh)
set -eu

cd "$(git rev-parse --show-toplevel)"
bad=0
list=$(mktemp)
trap 'rm -f "$list"' EXIT
# -I: text files only; -l -e '': every text file that has content.
git grep -I -l -e '' -- . ':(exclude)tests/goldens/' >"$list" || true
while IFS= read -r f; do
    if [ -n "$(tail -c 1 "$f" | tr -d '\n')" ]; then
        echo "no final newline: $f"
        bad=$((bad + 1))
    fi
done <"$list"
if [ "$bad" -ne 0 ]; then
    echo "check_final_newline: $bad file(s) do not end in a newline" >&2
    exit 1
fi
echo "check_final_newline: every tracked text file ends in a newline"
