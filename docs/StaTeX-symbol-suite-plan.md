# StaTeX: what is left of the font work

Phases A–D are done and on `master`. What follows is the remainder, plus the
few numbers and decisions worth not re-deriving. Everything this document used
to say about the glyph-store split, the table-driven generator and the symbol
suite has been deleted, because it is in the code now — read
`tools/genfont/README.md` for how the pipeline works and
`docs/StaTeX-layering.md` for why the glyph store is its own library.

## Where things stand

| | |
|---|---|
| atlas | **1023 glyphs**, ~1.5 MB of SDF |
| symbol table | **600 names**, generated from `symbols.tsv` |
| faces | 5 (Roman, Italic, Bold, Symbol, Blackboard) |
| firmware | text 1,996,128 / data 47,808 / bss 422,080 |
| host suite | 23/24, only the known-red `test_oracle_diff` |

Flash is comfortable on the Teensy's 8 MB and about half of the STM32U5G9's
4 MB. That is what D½ is for.

---

## D½. The SDF shrink — one variable at a time

Two experiments, each its own commit with its own before/after oracle capture:

- spread `0.125 → 0.0625` em — roughly **26%**
- `--max-sdf 48 → 32` — roughly **56%**

`test_glyph_positions_agree` is the gate; the picture test shows the ink.

**This is why the parameters were held fixed through D.** Every check on the
generator rewrite was "did a glyph that already existed move?", which
`diffatlas.py --common-only` answers exactly. Change a SDF parameter and every
glyph moves by design, so that check is gone and the oracle is the only
instrument left. Never combine the two.

A third, independent saving: the SDF box is padded to `--pad 24` around the ink,
and clipping to the true ink bbox was measured at **21–35% fewer coverage
pixels** — a sampler win as well as a flash one. It needs an exact ink bbox in
`GlyphRecord`, which means appending a field and regenerating.

## E. Alphabet faces, and `\left` / `\right`

`\mathsf` is required by STX-FNT-05 and absent; corpus case `style_sf` carries
`openDefect = "MATHSF-MISSING"`. `cmss10`/`cmssi10`/`cmssbx10`, `cmtt10`,
`rsfs10` and `eufm10`/`eufb10` are **all already vendored** with matching metric
tables, so no new font is needed.

Symbols did not need new faces — all 600 live on `Face::Symbol`, keyed by
codepoint, assembled from 15 source fonts. Alphabets are the opposite case: the
*same* ASCII letter needs a different design, so each does need an enumerator.

Per face: an enumerator in `glyphstore_types.h`, a `nameIs("mathsf")` line
beside `statex_parser.cpp:279-282`, a `FACES` entry in `genfont.py`, and a
serializer marker — `statex_serialize.cpp` switches over `Face` with no
`default:`, so `-Wswitch` will demand that one. The other ~15 `Face` sites will
not tell you.

`\left` / `\right` do not exist, and there is no path from a named symbol to a
grown delimiter: only matrix fences (hardcoded `Face::Roman`,
`statex_layout.cpp:731-753`) and the radical grow at all. The 28 `del()`
entries in `tex_symbols.res.cpp` are the input. Two symbols are already waiting
on this — `\lmoustache` and `\rmoustache` have no single-glyph design and are
excluded until there is a stretchy path to put them in.

Extensible recipes can have a **middle** piece (`kPieceMiddle`); four symbols in
the suite use one, and `makeExtensibleDelimiter` does not handle it yet — it
stacks top, repeats, bottom only.

## F. `lv_font_statex`

`backends/lvgl_font/`, linking **`lib/glyphstore` only** — which is now a real
constraint rather than an aspiration: `libglyphstore.a` has zero undefined
symbols, not even libc.

Map `GlyphRecord` → `lv_font_glyph_dsc_t` (`adv_w`, `box_w`, `box_h`, `ofs_x`,
`ofs_y`), and `get_glyph_bitmap` → the SDF sampler into an A8 `lv_draw_buf`.
Kerning returns 0 (measured: worth 0.031 em on one corpus case).

**Spike `get_glyph_dsc` against the current atlas before D½**, purely to find
out whether `GlyphRecord` is missing a field. If it is, that field can be
appended in the same regeneration as the ink bbox instead of forcing a second
one.

