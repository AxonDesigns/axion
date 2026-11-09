#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Checks that the same headless tests give byte-identical images on every platform (decision 86).

Each argument is a directory of <suite>__<name>.png files (and <suite>__<name>.txt text goldens,
such as the semantic tree of decision 108) written by the golden tests with
AX_TEST_IMAGE_DIR set, one directory per platform (Linux, macOS, Windows, the web). Every directory
must hold the same files with the same bytes: AXION's PNG encoder depends only on the pixels, so
equal files mean equal pixels. For a file that differs, the first differing pixel is reported.
Exits 1 on any difference or missing file.

    tools/ci/compare_images.py images-linux images-macos images-windows images-web
"""
import argparse
import os
import struct
import sys
import zlib


def pixels(path):
    """(width, height, channels, rows) of an 8-bit non-interlaced PNG, unfiltered."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, width, height, channels = 8, b"", 0, 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, _, color = struct.unpack(">IIBB", body[:10])
            channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw, stride = zlib.decompress(idat), width * channels
    rows, prev = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = row[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            paeth = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            row[x] = (row[x] + (0, a, b, (a + b) // 2, paeth)[kind]) & 255
        rows.append(bytes(row))
        prev = row
    return width, height, channels, rows


def first_difference(left, right):
    lw, lh, lc, lrows = pixels(left)
    rw, rh, rc, rrows = pixels(right)
    if (lw, lh) != (rw, rh):
        return "sizes %dx%d and %dx%d" % (lw, lh, rw, rh)
    if lc != rc:
        return "channels %d and %d" % (lc, rc)
    for y in range(lh):
        if lrows[y] != rrows[y]:
            x = next(i for i in range(len(lrows[y])) if lrows[y][i] != rrows[y][i]) // lc
            return "pixel (%d, %d): %s and %s" % (
                x, y, tuple(lrows[y][x * lc:(x + 1) * lc]), tuple(rrows[y][x * rc:(x + 1) * rc]))
    return "same pixels, different bytes"


def first_text_difference(left, right):
    with open(left, "rb") as f:
        a = f.read().split(b"\n")
    with open(right, "rb") as f:
        b = f.read().split(b"\n")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return "line %d: %r and %r" % (i + 1, x.decode("utf-8", "replace"),
                                           y.decode("utf-8", "replace"))
    return "%d and %d lines" % (len(a), len(b))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("dirs", nargs="+")
    args = parser.parse_args()
    if len(args.dirs) < 2:
        sys.exit("compare_images.py: need at least two directories")
    files = {d: sorted(f for f in os.listdir(d) if f.endswith((".png", ".txt")))
             for d in args.dirs}
    reference = args.dirs[0]
    failed = False
    if not files[reference]:
        sys.exit("compare_images.py: %s holds no images" % reference)
    for other in args.dirs[1:]:
        missing = set(files[reference]) ^ set(files[other])
        for name in sorted(missing):
            print("%s: only in %s" % (name, reference if name in files[reference] else other))
            failed = True
        same = 0
        for name in sorted(set(files[reference]) & set(files[other])):
            with open(os.path.join(reference, name), "rb") as f:
                a = f.read()
            with open(os.path.join(other, name), "rb") as f:
                b = f.read()
            if a == b:
                same += 1
                continue
            failed = True
            compare = first_text_difference if name.endswith(".txt") else first_difference
            print("%s: %s and %s differ: %s" % (name, reference, other, compare(
                os.path.join(reference, name), os.path.join(other, name))))
        print("%s against %s: %d identical files" % (other, reference, same))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
