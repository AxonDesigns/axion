#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Compares the coverage tests' images across WebGPU implementations (decision 56).

Each argument is a directory of <scene>.ppm files written by test_render_wgpu_coverage with
AX_TEST_ARTIFACT_DIR set, one directory per implementation (Dawn on lavapipe, wgpu-native on
lavapipe, Dawn on Metal). Every pair of directories must hold the same scenes, of the same size,
with every channel of every pixel within --tolerance (8-bit units). Prints the largest difference
per scene and pair; exits 1 if any exceeds the tolerance or a scene is missing.

    tools/ci/compare_ppm.py --tolerance 3 coverage-dawn-linux coverage-wgpu-native-linux ...
"""
import argparse
import itertools
import os
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:  # magic, width, height, maxval; '#' comments are not written
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    if fields[0] != b"P6" or fields[3] != b"255":
        raise ValueError("%s: not an 8-bit P6 file" % path)
    w, h = int(fields[1]), int(fields[2])
    pixels = data[pos + 1:pos + 1 + w * h * 3]
    if len(pixels) != w * h * 3:
        raise ValueError("%s: truncated" % path)
    return w, h, pixels


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tolerance", type=int, default=3)
    ap.add_argument("dirs", nargs="+")
    a = ap.parse_args()
    if len(a.dirs) < 2:
        sys.exit("compare_ppm.py: need at least two directories")

    scenes = {d: sorted(f[:-4] for f in os.listdir(d) if f.endswith(".ppm")) for d in a.dirs}
    failed = False
    for d in a.dirs:
        print("%s: %d scenes" % (d, len(scenes[d])))
        if not scenes[d]:
            print("  no images", file=sys.stderr)
            failed = True
    for left, right in itertools.combinations(a.dirs, 2):
        missing = set(scenes[left]) ^ set(scenes[right])
        if missing:
            print("%s vs %s: scenes in only one: %s" % (left, right, ", ".join(sorted(missing))))
            failed = True
        for scene in sorted(set(scenes[left]) & set(scenes[right])):
            lw, lh, lp = read_ppm(os.path.join(left, scene + ".ppm"))
            rw, rh, rp = read_ppm(os.path.join(right, scene + ".ppm"))
            if (lw, lh) != (rw, rh):
                print("%s: %s is %dx%d, %s is %dx%d" % (scene, left, lw, lh, right, rw, rh))
                failed = True
                continue
            worst, at = 0, 0
            for i in range(len(lp)):
                diff = abs(lp[i] - rp[i])
                if diff > worst:
                    worst, at = diff, i
            status = "ok" if worst <= a.tolerance else "FAIL"
            pixel = at // 3
            print("%-14s %s vs %s: max difference %d/255 at (%d, %d) %s" %
                  (scene, os.path.basename(left), os.path.basename(right), worst, pixel % lw,
                   pixel // lw, status))
            failed |= worst > a.tolerance
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
