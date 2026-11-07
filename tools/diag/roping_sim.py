#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Simulates the roping of thin rotated lines (development only; decision 58).

A line w device pixels wide, rotated 3 degrees from vertical, light on dark (the colors of
examples/m0_window: #E8ECF4 on #1E2129), is filtered into pixels and blended:

  - box, sRGB:    exact pixel-area coverage blended in sRGB space (ax_render_wgpu's default);
  - box, linear:  the same coverage blended in linear light;
  - tent, sRGB:   a 1-pixel-radius tent filter (support 2 px) blended in sRGB space.

For each scanline it sums the light the line adds over the background (relative luminance, after
decoding the displayed sRGB values) and prints max/min over the scanlines: 1.00 means the line
emits the same light everywhere; larger values are the beads the eye sees.

    tools/diag/roping_sim.py
"""
import numpy as np

BG = np.array([0x1E, 0x21, 0x29]) / 255.0
FG = np.array([0xE8, 0xEC, 0xF4]) / 255.0
LUMA = np.array([0.2126, 0.7152, 0.0722])


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.maximum(c, 0) ** (1 / 2.4) - 0.055)


def luminance(srgb):
    return (srgb_to_linear(srgb) * LUMA).sum(-1)


def coverage(width, degrees, filt, h=120, w=24, samples=16):
    """Filtered coverage of the line, by supersampling the filter's support."""
    theta = np.radians(degrees)
    normal = np.array([np.cos(theta), -np.sin(theta)])
    ys, xs = np.mgrid[0:h, 0:w]
    cx, cy = w / 2 + 0.3, h / 2
    radius = 0.5 if filt == "box" else 1.0
    n = int(samples * 2 * radius)
    offsets = (np.arange(n) + 0.5) / n * 2 * radius - radius
    out = np.zeros((h, w))
    total = 0.0
    for du in offsets:
        for dv in offsets:
            weight = 1.0 if filt == "box" else (1 - abs(du)) * (1 - abs(dv))
            d = (xs + 0.5 + du - cx) * normal[0] + (ys + 0.5 + dv - cy) * normal[1]
            out += weight * (np.abs(d) <= width / 2)
            total += weight
    return out / total


def blend(cov, space):
    c = cov[..., None]
    if space == "srgb":
        return c * FG + (1 - c) * BG
    return linear_to_srgb(c * srgb_to_linear(FG) + (1 - c) * srgb_to_linear(BG))


def roping(width, filt, space):
    light = luminance(blend(coverage(width, 3.0, filt), space)) - luminance(BG)
    rows = light[10:-10].sum(axis=1)
    return rows.max() / rows.min()


def main():
    print("| Width (px) | Box, sRGB (default) | Box, linear | Tent 1 px, sRGB |")
    print("| --- | --- | --- | --- |")
    for width in (0.33, 0.66, 1.325, 1.99):
        cells = [roping(width, f, s) for f, s in (("box", "srgb"), ("box", "linear"),
                                                  ("tent", "srgb"))]
        print(f"| {width} | " + " | ".join(f"{v:.2f}" for v in cells) + " |")


if __name__ == "__main__":
    main()