Be honest about the boundary: `lv_font_t` carries no italic correction, size
variants, extensible assemblies or math constants. It is a *text* adapter.
StaTeX's layout keeps reading the glyph store directly. Two consumers, one
atlas — that is what makes labels and formulas share a typeface.

Note for the LVGL side: 147 symbols have private-use codepoints, so a label
cannot address those by character. `symbols.tsv` records unicode-math's value
wherever it differs, so a future switch is a one-column edit.

---

## Decisions taken (do not relitigate)

| question | answer |
|---|---|
| symbol scope | all of MicroTeX's, minus 16 accents and 2 assembly-only delimiters → **600** |
| accents | excluded until there is accent layout; an accent as an ordinary atom draws a hat on the baseline |
| name → codepoint | MicroTeX's own tables first (410), `unicode-math-table.tex` for gaps (42), private use for the rest (147) |
| source fonts | vendored CM/AMS in `tests/oracle/fonts` — **not** LM Math. Shared source with the oracle is what makes the differential meaningful |
| SDF shrink | its own change, after the suite settled |
| sortedness check | the generator guarantees it and a runtime sweep verifies it; it cannot be a `static_assert` now the table is in flash |

## Verification

- `cmake --build build && ctest --test-dir build` → **23/24**. Anything beyond
  `test_oracle_diff` is a regression.
- **After any regeneration**:
  `python tools/genfont/diffatlas.py OLD.cpp NEW.cpp --common-only`. `cmp` is
  the wrong tool — see `tools/genfont/README.md`.
- **Oracle**: run `build/tests/oracle/bin/test_oracle_diff.exe` to a file before
  and after and diff it. Metric drift in the 4th decimal is noise; a changed
  pass/fail count is not.
- **Firmware**: `cmake --build build-teensy41`, then `--target flash`.
- **Serial**: open COM4 in the *foreground* and trigger `teensy_restart.exe`
  from a background job — background readers failed repeatedly.

## Traps that have already cost time

- **Shell heredocs mangle backslashes**, including quoted ones. `U"\\frac"`
  arrives as `U"\frac"` and C++ reads `\f` as a formfeed; Python string
  escapes collapse the same way. Use the editing tools for anything containing
  a backslash. This bit three more times during this work.
- **The vendored TTFs are addressed by raw slot**, `chr(slot)` — not by
  character. Pillow must be opened with the **BASIC** layout engine: with RAQM,
  HarfBuzz applies Unicode semantics to a font position, and cmex10 slot 173 is
  `chr(173)` = U+00AD SOFT HYPHEN, which it drops as invisible formatting.
- **`.rodata` → DTCM on Teensy.** Any new large const table needs
  `STATEX_FLASH`. String literals cannot be moved this way.
- **A8 compositing must use `max`** — glyph boxes overlap.
- **LVGL's image cache is off on purpose** (`LV_CACHE_DEF_SIZE 0`), which is what
  makes "mutate the A8 buffer, then `lv_obj_invalidate()`" correct.
- **`lv_area_t` bounds are inclusive**: `w = x2 - x1 + 1`.
- **`LV_BUILD_CONF_PATH`, not `LV_CONF_PATH`**, in v9.5 CMake.
- **The Teensy appears as an HID bootloader device**, not a COM port, when it is
  waiting to be programmed. `teensy_ports.exe -L` sees it.
- **`Arena::reset()` preserves the high-water mark**, so `stats->highWater` is
  cumulative since construction, not per-call.
- **The mingw compiler does not run from the Bash tool** in this environment —
  it exits 1 with no diagnostic, even on a hello-world, because of DLL
  resolution. Build from PowerShell.

## Loose ends

- `backends/ili9488/ili9488_graphics.h:39` truncates a rule's origin while
  rounding its size, so device and host disagree by up to a pixel. Its own
  change, with its own before/after oracle capture.
- No `requirements.txt` for the generator (Pillow, numpy, scipy).
- No perf guard in the harness.
- Remaining entries in `docs/StaTeX-known-standins.md`: kerning, `showcase`
  assembling one delimiter tile fewer than TeX, `\atop`/`\over`, `\int` above
  ~96 px em.
