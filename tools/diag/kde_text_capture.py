#!/usr/bin/env python3
# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
"""Captures how KDE draws text, for tuning AXION's text coverage correction (decision 101).

A Qt Quick window (qml6, a Qt Quick Controls TextArea: KDE's desktop style, the text path of
Plasma and Kirigami apps, in the desktop's general font) shows a fixed sample and captures itself
(grabToImage): only that window, at the device pixel ratio of the monitor it is on. Spectacle's
window capture is not used because it saves in logical pixels, which hides a fractional scale.
At scale 1 this path and Qt Widgets (kdialog) give the same ink within 0.3 %.

For each output and both polarities:
  - dark: the desktop's color scheme as it is (light text on dark with Breeze Dark);
  - light: Breeze Light for that process only, through a temporary XDG_CONFIG_HOME holding
    BreezeLight as kdeglobals and a copy of the user's fontconfig and font settings; the desktop's
    own scheme is never touched.
A KWin script moves the window to the output (new windows open on the monitor under the cursor),
and the window logs the scale it was captured at, which the script checks.

    tools/diag/kde_text_capture.py OUT_DIR [--outputs HDMI-A-1,DP-2]

writes OUT_DIR/<scheme>_<output>.png (the TextArea alone). Then, with the crop of the three lines
(4,4,300,62 at scale 1; 5,5,400,83 at 1.325 for the default window):

    build/diag/tools/diag/text_weight --capture OUT_DIR/dark_DP-2.png --crop 5,5,400,83 --scale 1.325

The captures are the user's screen content: keep them out of the repository. Do not touch the
desktop while it runs (about 4 s per capture). Needs qml6, qdbus6 and journalctl.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

GRAB_QML = """import QtQuick
import QtQuick.Controls
ApplicationWindow {
    id: win
    title: "AXION text reference"
    width: 620; height: 240; visible: true
    TextArea {
        id: area
        anchors.fill: parent; anchors.margins: 12
        readOnly: true
        text: "The quick brown fox jumps over the lazy dog\\nSphinx of black quartz, judge my vow\\n0123456789 office AV To Wa"
    }
    Timer {
        interval: 3000; running: true
        onTriggered: area.grabToImage(function(r) {
            r.saveToFile(Qt.application.arguments[Qt.application.arguments.length - 1]);
            console.warn("AXGRAB dpr " + win.devicePixelRatio + " screen " + win.screen.name);
            Qt.quit();
        })
    }
}
"""

# KWin rejects a script whose file name it considers mismatched: each run gets a directory with a
# main.qml and a plugin name of its own.
PLACE_QML = """import QtQuick
import org.kde.kwin
Item {
    Timer { interval: 50; repeat: true; running: true
        onTriggered: {
            const list = Workspace.stackingOrder;
            for (let i = 0; i < list.length; ++i) {
                const w = list[i];
                if (w.pid != %(pid)d || w.caption != "AXION text reference") continue;
                let o = null;
                for (let k = 0; k < Workspace.screens.length; ++k)
                    if (Workspace.screens[k].name == "%(output)s") o = Workspace.screens[k];
                if (o == null || w.output == o) return;
                Workspace.sendClientToScreen(w, o);
                w.frameGeometry = Qt.rect(o.geometry.x + 120, o.geometry.y + 120, 620, 240);
                Workspace.activeWindow = w;
            }
        }
    }
}
"""

SCALES = {"HDMI-A-1": "1", "DP-2": "1.325"}


def qdbus(*args):
    subprocess.run(["qdbus6", "org.kde.KWin", "/Scripting"] + list(args), capture_output=True)


def light_config(tmp):
    home = os.path.expanduser("~/.config")
    cfg = os.path.join(tmp, "lightcfg")
    os.makedirs(cfg)
    shutil.copy("/usr/share/color-schemes/BreezeLight.colors", os.path.join(cfg, "kdeglobals"))
    with open(os.path.join(cfg, "kdeglobals"), "a") as f:
        f.write("\n[General]\nColorScheme=BreezeLight\n")
    for name in ("fontconfig", "kcmfonts"):
        src = os.path.join(home, name)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(cfg, name))
        elif os.path.isfile(src):
            shutil.copy(src, os.path.join(cfg, name))
    return cfg


def capture(tmp, out, scheme, output, cfg, n):
    env = dict(os.environ)
    if scheme == "light":
        env["XDG_CONFIG_HOME"] = cfg
    png = os.path.join(out, "%s_%s.png" % (scheme, output))
    grab = os.path.join(tmp, "grab.qml")
    start = time.time()
    proc = subprocess.Popen(["qml6", grab, "--", png], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    run_dir = os.path.join(tmp, "kwin%d" % n)
    os.makedirs(run_dir)
    with open(os.path.join(run_dir, "main.qml"), "w") as f:
        f.write(PLACE_QML % {"pid": proc.pid, "output": output})
    plugin = "axion_text_capture_%d_%d" % (os.getpid(), n)
    qdbus("org.kde.kwin.Scripting.loadDeclarativeScript", os.path.join(run_dir, "main.qml"), plugin)
    qdbus("org.kde.kwin.Scripting.start")
    try:
        proc.wait(timeout=15)
    finally:
        qdbus("org.kde.kwin.Scripting.unloadScript", plugin)
    log = subprocess.run(["journalctl", "--user", "-b", "--since", "@%d" % int(start),
                          "_PID=%d" % proc.pid], capture_output=True, text=True).stdout
    line = [l for l in log.splitlines() if "AXGRAB" in l]
    got = line[-1].split("AXGRAB", 1)[1].strip() if line else "(no log)"
    ok = ("dpr %s screen %s" % (SCALES.get(output, "?"), output)) in got
    print("%s %s: %s%s" % (png, scheme, got, "" if ok else "  <- NOT on the requested output"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--outputs", default="HDMI-A-1,DP-2")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    print("KDE general font:", subprocess.run(
        ["kreadconfig6", "--group", "General", "--key", "font"], capture_output=True,
        text=True).stdout.strip() or "(default: Noto Sans,10)")
    print("fontconfig:", subprocess.run(
        ["fc-match", "-f", "%{family} %{style} hintstyle=%{hintstyle} rgba=%{rgba}", "sans-serif"],
        capture_output=True, text=True).stdout)
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "grab.qml"), "w") as f:
            f.write(GRAB_QML)
        cfg = light_config(tmp)
        n = 0
        for output in args.outputs.split(","):
            for scheme in ("dark", "light"):
                n += 1
                ok = capture(tmp, args.out, scheme, output, cfg, n) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
