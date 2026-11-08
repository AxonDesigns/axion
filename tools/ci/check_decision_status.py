#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Fails, listing them, when a decision's status in docs/decisions.md's index disagrees with the
# entry's own **Status.** line. They agree when the line starts with the index's word: Approved and
# "Approved by the owner." (or "Approved (part of the approved plan)."), Deviation and
# "Deviation, ...", Implementation and "Implementation, pending the owner's approval.", and so on.
# Also fails when an index row has no entry, an entry no index row, or an entry no status line.
#
# Usage: python3 tools/ci/check_decision_status.py [path]   (from anywhere; default: the
# repository's docs/decisions.md; needs Python 3 only)
import os
import re
import sys

# Notes: decision 38, notes the owner asked for rather than a decision.
STATUSES = ("Approved", "Clarification", "Deviation", "Implementation", "Notes")
ROW = re.compile(r"^\|\s*(\d+)\s*\|.*\|\s*([A-Za-z]+)\s*\|\s*$")
HEADING = re.compile(r"^## (\d+)\.\s")
STATUS = re.compile(r"^\*\*Status\.\*\*\s*(\S.*)$")


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = argv[1] if len(argv) > 1 else os.path.join(root, "docs", "decisions.md")
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()

    index = {}
    entries = {}  # number -> (heading line number, status text or None)
    current = None
    for i, line in enumerate(lines, 1):
        heading = HEADING.match(line)
        if heading:
            current = int(heading.group(1))
            entries[current] = (i, None)
            continue
        if current is None:
            row = ROW.match(line)
            if row:
                index[int(row.group(1))] = row.group(2)
            continue
        status = STATUS.match(line)
        if status and entries[current][1] is None:
            entries[current] = (entries[current][0], status.group(1))

    problems = []
    for n in sorted(set(index) | set(entries)):
        if n not in entries:
            problems.append(f"{n}: in the index ({index[n]}) but has no entry")
            continue
        at, text = entries[n]
        if n not in index:
            problems.append(f"{n} (line {at}): has an entry but no index row")
            continue
        word = index[n]
        if word not in STATUSES:
            problems.append(f"{n}: index status '{word}' is not one of {', '.join(STATUSES)}")
        elif text is None:
            problems.append(f"{n} (line {at}): no **Status.** line (index: {word})")
        elif not re.match(re.escape(word) + r"\b", text):
            problems.append(f"{n} (line {at}): index says {word}, the entry says \"{text}\"")

    if problems:
        for p in problems:
            print(p)
        print(f"check_decision_status: {len(problems)} decision(s) disagree", file=sys.stderr)
        return 1
    print(f"check_decision_status: the index and the {len(entries)} entries agree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
