# Known stand-ins and deliberate debt

Things knowingly not right yet, each with what replaces it. Written down
because most of them are *correct against the oracle today* and will go
silently wrong when the thing they stand in for arrives — a comment at the call
site is easy to miss when you are changing something else.

Not a bug list. Real defects get a failing test in `tests/defects/` instead
(dev-process §1). That directory is currently empty.

Where the differential stands: **73 of 75 comparable corpus cases place every
glyph within tolerance** at 22, 36 and 64px.

---

## Open

### Kerning — worth exactly one case, deliberately not done

`style_group` (`\mathbf{abc}`) is 0.031 em out, 0.57px over tolerance at 64px.
Diagnosed: cmbx10 declares a kern of **0.031944** for the pair (b, c) and
StaTeX does not kern. The measured gap error is 0.032 em, so that is the whole
of it.

This is the measurement the old "measure before building the kern tables" note
was asking for. The answer is that kerning buys one case, half a pixel over the
line, against ~1500 table entries across the CM faces. Ligatures buy none.
Revisit only if a case turns up where it matters visibly rather than
marginally.

### `showcase` assembles one delimiter tile fewer than TeX

21 glyphs against 23, and the only case the position test cannot score at all
(it cannot pair up glyph lists of different lengths). Both sides use the same
extensible recipe and the same stacking; StaTeX's `minh` for that matrix comes
out just under 4.20 em where TeX's is just over, so TeX adds a second tile.

The residual is a small content-height difference inside a formula combining a
fraction, a radical, a big operator and a script — every one of which now
converges on its own, so finding it means diffing the whole box tree. The
picture is otherwise indistinguishable from the reference.

### `\mathsf` is required and absent

STX-FNT-05 requires it; the parser has no case and the atlas no face. Corpus
case `style_sf` carries `openDefect = "MATHSF-MISSING"`.

### `\atop` / `\over` are not implemented

`Node::frac.rule` exists and is always true, because the parser has no syntax
that sets it false. Adding one means the other half of TeX82 rule 15c — `num3`
for the numerator shift, and a clearance of 7 rule thicknesses in display style
and 3 otherwise — plus a branch in the fraction arm. `num3` is not carried in
`FontParams` until then rather than sitting unused.

### `\int` refuses above ~96px em

Its display variant is 1.078 × 2.406 em, which at 100px needs 26,028 coverage
pixels against the 25,600 budget (`kMaxGlyphCoveragePx`, 160×160). A defined
refusal, not corruption, and far above the device's 22px. Note it *used* to
render at 100px only because the glyph was being clipped during atlas
generation — fixing that clip exposed a real limit rather than creating one.
Raising the budget costs coverage scratch in the arena, so it is a memory trade
rather than a free fix.

### SDF boxes are wider than the ink

A glyph rasterises over its ink box plus the distance-field pad, and the pad is
all zeros. Clipping the output rect to the record's ink fields saves 21% of
coverage pixels at 22px, 27% at 36px, 35% at 64px. A 1px AA margin is not
enough — it cuts 17 inked pixels at 22px and 524 at 64px — so use 2px, or have
genfont emit an exact ink bbox, which is worth ~40%.

A coverage cache is *not* the adjacent win it looks like: the showcase places
21 glyphs of which 20 are distinct, so a cache buys 2%.

Nothing has been measured on hardware, and there is no perf guard in the
harness — a layout change that doubled the sampling work would pass silently.

### genfont hygiene

- Python dependencies are undeclared. It needs Pillow, numpy and scipy and says
  so only in a docstring and the README. One `requirements.txt` would fix it.

### Accents are excluded from the symbol suite

Sixteen of MicroTeX's names are accents -- `\hat`, `\tilde`, `\vec`,
`\widehat`. StaTeX has no accent machinery, and an accent added as an ordinary
atom renders as a hat floating on the baseline at the wrong place, which is
worse than not having it. They are dropped by `mksymbols.py`, so `\hat` fails
with `UnknownCommand` rather than drawing something wrong.

