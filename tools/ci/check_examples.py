#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# Fails, listing them, when an example under examples/ would silently stop building: a directory
# without a CMakeLists.txt, one whose CMakeLists.txt defines no executable named after the
# directory (add_executable(<dir> ...)), one that lists a source file that does not exist, or one
# that examples/CMakeLists.txt never adds (add_subdirectory). PR #12 emptied m1_nodes'
# CMakeLists.txt (7ffa651) and nothing noticed (decision 103).
#
# Usage: python3 tools/ci/check_examples.py [examples dir]   (needs Python 3 only, no build)
import os
import re
import sys


def strip_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    examples = argv[1] if len(argv) > 1 else os.path.join(root, "examples")
    with open(os.path.join(examples, "CMakeLists.txt"), encoding="utf-8") as f:
        added = set(re.findall(r"add_subdirectory\(\s*([\w.-]+)", strip_comments(f.read())))
    problems = []
    count = 0
    for name in sorted(os.listdir(examples)):
        d = os.path.join(examples, name)
        if not os.path.isdir(d):
            continue
        count += 1
        cm = os.path.join(d, "CMakeLists.txt")
        if not os.path.isfile(cm):
            problems.append("%s: no CMakeLists.txt" % name)
            continue
        with open(cm, encoding="utf-8") as f:
            text = strip_comments(f.read())
        m = re.search(r"add_executable\(\s*%s\b([^)]*)\)" % re.escape(name), text)
        if m is None:
            problems.append("%s: its CMakeLists.txt defines no executable '%s'" % (name, name))
        else:
            for src in m.group(1).split():
                if "$" in src or src.startswith(("WIN32", "MACOSX_BUNDLE", "EXCLUDE_FROM_ALL")):
                    continue
                if not os.path.isfile(os.path.join(d, src)):
                    problems.append("%s: lists %s, which does not exist" % (name, src))
        if name not in added:
            problems.append("%s: examples/CMakeLists.txt does not add it" % name)
    if problems:
        print("check_examples: %d problem(s):" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("check_examples: %d examples define their executables" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
