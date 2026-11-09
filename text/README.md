Text: fonts, itemization, shaping, line layout and glyph rasterization (the document's "Text
rendering"; decisions 97 to 101). Depends only on base, its libraries (HarfBuzz, FreeType and
libunibreak, built from source by `cmake/AxionTextDeps.cmake`) and the system's font APIs
(fontconfig on Linux), never on the platform module.

- `include/axion/text/ax_text.h`: the text system (fonts, the shaping and glyph caches),
  paragraphs, line layouts and glyphs.
- `include/axion/text/ax_font_source.h`: where fonts come from: the bundled set, fontconfig.
- `fonts/`: the bundled Noto Sans (see its README), compiled into the library.
- `third_party/freetype/`: FreeType's module list and options for AXION.

Still to come (M3): bidi, font fallback per script, emoji, editing and IME.
