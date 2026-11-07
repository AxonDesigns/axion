# Development diagnostics

Tools for measuring and debugging on a developer's machine. None of them is built by default,
linked into an AXION library or example, or shipped.

| File | What it is |
| --- | --- |
| `wl_read_trace.c` | LD_PRELOAD library counting libwayland read and dispatch calls per thread, to see which threads read the display fd (decision 49). Built with `-DAX_BUILD_DIAG_TOOLS=ON`. |
| `measure_m0.py` | Runs `examples/m0_window` across renderer, pacing, decorations, monitor and quiet/resize on KDE Plasma, and prints a summary table (decision 49). |
| `kwin_place_m0.qml.in` | KWin declarative script template `measure_m0.py` uses to move the window to a monitor and resize it about 120 times per second. |

```sh
cmake -S . -B build/diag -G Ninja -DCMAKE_BUILD_TYPE=Release -DAX_BUILD_DIAG_TOOLS=ON
cmake --build build/diag
LD_PRELOAD=$PWD/build/diag/tools/diag/libax_wl_read_trace.so \
    build/diag/examples/m0_window/m0_window --renderer=gpu --duration=3

tools/diag/measure_m0.py --binary build/clang-release/examples/m0_window/m0_window --with-lavapipe
```

`measure_m0.py` needs `qdbus6` and `kscreen-doctor`. Do not touch the windows while it runs: a
manual resize counts as part of a "quiet" run. Use `--manual-resize` to drag the window yourself
instead of the script; it announces each run that needs your hand.
