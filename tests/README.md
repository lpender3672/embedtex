# StaTeX tests

One test tree. CMake is the primary host workflow; PlatformIO still builds the
Teensy image and can still run `tests/unit`.

```
tests/
  framework/    a dependency-free Unity-compatible shim (unity.h)
  support/      the image + similarity engine, the formula corpus, the harness
  unit/         per-module suites (moved here from the old top-level test/)
  behaviour/    whole-pipeline properties over the corpus
  defects/      one suite per open defect -- RED ON PURPOSE
  oracle/       the MicroTeX differential (§4 of the dev-process doc)
    fonts/      the real Computer Modern faces, for rendering the reference
```

## Running

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` with no selector runs everything, and **will report failures** — the
defect suites are supposed to fail. Use the labels:

| command | what it means |
|---|---|
| `ctest --test-dir build -L green` | the gate. This must be clean. |
| `ctest --test-dir build -L red` | the open-defect report. Every failure here is a known bug. |
| `ctest --test-dir build -L oracle` | MicroTeX differential only |
| `ctest --test-dir build -L unit` | the per-module suites |

Options: `-DSTATEX_BUILD_ORACLE=OFF` skips building MicroTeX (much faster
configure), `-DSTATEX_WERROR=ON` for CI.

PlatformIO still works for what it is still for:

```sh
pio run -e teensy41     # the firmware
pio test -e native      # tests/unit only (test_dir in platformio.ini)
```

## The red/green policy

`docs/StaTeX-dev-process.md` §1 says the failing test comes first and must fail
for the right reason. The defect suites are that, kept permanently until the
defect is fixed.

Rules:

- **A red test is not broken.** Do not relax an assertion to make it pass. The
  assertion states the specified behaviour; if the specification is wrong,
  change the specification and say so.
- Each red test calls `TEST_DEFECT(id, summary)` and prints the measured value
  next to the expected one, so the output reads as a defect report.
- Each suite's header comment cites the file and line the defect lives at and
  what the requirement says should happen instead.
- When a defect is fixed its suite goes green on its own. Move it out of
  `defects/` into `behaviour/` and relabel it in `tests/CMakeLists.txt`.

Cases in the corpus that are affected by an open defect carry an `openDefect`
tag (`tests/support/stx_corpus.cpp`). Green suites skip those and name them in
the output; the tagged red suite asserts them. That way the deferred list
cannot silently grow.

## Open defects

| id | where | what |
|---|---|---|
| `GLYPH-COVERAGE` | `statex_layout.cpp:33`, `statex_draw.cpp:38` | a glyph missing from the atlas renders as blank space and `render()` still returns `Ok` (STX-FNT-05 says refuse) |
| `SDF-ROUNDING` | `statex_sdf.cpp:35` | `(g.boxW * emPx) / 256 + 0.5f` is integer division, so the `+ 0.5f` rounds nothing |
| `ATOM-SPACING` | `statex_layout.cpp` | `Node::atomType` is parsed and stored but never read; no inter-atom glue anywhere |
| `MATRIX-DEPTH` | `statex_layout.cpp:181` | short matrices get a negative depth |
| `MATRIX-TRAILING-GAP` | `statex_layout.cpp:180,192` | one column gap of dead space on the grid's right edge |
| `MATRIX-CENTRING` | `statex_layout.cpp:192` | cells sit at the left of their column instead of centred |
| `MATRIX-EMPTY` | `statex_parser.cpp:428` | `\begin{matrix}\end{matrix}` is accepted as a 0x0 matrix |
| `DRAW-ATOMICITY` | `statex_draw.cpp:43` | a draw-phase refusal happens after earlier glyphs are already on the panel, contradicting `statex_render.h:42` |
| `MATRIX-U16-OVERFLOW` | `statex_node.cpp:86` | `rows * cols` truncates to `u16` inside the guard that is meant to catch it |
| `LAYOUT-CELL-COUNT` | `statex_layout.cpp:288` | layout iterates `rows*cols` rather than the authoritative `cells.count` |
| `SQRT-VINCULUM` | `statex_layout.cpp:129` | no rule over the radicand |
| `SQRT-INDEX-DROPPED` | `statex_layout.cpp:129` | the index box is laid out at `:285` and then never read; `\sqrt[3]{x}` draws as `\sqrt{x}` |
| `SQRT-STRETCH` | `statex_layout.cpp:141` | one surd glyph scaled uniformly to the radicand height |
| `ORACLE-METRICS` | `statex_fontparams.h`, `tools/genfont` | layout constants and glyph metrics do not come from MicroTeX's tables |
| `MATH-ITALIC` | `statex_parser.cpp:466` | bare letters default to Roman; TeX sets math variables in italic |

## The similarity engine

`support/stx_compare.h`. Comparing renders pixel-for-pixel would be
permanently red for uninteresting reasons (antialiasing, sub-pixel rounding),
so comparisons are scored instead:

- `dice` / `iou` — thresholded ink overlap. Most sensitive to misplacement.
- `ncc` — zero-mean normalised cross-correlation; brightness-invariant.
- `ssim` — structural similarity over 8x8 uniform windows.
- `rmse` — raw residual, for reporting.
- `inkRatio` — total coverage mass ratio; catches "a glyph vanished".

Scoring happens **after** an integer-translation alignment search seeded from
the centroid offset, and the recovered offset is reported separately —
a constant shift is a different defect from a distorted layout, and
`SimilarityGate::maxShift` asserts on it directly.

`test_imgcompare` is the engine's own self-test suite. It is green, and it
must stay green: every oracle and golden verdict is expressed in these
numbers, so a bug here would quietly invalidate the rest.

## Failure pictures

Every failing comparison writes an annotated PNG into **`build/tests/artifacts/`**
(gitignored, regenerated on every `ctest` run). Three panels stacked, sharing
one crop rectangle so a vertical scan compares like with like:

```
  case id + the measured scores
  +--------------------------------+
  | reference (MicroTeX oracle)    |
  +--------------------------------+
  | statex                         |
  +--------------------------------+
  | diff                           |   red   = reference only
  +--------------------------------+   green = statex only
                                       grey  = both
