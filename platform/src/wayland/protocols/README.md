# Vendored Wayland protocols

Exact copies of the protocol descriptions AXION's Wayland backend uses, taken unmodified from
[wayland-protocols](https://gitlab.freedesktop.org/wayland/wayland-protocols) 1.50.
`wayland-scanner` generates the client code from them at build time, so the build never depends
on which wayland-protocols version is installed.

| File | Upstream path | Why |
| --- | --- | --- |
| `xdg-shell.xml` | `stable/xdg-shell/` | Toplevel windows, configure/ack, move and resize |
| `xdg-decoration-unstable-v1.xml` | `unstable/xdg-decoration/` | Ask for server-side decorations (KDE) |
| `presentation-time.xml` | `stable/presentation-time/` | Present timestamps and refresh for the frame clock |
| `viewporter.xml` | `stable/viewporter/` | Logical destination size for buffers at any scale |
| `fractional-scale-v1.xml` | `staging/fractional-scale/` | Fractional scale factors |
| `cursor-shape-v1.xml` | `staging/cursor-shape/` | Resize cursors by name |
| `tablet-v2.xml` | `stable/tablet/` | Only because `cursor-shape-v1.xml` references `zwp_tablet_tool_v2` |

The core protocol (`wl_surface`, `wl_seat`, ... including `wl_surface.preferred_buffer_scale`) is
not vendored: its interfaces are defined inside libwayland-client, which must be 1.22 or newer.

## Licence

Every file carries its own copyright notice and the MIT (Expat) licence, which is compatible with
AXION's Apache 2.0 licence. Keep the notices intact.

## Checksums (SHA-256)

```text
bb57d91e53a79dadab7c612dab87c233393cee73673feefa7442cfbfdd9aed2f  cursor-shape-v1.xml
42945a10487cebe2f16c2f0284f263957d78325748409bfa8bcada385b65f86a  fractional-scale-v1.xml
0a9e4ee351aada90af9d165a540b86ad9d85f740ce87cf8c26490dd1ae701b5f  presentation-time.xml
ac1128b26c779cf90b9ed71182ba5e34a7262826789adae206d825a4d45908b4  tablet-v2.xml
dcb12279a03746301fe490aaed4b38a403485a925abfce2ccfceb644e104fe71  viewporter.xml
68753c4a85a28659e3aa1fe0f138329a8bf3dff0b4789f4663a2c7fedbfecf2f  xdg-decoration-unstable-v1.xml
8cd3980d0783e1f55d12901ac51533204af71487cc4dc69ee00bb7096647d9de  xdg-shell.xml
```

To update, copy the new files from a wayland-protocols release, update the version above and the
checksums, and note the change in `docs/decisions.md`.
