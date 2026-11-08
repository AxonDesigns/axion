# Bundled fonts

The font set compiled into `ax_text` (decision 98): headless hosts, tests, goldens and the web use
it, and every other platform falls back to it.

| File | Source | SHA-256 |
| --- | --- | --- |
| `NotoSans[wdth,wght].ttf` | Noto Sans v2.015, `unhinted/variable-ttf` | `264d2782edd6a0799a5184109bcd345532f5b0abb66d9ec43eb4556b5fb1d50a` |
| `NotoSans-Italic[wdth,wght].ttf` | Noto Sans v2.015, `unhinted/variable-ttf` | `3a6f5eacec7587e5f71e315c7dd1be0fa992aece02adca4a1e433458a70069f4` |

- **License:** SIL Open Font License 1.1, in `OFL.txt` (copied from the release).
- **Release:** [NotoSans-v2.015](https://github.com/notofonts/latin-greek-cyrillic/releases/tag/NotoSans-v2.015),
  archive SHA-256 `0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5`.
- **Cut down to Latin** by `tools/fonts/subset_noto.py` with fontTools 4.68.0: Basic Latin,
  Latin-1, Latin Extended-A, combining diacritical marks, general punctuation, currency signs and a
  few symbols. Both axes (width 62.5–100, weight 100–900) and every OpenType layout feature of the
  characters kept stay (GSUB, GPOS, GDEF). Features whose lookups only touch removed characters go
  with them: `rtlm`, and the stylistic sets for Greek, Cyrillic and `ƒ` (`ss03`, `ss06`, `ss07`).
  `text.fonts.the_versioned_fonts_keep_the_features_the_shaping_tests_use` checks `liga`, `kern`,
  `tnum`, `smcp`, `zero` and `frac` on these files.
- **Rebuild:** `python3 -I tools/fonts/subset_noto.py` gives the same bytes (it checks the archive
  and the fontTools version).