```

PNG rather than PGM because these are for a human to open;
`support/stx_png.cpp` is a self-contained writer — fixed-Huffman deflate with a
greedy LZ77 matcher over Up-filtered rows, so no zlib dependency and the whole
set is about 1 MB rather than 22. Captions are drawn with StaTeX's own glyph
atlas rather than a bundled bitmap font, and panels are magnified by a whole
number of pixels (nearest-neighbour, so you are looking at the real pixels
enlarged, not a resample that could invent or hide a difference).

The differential renders at 36px rather than the device's 22px: sub-pixel
rounding at 22px adds noise the comparison has to tolerate, which blunts it,
and the pictures are meant to be read.

The oracle differential emits one per diverging case. If FreeType ever fails
to render a reference glyph the case is still pictured, labelled `NOT SCORED`
rather than silently omitted. The defect suites emit
one each where there is a meaningful reference to compare against:

| file | reference panel | what to look for |
|---|---|---|
| `defect_glyph_coverage.png` | `aXb`, every glyph present | the blank gap where `?` should be |
| `defect_sqrt_vinculum.png` | the radicand alone | nothing covers the radicand |
| `defect_sqrt_index_dropped.png` | `\sqrt{x}` | an **empty diff** — the index was never drawn |
| `defect_matrix_centring.png` | equal-width cells | the narrow cell hugging the left edge |
| `defect_draw_atomicity.png` | a blank canvas | all-green: ink that a refused render still emitted |

`writePgm` is still there for raw, chrome-free dumps and goldens.

## The MicroTeX oracle

`oracle/`. `lib/MicroTeX` is the dynamic implementation StaTeX was rewritten
from. It is built here only, never linked into the device image (STX-BLD-02),
with exceptions and RTTI on — StaTeX itself stays `-fno-exceptions -fno-rtti`,
and mixing is fine because StaTeX contains nothing that throws.

### How the layers split

MicroTeX does the parsing, the box model and the placement. The one thing it
leaves to the platform is turning *"slot 0x32 of cmex10 at size z, here"* into
pixels — every MicroTeX port supplies that. `oracle/ft_raster.cpp` supplies it
with FreeType and the real Computer Modern faces in `oracle/fonts/`.

That split is the whole design. **The layer StaTeX reimplements (layout) comes
from MicroTeX untouched. The layer underneath it (glyph rasterisation) is
supplied independently.** So the reference is genuine TeX output, and StaTeX's
own atlas is not involved on that side at all.

An earlier version drew the reference with StaTeX's SDF atlas, on the theory
that identical glyph images would isolate position. That was wrong: the atlas
has exactly *one* radical and one of each bracket, whereas TeX picks a taller
glyph from a size-variant family and, past its largest member, assembles the
delimiter from top/extension/bottom pieces. Every variant had to be faked by
stretching the one glyph available, and it took a learned slot→Unicode map,
metric-derived scaling and piece merging to look wrong slightly less. Drawing
the real glyph deleted all of that.

### The fonts

`oracle/fonts/` — 30 Computer Modern faces plus their licences, ~450 KB,
restored from git history (they were dropped in `0e5dc5d` when the firmware
moved to the SDF atlas). **Test-only: the firmware still does not ship them.**
Override the location with `STATEX_TEX_FONTS` to point at a different TeX
installation.

FreeType is an optional host dependency. Without it the oracle target is
skipped with a warning and everything else — the green gate, the defect suites
— still builds and runs. MSYS2: `pacman -S mingw-w64-x86_64-freetype`;
Debian: `libfreetype-dev`.

### What it does not cover

Per dev-process §4: refusal behaviour, arena exhaustion and depth bounding.
MicroTeX would render, throw or grow the heap rather than refuse, so those
stay with the hand-written suites.

Baselines are aligned before comparison: `TeXRender::draw()` places the box's
top-left at the given point and `getBaseline()` returns a *fraction* of the
height, whereas StaTeX draws from the baseline.

Set `STX_ORACLE_DEBUG=1` to dump every glyph MicroTeX asked for — slot, face,
pen position, size — plus the rules and any glyph FreeType could not render.

### Why the differential is red

Dev-process §4 names the precondition:

> Same metric source. StaTeX's flash tables must be generated from the very
> values MicroTeX uses […] Otherwise divergence is just different input
> numbers, not an algorithm bug.

It is not met. The fourteen constants in `statex_fontparams.h` are hand-picked,
not MicroTeX's `tex_param.res.cpp` values, and the glyph metrics come from
Latin Modern via `tools/genfont` rather than `res/font/*.def.cpp`. So
`test_oracle_diff` measures how far off things are; it goes green when the
flash tables are regenerated from MicroTeX's own numbers.

One consequence of using the real faces: the image scores now also carry the
`MATH-ITALIC` defect, because the reference sets variables in cmmi10 italic
and StaTeX sets them upright. That is a real divergence and it should show —
but it means a score is currently "typeface + position", not position alone.
Fixing `MATH-ITALIC` makes the scores a clean layout signal again.

### A note on the vendored fork

`lib/MicroTeX` is a local fork ("modified libs to build for teensy"), not
pristine upstream. Its two layout-adjacent patches — `SymbolAtom::get` and
`Formula::get` returning null instead of throwing — are both behind
`#if defined(ARDUINO)`, so **the host build takes the original upstream path**
and the oracle's layout is unmodified MicroTeX. Worth re-checking if anyone
patches that tree again.

## Adding a case

Add one line to `support/stx_corpus.cpp`. Every corpus-driven suite widens at
once: the refusal audit, determinism, the memory-budget sweep, and the
differential. Give it a stable `id` — goldens and artifact files are named
after it.
