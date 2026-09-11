# embedtex: symbol suite, glyph-store split, and the LVGL font

## Context

StaTeX now renders through LVGL on the Teensy — a scrolling column of formulas
as A8 coverage images. What it renders is a **14-symbol** subset of LaTeX,
which is the blocker for everything downstream: a CAS emits `\ln`, `\sin`,
`\pm`, `\partial`, `\rightarrow` constantly, and a grapher wants `√`, `∞` and
Greek beyond the current set.

Two structural things want doing at the same time, because all three touch the
same files and regenerating the atlas is the disruptive step:

1. **Split the glyph store out of `lib/StaTeX`.** The font (atlas, records,
   `FontParams`, SDF sampler) is already dependency-clean — it includes only
   `statex_types.h`, and the only "upward" reference is a *comment* at
   `statex_fontparams.h:17`. Making that structural means the font is swappable
   and the LVGL font adapter can link it without dragging in the parser and
   layout.
2. **Grow the symbol suite**, combined with the box-tightening experiment
   (worth ~26% flash *and* 21–35% of the sampler's pixels).

Then the `lv_font_t` adapter, built against the final shape.

---

## STATE AS OF WRITING — read this first

**Last commit is `7d2d1ee`** ("Split the LVGL plan into architecture and
implementation"). Everything below is **uncommitted in the working tree**:

- Phase 1: `glyphCoverageSize()` in `statex_glyphstore.h`; `Renderer::measure()`
  + `buildLayout()` in `statex_render.{h,cpp}`; 4 tests in
  `tests/unit/test_render/test_render.cpp`.
- Phase 2: `backends/statex_a8/` + `tests/behaviour/test_a8_backend.cpp`.
- Phase 3: `vendor/lvgl/` (v9.5.0 @ `85aa60d18b3d5e5588d7b247abf90198f07c8a63`,
  pruned 267 MB → 26 MB), `tools/make_lv_conf.py`, `targets/teensy41/lv_conf.h`.
- Phase 4: `Ili9488::blitRgb888`, `backends/lvgl_ili9488/`.
- Phase 5a: `targets/teensy41/src/main.cpp` rewritten for LVGL.

**Verified**: host suite 23/24 (only the known-red `test_oracle_diff`);
firmware `text 774496 / data 41664 / bss 422080`; flashed and confirmed working
on hardware (scrolling column visible).

**Commit this before touching genfont.**

### Measured numbers worth not re-deriving

| thing | value |
|---|---|
| `measure()` vs `render()` | **flat ~1.8 µs** vs 21.6 µs @em18, 233.8 µs @em64 |
| atlas today | 248 records, **413 KiB**, mean **1706 B**/glyph, max 2304 (48×48) |
| record table | 32 B × 248 = 7.8 KiB |
| LVGL cost | +285 KB flash, **+27.6 KB DTCM** (`.rodata` → DTCM on this target) |
| LVGL heap actually used | `max_used=4244` of 24 KB pool |
| StaTeX arena high-water | 42,112 of 98,304 B |
| flash projection | ~600 glyphs ≈ **1.0 MB**; ~1400 ≈ **2.4 MB** |
| budget | Teensy 8 MB (fine); **STM32U5G9 4 MB (the constraint)** |

---

## Key discovery: the symbol suite is already in the repo

`tests/oracle/fonts/` vendors the **complete TeX math font set** — and genfont
already reads from that exact directory (`genfont.py:31-32`), which is also
what the oracle rasterises. That shared source is the "closed by construction"
property `known-standins.md` describes; **do not break it** by introducing a
different source font.

Available: `cmex10` `cmmi10` `cmmib10` `cmsy10` `cmbsy10` `msam10` `msbm10`
(AMS A+B) `stmary10` `rsfs10` (calligraphic) `eufm10`/`eufb10` (Fraktur)
`dsrom10` `cmtt10` `cmss10`/`cmssi10`/`cmssbx10` (sans — closes the `\mathsf`
gap in STX-FNT-05) `cmti10` `cmbxti10`.

And MicroTeX's own tables are machine-readable and already vendored:

| data | where | count |
|---|---|---|
| name → (fontId, slot) | `lib/MicroTeX/res/sym/*.def.cpp`, `E(font, slot, name)` | **620** |
| name → AtomType | `lib/MicroTeX/res/builtin/tex_symbols.res.cpp`, `sym()/del()` | **619** |
| both | | **618** |
| per-slot metrics | `lib/MicroTeX/res/font/*.def.cpp` `METRICS_START` | all fonts |
| **size variant chains** | same files, `LARGERS_START` (`slot, larger, fontId`) | cmex10 has 78 |
| **extensible recipes** | same files, `EXTENSIONS_START` (`slot, top, mid, rep, bot`) | cmex10 |
| font params | same files, `xHeight() quad() skew() bold()` | per font |

AtomType breakdown of the 619: ord 175, rel 257, bin 112, op 24, open 17,
close 16, punct 11, acc 17.

**`tfm.py` currently parses only `METRICS_START…METRICS_END`** (`tfm.py:75-87`).
LARGERS and EXTENSIONS are *hand-transcribed* into genfont's `VARIANTS` dict
(59 entries, `genfont.py:146-211`). That hand-transcription is the single
biggest manual surface and the thing to automate.

### The one gap: name → Unicode

StaTeX keys glyphs by `(Face, c32 codepoint)`; MicroTeX keys by (font, slot).
Measured resolution rates for the 620 names:

- Latin Modern Math glyph names: **78**
- STIX Math glyph names: 35 (union with LM: still 78)
- Python `unicodedata` heuristics: 22
- **union: 88 → 532 names need a real mapping**

**Solution**: CTAN's `unicode-math` package ships `unicode-math-table.tex`,
~2,500 lines of `\UnicodeMathSymbol{"0221A}{\sqrt}{\mathop}{radical}` — exactly
name → codepoint → math class. Not currently installed, but
`C:\Users\louis\AppData\Local\Programs\MiKTeX\miktex\bin\x64\mpm.exe` is
present, so `mpm --install=unicode-math` fetches it.

Do the join **once**, check the result in as `tools/genfont/symbols.tsv`
(name, codepoint, AtomType, tfm font, slot, face), and have genfont read that.
Then generation needs no MiKTeX and stays reproducible — the same reasoning
that removed the old Latin Modern dependency.

---

## Phases

### A. Commit the LVGL work *(do first, it is a clean base)*

Suggested split: one commit for phases 1–2 (`measure()` + A8 backend, both
host-verified), one for 3–5 (LVGL vendored, port, screen).

### B. Split out `lib/glyphstore`

Move `statex_glyphstore.h`, `statex_glyphs.gen.cpp`, `statex_sdf.{h,cpp}`,
`statex_fontparams.h` → `lib/glyphstore/`. `lib/StaTeX` depends on it.

**The only non-mechanical part** is `statex_types.h`. It splits:
- to the font: the integer aliases, `c32`, `enum class Face`
- stays with StaTeX: `defaultMathFace()`, `mathModeGlyph()`,
  `isMathModeSymbol()` — those are TeX *semantics* ("letters are italic in math
  mode", "`-` is U+2212 not a hyphen"), plus `Handle`, `NO_NODE`, `Span`.

Update: root `CMakeLists.txt`, `targets/teensy41/CMakeLists.txt`,
`tests/CMakeLists.txt`, genfont's `--out` default, and `docs/StaTeX-layering.md`.

### C. Automate the metric tables *(before regenerating)*

1. Extend `tfm.py` to parse `LARGERS_START` and `EXTENSIONS_START` alongside
   `METRICS`. Deletes most of genfont's 59-entry hand-written `VARIANTS`.
2. Build `tools/genfont/symbols.tsv` by joining: `res/sym/*.def.cpp` (slot) ×
   `tex_symbols.res.cpp` (AtomType) × `unicode-math-table.tex` (codepoint).
   Check it in; note unresolved names explicitly rather than dropping them.
3. Drive `FACES`/`TFM_SLOT` from that table instead of Python literals.

### C½. Constraints that only bite at scale — decide before regenerating

- **`u8 variant` currently gives 0–7**, because `kFirstPieceVariant = 8`
  partitions the space (`statex_glyphstore.h:100-110`) — so a glyph needing
  more size steps, or a **middle** recipe piece, collides with the piece range.
  **Cheap fix: raise `kFirstPieceVariant` to 64** and put pieces at 64/65/66.
  The field is already `u8` (0–255) and the threshold is arbitrary — it only
  has to exclude pieces from the "smallest that fits" walk. One line in
  `statex_glyphstore.h` and one in `genfont.py:143-144`, no record-format
  change, no re-layout. Leaves room for a `PIECE_MID`, which no bracket needs
  today but taller delimiters do.
- **`statex_symbols.cpp`'s `tableSorted` is a `constexpr` loop** over
  `sum(len(name))`. Fine at 14; at ~620 it will likely exceed GCC's
  `-fconstexpr-ops-limit` and MSVC's `/constexpr:steps`, and nothing currently
  raises them. Prefer moving the ordering check to a **runtime assertion in a
  unit test** over raising compiler limits: the table is generated, so the
  check belongs where the generator's output is validated, not in every
  translation unit that includes it.
- **`tests/unit/test_symbols/test_symbols.cpp:31-35` pins `alpha` as the first
  entry and `times` as the last.** Both break the moment the table grows.
- **`statex_serialize.cpp:68-76` switches over `Face` with no `default:`** — a
  new enumerator is a `-Wswitch` warning, and an error under `STATEX_WERROR`.
  That is the one site that forces itself on you; the other ~15 `Face` sites do
  not.
- **No build-time link between the symbol table and the atlas.** A name whose
  codepoint is absent parses fine and refuses later in *layout* with
  `MissingGlyph` (`statex_layout.cpp:52-58`) — a different stage and a
  different error from `UnknownCommand`. Worth a generated cross-check.
- **`kRowTmp = 256` caps a row at 128 atoms** (`statex_layout.cpp:28,85`), and
  `AtomType type[256]` + `Handle tmp[256]` sit on the C++ stack.
- **Spacing columns that nothing exercises yet**: `Opening`, `Closing`,
  `Punctuation` are reached only via ASCII `( ) [ ] , ;`, and **`Inner` is
  produced by nothing at all**. Named delimiters and `\ldots` would be their
  first users, so those rows of `kInterAtomGlue` are untested today.

### D. Regenerate — full suite, today's SDF parameters

**Decided: all ~620 MicroTeX symbols, and the SDF-shrink experiment is a
separate change afterwards.** One variable at a time: this pass changes *which*
glyphs exist, not *how* they are rendered.

- **Source font**: the vendored CM/AMS set. *Not* LM Math — it would break the
  shared-source property with the oracle, which is CM.
- **SDF parameters unchanged**: `--render 256 --max-sdf 48 --pad 24
  --spread-em 0.125`. So existing glyphs must come out **byte-identical** —
  that is the strongest possible regression check on the genfont rewrite, and
  it is only available because the parameters are held fixed.
- **Record fields**: `GlyphRecord` is emitted **positionally**
  (`genfont.py:511-524`) with **no** `static_assert` canary (unlike
  `FontParams`, which has one at `statex_fontparams.h:105-108`). **Append
  only**, and add the canary while you are in there. Candidate addition now, so
  the later shrink does not need a second regeneration: an **exact ink bbox**,
  which the ink-clipping win needs.

Expect roughly **1.0 MB** of atlas at these parameters — fine on the Teensy's
8 MB, and the later SDF shrink should take it to ~330 KB before the U5's 4 MB
matters.

### D½. The SDF shrink — its own change, after

Two experiments, each with its own before/after oracle capture:
spread 0.125 → 0.0625 em (**~26%**), and `max-sdf` 48 → 32 (**~56%**).
`test_glyph_positions_agree` is the gate; the picture test shows the ink.
Cheaper to judge on a corpus the oracle can score fully, which it can only do
once the symbol suite has settled.

### E. Grow the symbol set

Faces grow from 5 to ~9 (Sans, Typewriter, Calligraphic, Fraktur). `Face` is
`u8`, so there is room. **`cmss10`/`cmssi10`/`cmssbx10`, `cmtt10`, `rsfs10` and
`eufm10`/`eufb10` are all already vendored** in `tests/oracle/fonts/` with
matching MicroTeX metric defs — so `\mathsf` (STX-FNT-05, corpus case
`style_sf`, `openDefect = "MATHSF-MISSING"`) closes with no new font.

Adding a face means: the enumerator, a `nameIs("mathsf")` line at
`statex_parser.cpp:279-282`, a genfont `FACES` entry, and a serializer marker
(which the `-Wswitch` above will demand).

Parser and `statex_symbols.cpp` are **two tables that must agree**, and the
symbol table must stay **strictly sorted by name**.

**`\left` / `\right` do not exist**, and there is no path from a named symbol to
a grown delimiter — only matrix fences (`[ ] ( )`, hardcoded `Face::Roman` at
`statex_layout.cpp:731-753`) and the radical grow at all. A comprehensive
delimiter set needs that path built, not just glyphs added.

### F. `lv_font_statex`, built against the final atlas

`backends/lvgl_font/`, linking `lib/glyphstore` **only** — not the parser or
layout. Map `GlyphRecord` → `lv_font_glyph_dsc_t` (`adv_w`, `box_w`, `box_h`,
`ofs_x`, `ofs_y`), and `get_glyph_bitmap` → the SDF sampler into an A8
`lv_draw_buf`. Kerning returns 0 (measured, deliberate — worth 0.031 em on one
corpus case).

**Do a throwaway spike of `get_glyph_dsc` against today's atlas before phase D**
— an hour, purely to discover whether the record is missing a field. Cheaper
than finding out after the regeneration.

Note honestly: `lv_font_t` carries **no** italic correction, size variants,
extensible assemblies or math constants. It is a *text* adapter. StaTeX's
layout keeps reading the glyph store directly. Two consumers, one atlas — that
is what makes labels and formulas share a typeface.

---

## Decisions taken (do not relitigate)

| question | answer |
|---|---|
| symbol scope, first pass | **all ~620** MicroTeX names (618 with both slot and AtomType) |
| SDF shrink | **separate change, after** — one variable at a time |
| source font | vendored CM/AMS in `tests/oracle/fonts/`; **not** LM Math |
| name → Unicode | `unicode-math-table.tex` via `mpm --install=unicode-math`, joined **once** into a checked-in `tools/genfont/symbols.tsv` |
| `kFirstPieceVariant` | raise 8 → 64; no record-format change |
| sorted-table check | move from `constexpr static_assert` to a unit test |

## Verification

- `cmake --build build && ctest --test-dir build` → **23/24**, only
  `test_oracle_diff` (labelled `red`) failing. Any other failure is a
  regression.
- **Atlas changes**: dump coverage for every glyph at em 8..96 before and after
  and compare hashes. The scratch harness `dump.cpp` does this; a byte-identical
  hash proves a refactor moved no pixels.
- **The genfont rewrite specifically**: because phase D holds the SDF
  parameters fixed, the **248 existing records must regenerate byte-identical**.
  Diff the old and new `statex_glyphs.gen.cpp` filtered to those records — any
  change means the table-driven rewrite altered a slot, a face or a metric.
  This is the single most valuable check in the whole plan and it evaporates if
  the SDF parameters change in the same pass.
- **Oracle before/after**: run `build/tests/oracle/bin/test_oracle_diff.exe`
  into a file before and after, and diff the aggregate verdict lines
  ("N cases diverge", "worst offset ... em"). Metric drift in the 4th decimal is
  noise; a changed pass/fail count is not.
- **Firmware**: `cmake --build build-teensy41`, then
  `cmake --build build-teensy41 --target flash`.
- **Serial**: the board prints refusals and clipping only. Read it by opening
  COM4 in the *foreground* and triggering `teensy_restart.exe` from a background
  job — the background-reader approach failed repeatedly.

---

## Traps (each one cost time already)

- **Shell heredocs mangle TeX literals.** Writing `U"\\frac"` through a bash
  heredoc produces `U"\frac"`, which C++ reads as a formfeed. Use the Write/Edit
  tools for anything containing backslashes.
- **`GlyphRecord` is positional and has no canary.** Append only.
- **genfont hard-fails if ink touches the canvas border** (`genfont.py:328-332`).
  Bigger glyphs may need `--render` raised.
- **`.rodata` → DTCM on Teensy.** `imxrt1062_t41_xip.ld` routes it into
  `.data > DTCM`. `LV_ATTRIBUTE_LARGE_CONST → .progmem` is already set in
  `tools/make_lv_conf.py`; the same applies to any new large const table.
- **A8 compositing must use `max`**, not overwrite or add — glyph boxes overlap.
- **LVGL's image cache is off on purpose** (`LV_CACHE_DEF_SIZE 0`). That is what
  makes "mutate the A8 buffer, then `lv_obj_invalidate()`" correct. Turn it on
  and you must `lv_image_cache_drop()` after every change.
- **`lv_area_t` bounds are inclusive**: `w = x2 - x1 + 1`.
- **`LV_BUILD_CONF_PATH`, not `LV_CONF_PATH`**, in v9.5 CMake. The old spelling
  is silently ignored and you build against every default.
- **The Teensy appears as an HID bootloader device**, not a COM port, when
  waiting to be programmed. `teensy_ports.exe -L` sees it; `Get-PnpDevice`
  filtering and `pio device list` do not.
- **`Arena::reset()` preserves the high-water mark** by design, so
  `stats->highWater` is cumulative since construction, not per-call.

## Small corrections to make while in these files

- `statex_glyphstore.h:138-142` says extensible assembly is something "StaTeX
  does not do yet". **It does** — `Layout::makeExtensibleDelimiter`,
  `statex_layout.cpp:814-882`. Stale comment.
- `findGlyphRecord` (inside the genfont heredoc, emitted at
  `statex_glyphs.gen.cpp:17911`) tests `g.variant < 0` on a `u8` — always false.
  Harmless because variant 0 sorts first, but it is dead code in a search
  predicate and produces a `-Wtype-limits` warning.
- Three other warnings block turning on `STATEX_WERROR`:
  `statex_fontparams.h:11` (`/*` inside a comment), `statex_layout.cpp:899`
  (unused `const FontParams& p`), `statex_record.h`-derived sign conversion.
- `tools/genfont/README.md` is substantially stale — it documents
  `C:\Windows\Fonts`, a `STATEX_FONTDIR` override, a `--spread-store` flag and
  `fontTools`, none of which exist. There is no `requirements.txt` (needs
  numpy, Pillow, scipy).

## Known open items (from `docs/StaTeX-known-standins.md`)

Kerning (worth one corpus case); `showcase` assembles one delimiter tile fewer
than TeX; `\mathsf` missing; `\atop`/`\over` unimplemented; `\int` refuses above
~96 px em; SDF boxes wider than the ink; no perf guard in the harness. Also
recorded in `docs/StaTeX-lvgl-plan.md`: `ili9488_graphics.h:39` truncates a
rule's origin while rounding its size, so device and host disagree by up to a
pixel — its own change, with its own before/after oracle capture.
