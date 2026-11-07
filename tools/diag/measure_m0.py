#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Measures examples/m0_window on a KDE Plasma (KWin) desktop across the whole matrix and prints a
summary table (development only; decision 49).

Matrix: renderer (shm, gpu with compositor pacing, gpu with driver pacing) x decorations (native,
none) x output (every enabled monitor) x state (quiet, resize).

A KWin declarative script (kwin_place_m0.qml.in) moves the window onto the output and, for
"resize" runs, resizes it about 120 times per second. With --manual-resize the script only places
the window and asks you to drag its corner, announcing each run that needs your hand first.

    tools/diag/measure_m0.py --binary build/clang-release/examples/m0_window/m0_window

Options: --seconds N (measured seconds per run, after a warm-up), --manual-resize,
--with-lavapipe (adds GPU rows on Mesa's software driver, Mesa's WSI on KWin),
--only-renderer, --only-output, --log FILE (raw output of every run).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
QML_TEMPLATE = os.path.join(HERE, "kwin_place_m0.qml.in")
LAVAPIPE_ICDS = ["/usr/share/vulkan/icd.d/lvp_icd.json", "/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"]

STATS_RE = re.compile(
    r"stats: interval (?P<interval>[\d.]+) ms .*?\| refresh (?P<refresh>[\d.]+) ms \| drawn (?P<drawn>\d+) "
    r"\| presented (?P<presented>\d+) \| missed (?P<missed>\d+) \| discarded (?P<discarded>\d+) "
    r"\| lost (?P<lost>\d+) \| configure->present (?P<lat_mean>[\d.]+) ms mean, (?P<lat_max>[\d.]+) ms max "
    r"\((?P<lat_n>\d+)\) \| draw (?P<draw_mean>[\d.]+) ms mean, (?P<draw_max>[\d.]+) ms max"
    r"(?: \| gpu: resizes (?P<resizes>\d+) \((?P<rs_mean>[\d.]+) ms mean, (?P<rs_max>[\d.]+) ms max\) "
    r"\| acquire (?P<acq_mean>[\d.]+) ms mean, (?P<acq_max>[\d.]+) ms max \| present (?P<pr_mean>[\d.]+) ms mean, "
    r"(?P<pr_max>[\d.]+) ms max \| deferred acks (?P<deferred>\d+) \(drain (?P<drain_max>[\d.]+) ms max\) "
    r"\| failed (?P<failed>\d+))?")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def outputs():
    """Enabled outputs as (name, refresh Hz), from kscreen-doctor."""
    text = re.sub(r"\x1b\[[0-9;]*m", "", run(["kscreen-doctor", "-o"]).stdout)
    result = []
    for block in text.split("Output: ")[1:]:
        name = block.split()[1]
        if "enabled" not in block.split("Modes:")[0]:
            continue
        current = re.search(r"\d+:\d+x\d+@([\d.]+)\*", block)
        result.append((name, float(current.group(1)) if current else 0.0))
    return result


def kwin_script(output, resize, pid):
    with open(QML_TEMPLATE) as f:
        qml = (f.read().replace("@OUTPUT@", output).replace("@RESIZE@", "true" if resize else "false")
               .replace("@PID@", str(pid)))
    # KWin rejects QML files whose names do not look like component names ("File name case
    # mismatch"), so the file gets a fixed CamelCase name in a fresh directory.
    path = os.path.join(tempfile.mkdtemp(prefix="axion-m0-"), "AxionM0Place.qml")
    with open(path, "w") as f:
        f.write(qml)
    return path


def qdbus(*args):
    return run(["qdbus6", "org.kde.KWin", "/Scripting"] + list(args))


def measure(binary, cfg, decorations, output, resize, seconds, manual, log):
    warmup = 3  # seconds dropped: window mapping, moving to the output, first configures
    args = [binary, "--renderer=" + cfg["renderer"], "--decorations=" + decorations, "--stats",
            "--duration=%d" % (warmup + seconds)] + cfg.get("args", [])
    env = dict(os.environ)
    env.update(cfg.get("env", {}))
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    script = kwin_script(output, resize and not manual, proc.pid)
    plugin = "axion_m0_measure_%d" % proc.pid
    time.sleep(1.0)
    qdbus("org.kde.kwin.Scripting.loadDeclarativeScript", script, plugin)
    qdbus("org.kde.kwin.Scripting.start")
    out, _ = proc.communicate(timeout=warmup + seconds + 30)
    qdbus("org.kde.kwin.Scripting.unloadScript", plugin)
    os.unlink(script)
    os.rmdir(os.path.dirname(script))
    if log is not None:
        log.write("==== %s %s %s %s\n%s\n" % (cfg["name"], decorations, output, "resize" if resize else "quiet", out))
        log.flush()
    samples = [m.groupdict() for m in STATS_RE.finditer(out)][warmup - 1:]
    protocol_error = re.search(r"error \d+: .*", out)
    return samples, (protocol_error.group(0) if protocol_error else None), proc.returncode


