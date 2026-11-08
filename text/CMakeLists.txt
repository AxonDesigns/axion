# Copyright 2026 Axon Works
# SPDX-License-Identifier: Apache-2.0
#
# text: fonts, itemization, shaping, line layout and glyph rasterization (the document's "Text
# rendering"). Depends only on base, its three libraries built from source
# (cmake/AxionTextDeps.cmake) and the operating system's font APIs (fontconfig on Linux), never on
# the platform module.

include(AxionTextDeps)
ax_add_text_deps()
