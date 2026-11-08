Tooling: axion CLI, IDL code generator, plugin scaffolding, asset pipeline and devtools (later).

What exists today:

- `ci/`: scripts CI runs.
  - `check_final_newline.sh`, `check_decision_status.py` and `check_examples.py` (every example
    defines its executable) are the repository checks.
  - `with-weston.sh` runs a command against a headless weston for the Wayland tests.
  - `compare_images.py` and `compare_ppm.py` compare images across platforms (decision 86) and
    WebGPU implementations (decision 56).
  - `emsdk-version` pins the Emscripten version.
- `update_goldens.sh`: rewrites the golden images on purpose (decision 82).
- `yoga/port_fixtures.py`: ports Yoga's conformance fixtures to `core.layout_yoga` (decision 91).
- `diag/`: measuring and debugging tools for a developer's machine, never built by default (see its
  README).
- `sanitizers/`: suppressions for GPU drivers' leaks.
