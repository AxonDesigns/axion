#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Rebuilds text/fonts/ from the pinned Noto Sans release (decision 98).

The bundled set is Noto Sans, the variable fonts with the width and weight axes, roman and
italic, cut down to Latin: Basic Latin, Latin-1, Latin Extended-A, the combining diacritical
marks, general punctuation, currency signs and a few symbols. The cut keeps every OpenType
layout feature (GSUB, GPOS, GDEF), the variation tables and the names, so shaping behaves as with
the full font for the characters kept.

Reproducible: the release archive is checked against its SHA-256 and the subsetter is fontTools
at the version below, so running this again gives the same bytes (the script prints their
SHA-256, which text/fonts/README.md records).

Usage (needs network access once, and fontTools; a virtual environment is fine):
    python3 -m venv /tmp/ftv && /tmp/ftv/bin/pip install fonttools==4.68.0
    /tmp/ftv/bin/python -I tools/fonts/subset_noto.py [--zip NotoSans-v2.015.zip]
"""
import argparse
import hashlib
import io
import os
import sys
import urllib.request
import zipfile

RELEASE = "NotoSans-v2.015"
URL = ("https://github.com/notofonts/latin-greek-cyrillic/releases/download/"
       f"{RELEASE}/{RELEASE}.zip")
SHA256 = "0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5"
FONTTOOLS = "4.68.0"

FONTS = {
    "NotoSans[wdth,wght].ttf": "NotoSans/unhinted/variable-ttf/NotoSans[wdth,wght].ttf",
    "NotoSans-Italic[wdth,wght].ttf":
        "NotoSans/unhinted/variable-ttf/NotoSans-Italic[wdth,wght].ttf",
}

UNICODES = [
    "U+0000-007F",  # Basic Latin
    "U+00A0-00FF",  # Latin-1 Supplement
    "U+0100-017F",  # Latin Extended-A
    "U+0300-036F",  # Combining Diacritical Marks (graphemes such as e + U+0301)
    "U+02BB-02BC", "U+02C6", "U+02DA", "U+02DC",  # modifier letters
    "U+2000-206F",  # General Punctuation (dashes, quotes, ellipsis, spaces)
    "U+20A0-20C0",  # Currency Symbols
    "U+2122", "U+2190-2193", "U+2212", "U+2215",  # trademark, arrows, minus, division slash
    "U+FB01-FB02",  # the fi and fl ligature characters
    "U+FEFF", "U+FFFD",
]

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "text", "fonts")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", help="the release archive, if already downloaded")
    args = ap.parse_args()

    import fontTools
    from fontTools import subset

    if fontTools.version != FONTTOOLS:
        sys.exit(f"needs fontTools {FONTTOOLS} for reproducible output (found {fontTools.version})")

    if args.zip:
        with open(args.zip, "rb") as f:
            data = f.read()
    else:
        with urllib.request.urlopen(URL) as r:
            data = r.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        sys.exit(f"{RELEASE}.zip: SHA-256 {digest}, expected {SHA256}")

    os.makedirs(OUT, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for out_name, member in FONTS.items():
            options = subset.Options()
            options.layout_features = ["*"]  # every feature: liga, kern, tnum, smcp, zero, frac...
            options.layout_scripts = ["*"]
            options.name_IDs = ["*"]
            options.name_languages = ["*"]
            options.notdef_outline = True
            options.glyph_names = False
            options.hinting = False  # the unhinted release has no instructions anyway
            options.recalc_timestamp = False  # keep head.modified: same bytes on every run
            options.drop_tables = [t for t in options.drop_tables if t not in ("STAT",)]
            font = subset.load_font(io.BytesIO(z.read(member)), options)
            s = subset.Subsetter(options)
            s.populate(unicodes=subset.parse_unicodes(",".join(UNICODES)))
            s.subset(font)
            path = os.path.join(OUT, out_name)
            subset.save_font(font, path, options)
            with open(path, "rb") as f:
                out = f.read()
            print(f"{hashlib.sha256(out).hexdigest()}  {out_name}  ({len(out)} bytes)")
        with open(os.path.join(OUT, "OFL.txt"), "wb") as f:
            f.write(z.read("OFL.txt"))


if __name__ == "__main__":
    main()
