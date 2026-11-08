# Development diagnostics

Tools for measuring and debugging on a developer's machine. None of them is built by default,
linked into an AXION library or example, or shipped.

| File | What it is |
| --- | --- |
| `wl_read_trace.c` | LD_PRELOAD library counting libwayland read and dispatch calls per thread, to see which threads read the display fd (decision 49). Built with `-DAX_BUILD_DIAG_TOOLS=ON`. |
| `measure_m0.py` | Runs `examples/m0_window` across renderer, pacing, decorations, monitor and quiet/resize on KDE Plasma, and prints a summary table (decision 49). |
| `roping_sim.py` | Simulates the roping of thin rotated lines for box and tent filters blended in sRGB space or linear light, and prints the table of decision 58. Needs numpy. |
| `kwin_place_m0.qml.in` | KWin declarative script template `measure_m0.py` uses to move the window to a monitor and resize it about 120 times per second. |
| `kde_text_capture.py` | Captures KDE's text (a Qt Quick TextArea in the desktop's general font that grabs itself at its monitor's scale, moved to each output by KWin, in Breeze Dark and Breeze Light for that process only), for tuning the text coverage correction (decision 101). The captures stay out of the repository. |
| `measure_m1_handshake.py` | Runs `examples/m1_nodes` on each output at rest and while KWin resizes it, and prints presented, missed, scene-late and forced acknowledgements per second (decision 103). |
| `text_weight.c` | Compares the ink of that capture's text with AXION's rendering of the same lines, for a grid of corrections (`ax_ir_text_lut`), and names the closest. Built with `-DAX_BUILD_DIAG_TOOLS=ON`. |

```sh
cmake -S . -B build/diag -G Ninja -DCMAKE_BUILD_TYPE=Release -DAX_BUILD_DIAG_TOOLS=ON
cmake --build build/diag
LD_PRELOAD=$PWD/build/diag/tools/diag/libax_wl_read_trace.so \
    build/diag/examples/m0_window/m0_window --renderer=ir --duration=3

tools/diag/measure_m0.py --binary build/clang-release/examples/m0_window/m0_window --with-lavapipe
```

```sh
tools/diag/kde_text_capture.py /tmp/kde          # do not touch the desktop (about 16 s)
build/diag/tools/diag/text_weight --capture /tmp/kde/dark_DP-2.png --crop 5,5,400,83 --scale 1.325
tools/diag/measure_m1_handshake.py --binary build/clang-release/examples/m1_nodes/m1_nodes
```

The crop holds the sample's three lines (4,4,300,62 at scale 1).

`measure_m0.py` needs `qdbus6` and `kscreen-doctor`. Do not touch the windows while it runs: a
manual resize counts as part of a "quiet" run. Use `--manual-resize` to drag the window yourself
instead of the script; it announces each run that needs your hand.