def summarize(samples):
    if not samples:
        return None
    n = len(samples)
    f = lambda k: [float(s[k]) for s in samples if s.get(k) is not None]
    mean = lambda v: sum(v) / len(v) if v else 0.0
    lat_n = sum(int(s["lat_n"]) for s in samples)
    lat_mean = (sum(float(s["lat_mean"]) * int(s["lat_n"]) for s in samples) / lat_n) if lat_n else 0.0
    return {
        "hz": mean([1000.0 / x for x in f("interval") if x > 0]),
        "refresh_hz": mean([1000.0 / x for x in f("refresh") if x > 0]),
        "presented": mean(f("presented")),
        "missed": mean(f("missed")),
        "discarded": mean(f("discarded")),
        "lost": sum(f("lost")),
        "lat_mean": lat_mean,
        "lat_max": max(f("lat_max")) if f("lat_max") else 0.0,
        "draw_max": max(f("draw_max")),
        "resizes": mean(f("resizes")) if f("resizes") else None,
        "rs_mean": mean([float(s["rs_mean"]) for s in samples if s.get("resizes") and int(s["resizes"]) > 0]),
        "rs_max": max(f("rs_max")) if f("rs_max") else None,
        "acq_max": max(f("acq_max")) if f("acq_max") else None,
        "deferred": sum(f("deferred")) if f("deferred") else None,
        "failed": sum(f("failed")) if f("failed") else None,
        "seconds": n,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/clang-release/examples/m0_window/m0_window")
    ap.add_argument("--seconds", type=int, default=6)
    ap.add_argument("--manual-resize", action="store_true")
    ap.add_argument("--with-lavapipe", action="store_true")
    ap.add_argument("--only-renderer", action="append")
    ap.add_argument("--only-output", action="append")
    ap.add_argument("--log")
    a = ap.parse_args()

    configs = [
        {"name": "shm", "renderer": "shm"},
        {"name": "gpu compositor", "renderer": "gpu", "args": ["--pacing=compositor"]},
        {"name": "gpu driver", "renderer": "gpu", "args": ["--pacing=driver"]},
    ]
    if a.with_lavapipe:
        icd = next((p for p in LAVAPIPE_ICDS if os.path.exists(p)), None)
        if icd is None:
            sys.exit("--with-lavapipe: lavapipe's ICD file not found")
        for pacing in ("compositor", "driver"):
            configs.append({"name": "lavapipe " + pacing, "renderer": "gpu",
                            "args": ["--pacing=" + pacing, "--adapter=software"],
                            "env": {"VK_DRIVER_FILES": icd}})
    if a.only_renderer:
        configs = [c for c in configs if c["name"] in a.only_renderer]
    outs = outputs()
    if a.only_output:
        outs = [o for o in outs if o[0] in a.only_output]
    if not outs:
        sys.exit("no enabled outputs found (kscreen-doctor -o)")

    log = open(a.log, "w") if a.log else None
    rows = []
    total = len(configs) * 2 * len(outs) * 2
    i = 0
    for output, hz in outs:
        for decorations in ("native", "none"):
            for cfg in configs:
                for resize in (False, True):
                    i += 1
                    label = "%s | %s | %s @ %.0f Hz | %s" % (cfg["name"], decorations, output, hz,
                                                           "resize" if resize else "quiet")
                    if resize and a.manual_resize:
                        input("\n[%d/%d] %s\n  Next run needs your hand: when the window appears on %s, "
                              "drag its bottom-right corner continuously for %d s. Press Enter to start... "
                              % (i, total, label, output, a.seconds + 3))
                    else:
                        print("[%d/%d] %s" % (i, total, label), flush=True)
                    samples, perr, code = measure(a.binary, cfg, decorations, output, resize, a.seconds,
                                                  a.manual_resize, log)
                    summary = summarize(samples)
                    if summary is not None and hz > 0 and abs(summary["refresh_hz"] - hz) > 0.05 * hz:
                        perr = (perr + "; " if perr else "") + "on the wrong output (refresh %.1f Hz)" % summary["refresh_hz"]
                    rows.append((cfg["name"], decorations, "%s %.0fHz" % (output, hz),
                                 "resize" if resize else "quiet", summary, perr, code))
                    if perr:
                        print("  PROTOCOL ERROR: " + perr, flush=True)

    hdr = ("renderer", "decor", "output", "state", "Hz", "pres/s", "miss/s", "disc/s", "lost",
           "cfg->pres ms", "resize/s", "resize ms", "acq max", "deferred", "err")
    fmt = "%-16s %-6s %-13s %-6s %6s %7s %7s %7s %5s %13s %8s %11s %8s %8s %s"
    print("\n" + fmt % hdr)
    for name, deco, out, state, s, perr, code in rows:
        if s is None:
            print(fmt % (name, deco, out, state, "-", "-", "-", "-", "-", "-", "-", "-", "-", "-",
                         perr or ("exit %d, no stats" % code)))
            continue
        print(fmt % (name, deco, out, state, "%.1f" % s["hz"], "%.1f" % s["presented"], "%.2f" % s["missed"],
                     "%.2f" % s["discarded"], "%d" % s["lost"],
                     ("%.1f / %.1f" % (s["lat_mean"], s["lat_max"])) if s["lat_max"] else "-",
                     ("%.0f" % s["resizes"]) if s["resizes"] is not None else "-",
                     ("%.2f / %.2f" % (s["rs_mean"], s["rs_max"])) if s["resizes"] else "-",
                     ("%.2f" % s["acq_max"]) if s["acq_max"] is not None else "-",
                     ("%d" % s["deferred"]) if s["deferred"] is not None else "-",
                     perr or ""))
    if log:
        log.close()


if __name__ == "__main__":
    main()