Adding them needs layout work, not glyphs: an accent is positioned over its
argument's *skew* point, which is a per-glyph quantity `GlyphRecord` does not
carry.

### `\lmoustache` and `\rmoustache` need the delimiter path

These two have no single-glyph design at all. Their metrics row declares zero
height and zero depth and exists only to carry an extensible recipe, so TeX
always stacks them from pieces. Rasterising their slot draws whatever else sits
at that position in the shared TTF -- which is how the advance cross-check
caught them.

They are excluded until `\left` / `\right` exists, because a stretchy
delimiter is the only context they appear in anyway.

### `\textmu` is excluded: special.ttf cannot draw it

Every vendored Computer Modern face maps slot *n* to codepoint *n*, which is
what makes `chr(slot)` a valid way to ask for a glyph. `special.ttf` does not:
it holds **eight** distinct glyphs, maps 216 of its 256 slots to a single
`.notdef`, and aliases slots 101 and 109 to one glyph.

So `\textmu` drew a euro sign. It got through every existing check — the glyph
is not blank, it resolves through all four tables, and its advance matches its
TFM to 0.05 em, well inside the 0.20 em slot check. It was found by putting 600
glyphs on a contact sheet and looking at them.

genfont now refuses two *used* slots of one font that rasterise identically
while declaring different metrics, which is the precise signature. Different
fonts are exempt and must be: a period in cmmi10 and a centre dot in cmsy10 are
both small discs and downsample to the same field, correctly, with their
metrics placing them at different heights.

**The lesson is the one already written above** — check the picture, not just
the score. The contact sheets are written to the artifacts directory on every
run of `test_symbol_sweep` for exactly this reason.

### 147 symbols use private-use codepoints

StaTeX keys glyphs by `(Face, codepoint)`. MicroTeX's tables name a real
Unicode codepoint for 410 of the 600 symbols and `unicode-math-table.tex` adds
42; the rest have none, and 69 of those are stmaryrd, which largely predates
any Unicode assignment. Those get `U+E000` upward, assigned in sorted-name
order so the allocation is reproducible.

This is safe because the codepoint is an internal lookup key and never leaves
the repo -- `serialize()` is a test fixture (`statex_serialize.h`). It is worth
knowing anyway: an LVGL label cannot address those glyphs by character, and
`symbols.tsv` records unicode-math's value in a separate column wherever the
two disagree, so switching later is a one-column edit.

### The symbol table's name strings are in DTCM

`kSymbols` itself is in `.progmem`, but the `const char*` names point at
ordinary string literals, which land in `.rodata` and so in DTCM on this
target -- about 6 KB. A flash string pool (one `char[]` plus offsets) would
recover it, at the cost of changing `SymbolEntry`'s shape. Not worth doing
until DTCM is actually tight.

---

## Traps

Things that are right *now*, and the specific way each was wrong before.
Re-read the relevant one before changing that area.

### One glyph, two sources of truth

Six instances, all the same shape: a glyph's identity is decided in more than
one place and the places disagree. The numbers stay plausible and the picture
is wrong, so the position test cannot see it and only the artifact can.

| what disagreed | symptom |
|---|---|
| which face a letter takes | letters upright, TeX sets them italic |
| which font backs `Face::Italic` | lmroman10-italic (text) vs cmmi10 (math) |
| which font backs the Greek | cmmi10 metrics, Latin Modern *upright* shapes |
| which variant the draw walk fetches | measured variant 1, drew variant 4 |
| which codepoint `-` is | ASCII hyphen 0.332 em vs U+2212 0.778 |
| which face a test helper assumes | *negative* inter-atom glue reported |

**Closed by construction.** Every face draws from the same Computer Modern file
its metrics come from, and one mapping governs both. A face with no nominal
font is a hard error unless every one of its glyphs is redirected. Nothing
needs Latin Modern any more, so genfont no longer needs a MiKTeX install —
which was the Phase 0 reproducibility blocker.

