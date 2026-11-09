#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Re-measures the size handshake's bound (decision 57: half a refresh, within 2-8 ms) with real
layout and text: examples/m1_nodes (flex, 30 paragraphs from 11 to 48 px, implicit animations) on
each output, at rest and while KWin resizes the window about 120 times a second (every new size
lays every paragraph out again). Prints, per run, what --stats reports per second: presented,
missed refreshes, configures that waited for the scene and got their snapshot in time (scene late)
and those that hit the bound (forced), and the time from a change to the screen.

Missed refreshes are counted only while the window presents (issue #15): the time it rests
without frames (decision 95) is reported apart, so "animating" is each second's time not at
rest, and "missed per animating s" is the misses over that time. --per-second prints the rows.

    tools/diag/measure_m1_handshake.py --binary build/clang-release/examples/m1_nodes/m1_nodes

Do not touch the windows while it runs. Needs qdbus6, kscreen-doctor and journalctl.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

PLACE = """import QtQuick
import org.kde.kwin
Item {
    property var win: null
    property real t: 0
    property real bx: 0
    property real by: 0
    Timer { interval: 8; repeat: true; running: true
        onTriggered: {
            if (win == null) {
                const list = Workspace.stackingOrder;
                for (let i = 0; i < list.length; ++i)
                    if (list[i].pid == %(pid)d && list[i].caption == "AXION m1_nodes") win = list[i];
                let o = null;
                for (let k = 0; k < Workspace.screens.length; ++k)
                    if (Workspace.screens[k].name == "%(output)s") o = Workspace.screens[k];
                if (win == null || o == null) { win = null; return; }
                Workspace.sendClientToScreen(win, o);
                bx = o.geometry.x + 80; by = o.geometry.y + 60;
                win.frameGeometry = Qt.rect(bx, by, 860, 760);
                return;
            }
            if (%(resize)s) {
                t += 0.008;
                win.frameGeometry = Qt.rect(bx, by, Math.round(860 + 260 * Math.sin(t * 2.1)),
                                            Math.round(760 + 160 * Math.sin(t * 1.3)));
            }
        }
    }
}
"""

STATS = re.compile(r"stats: (\w+) \| presented (\d+) \| missed (\d+) \| frames (\d+) \| snapshots shown "
                   r"(\d+) \| refresh ([\d.]+) ms \| scene late (\d+) \| forced (\d+) \| change -> screen "
                   r"([\d.]+) ms \(max ([\d.]+)\).*?at rest ([\d.]+) ms")


def qdbus(*args):
    subprocess.run(["qdbus6", "org.kde.KWin", "/Scripting"] + list(args), capture_output=True)


def run_case(binary, output, resize, seconds, n, tmp, per_second):
    proc = subprocess.Popen([binary, "--stats", "--duration=%d" % (seconds + 2)],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    time.sleep(0.5)
    d = os.path.join(tmp, "kwin%d" % n)
    os.makedirs(d)
    with open(os.path.join(d, "main.qml"), "w") as f:
        f.write(PLACE % {"pid": proc.pid, "output": output, "resize": "true" if resize else "false"})
    plugin = "axion_m1_handshake_%d_%d" % (os.getpid(), n)
    qdbus("org.kde.kwin.Scripting.loadDeclarativeScript", os.path.join(d, "main.qml"), plugin)
    qdbus("org.kde.kwin.Scripting.start")
    out, _ = proc.communicate(timeout=seconds + 20)
    qdbus("org.kde.kwin.Scripting.unloadScript", plugin)
    rows = [m.groups() for m in STATS.finditer(out)][2:]  # the first two seconds warm up
    if not rows:
        return None
    if per_second:
        for r in rows:
            print("    %s %s: presented %s, missed %s, at rest %.0f ms" % (
                output, "resize" if resize else "quiet", r[1], r[2], float(r[10])))
    k = len(rows)
    tot = lambda i: sum(int(r[i]) for r in rows)
    rest_ms = sum(min(float(r[10]), 1000.0) for r in rows)
    animating_s = (k * 1000.0 - rest_ms) / 1000.0
    return {"secs": k, "presented": tot(1) / k, "missed": tot(2) / k, "late": tot(6) / k,
            "forced": tot(7) / k, "refresh": float(rows[-1][5]),
            "latency": sum(float(r[8]) for r in rows) / k, "latency_max": max(float(r[9]) for r in rows),
            "animating": animating_s / k,
            "missed_animating": tot(2) / animating_s if animating_s > 0 else 0.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default="build/clang-release/examples/m1_nodes/m1_nodes")
    ap.add_argument("--seconds", type=int, default=8)
    ap.add_argument("--outputs", default="HDMI-A-1,DP-2")
    ap.add_argument("--per-second", action="store_true")
    ap.add_argument("--states", default="quiet,resize")
    args = ap.parse_args()
    print("| Output | State | Refresh | Presented/s | Missed/s | Animating s/s | "
          "Missed per animating s | Scene late/s | Forced/s | Change -> screen (mean, max) |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    n = 0
    with tempfile.TemporaryDirectory() as tmp:
        for output in args.outputs.split(","):
            for resize in [st == "resize" for st in args.states.split(",")]:
                n += 1
                r = run_case(args.binary, output, resize, args.seconds, n, tmp, args.per_second)
                if r is None:
                    print("| %s | %s | no stats |" % (output, "resize" if resize else "quiet"))
                    continue
                print("| %s | %s | %.2f ms | %.1f | %.2f | %.2f | %.2f | %.1f | %.2f | "
                      "%.1f ms, %.1f ms |" % (
                          output, "resize" if resize else "quiet", r["refresh"], r["presented"],
                          r["missed"], r["animating"], r["missed_animating"], r["late"],
                          r["forced"], r["latency"], r["latency_max"]))
                sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main())