That mapping is now *derived* rather than transcribed: `tools/genfont/
mksymbols.py` reads the font, slot and metrics table for every symbol out of
MicroTeX's own tables and checks the join in as `symbols.tsv`. The six
disagreements above were all cases of someone copying one half of a pair by
hand, so removing the hand-copying removes the whole class.

Two cross-checks catch what remains. The advance the TFM declares is compared
against the advance the raster measures, and a gap beyond 0.20 em is a build
error — that is a *different glyph*, so the slot is wrong. And every name in
the symbol table must resolve to a glyph in the atlas, checked both by the
generator and by `test_symbols.cpp` at runtime.

When adding a glyph: **check the picture, not just the score.**

### A VList centres its children

`drawTree` centres VList children horizontally. That is right for a fraction
and for stacked limits, and wrong three times over for anything left-aligned:
script columns, the subscript inside a column widened by an italic correction,
and the radicand once a trailing mu was added to it. Each time the symptom was
content drifting right by *half* the width difference.

To left-align, wrap the child in a row of the container's full width so the
centring is a no-op.

### Positional brace-init, twice over

C++17 has no designated initialisers, so removing a struct field without
removing its initialiser (or the reverse) shifts every value after it and the
compiler says nothing — a short list just zero-fills the tail. This happened
once while deleting `num3`; the only symptom was an unrelated-looking matrix
test failing. There is now a `static_assert` that the last field is non-zero,
which turns that silent failure into a build error.

`GlyphRecord` has the same hazard — genfont emits its fifteen fields
positionally — and now has a stronger guard: a sentinel record with a distinct
value per field, asserted field by field, which pins the *order* of all of them
rather than just catching a zero tail. `kGlyphs` itself cannot carry the
assertion, because it lives in a flash section and so is `const` rather than
`constexpr`, and a const array is not a constant expression.

Still **append fields only**, and update `tools/genfont/genfont.py` in the same
change.

### A test pinned to a magic number stops testing

Three separate helpers produced confident nonsense by restating a production
value instead of reading it: `advanceSum` hardcoded `Face::Roman` (twice, once
reporting *negative* glue), `test_matrix_metrics` hardcoded the column gap, and
`test_draw_atomicity` hardcoded a size at which a formula refuses. All now read
the constant or search for the condition. `mathGlyphRecord` in `stx_render.h`
exists so that no test decides a glyph's face for itself.

### The picture gate can never go green, by design

Around 65 of 75 cases still "diverge" on `test_rendered_pictures_match`,
because StaTeX samples an SDF atlas while the reference uses FreeType's hinted
outlines, and those differ by 3–7% of ink mass on an identically placed glyph.
Dice ≈ 0.85 is the floor for a *correct* formula, not a pass mark.

The acceptance criterion is `test_glyph_positions_agree`. The picture test
exists so a human can see what is wrong, and it has caught two defects the
position test was structurally blind to — the wrong-variant draw and the
upright Greek. **Do not "fix" its score.**

---

## Smaller notes

- **The SSIM gate is inert.** Cases scoring dice 0.35 score ssim 0.99, because
  blank windows dominate even on the tight canvas. It has never been the
  binding constraint and currently cannot be. Either window it to the ink or
  drop it.
- **The alignment search is seeded from the ink centroid** and searches
  ±maxShift around that seed, so a case with a missing glyph is scored at an
  offset that was never a candidate. Only affects cases already broken.
- **`tests/support` and `tests/oracle` build with exceptions and RTTI** while
  `lib/StaTeX` does not. Intended — MicroTeX needs them — noted so nobody
  "fixes" it.
- **`tests/oracle/test_oracle_diff.cpp` is ~600 lines.** Cohesive, but at the
  point where the corpus sweeps and the single-case checks want separating.
- **Two canvas conventions.** `Canvas`'s defaults (640×320, baseline 200) serve
  the behaviour suites; `oracleCanvas(size)` derives everything from the em.
  The defaults are arbitrary and predate the measurement.
- **`tools/genfont/genfont.py` uses `tfm_used = [0]`** as a mutable counter
  inside `main`. Works, reads badly.
