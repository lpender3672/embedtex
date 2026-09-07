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

### Performance has never been looked at

From the original survey and still true: the SDF sampler does two divisions per
output pixel (`statex_sdf.cpp`), roughly 28 cycles/pixel of avoidable cost, and
SDF boxes are 1.28× the ink bbox so tightening them is worth 20–25%. Neither
has been measured on hardware. There is no perf guard in the harness either — a
layout change that doubled the sampling work would pass silently.

### genfont hygiene

- `tools/genfont/README.md` is stale: it documents `C:\Windows\Fonts`, a
  `STATEX_FONTDIR` override, a `--spread-store` flag and `getmask`, none of
  which the script has. The pipeline description at the top is accurate; the
  configuration half describes a version that no longer exists.
- Python dependencies are undeclared. It needs Pillow, numpy and scipy and says
  so only in a docstring. One `requirements.txt` would fix it.

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
its metrics come from, and `TFM_SLOT` in genfont governs both. A face with no
nominal font is a hard error unless every one of its glyphs is redirected.
Nothing needs Latin Modern any more, so genfont no longer needs a MiKTeX
install — which was the Phase 0 reproducibility blocker.

When adding a glyph: **check the picture, not just the score.**

### A VList centres its children

`drawTree` centres VList children horizontally. That is right for a fraction
and for stacked limits, and wrong three times over for anything left-aligned:
script columns, the subscript inside a column widened by an italic correction,
and the radicand once a trailing mu was added to it. Each time the symptom was
content drifting right by *half* the width difference.

To left-align, wrap the child in a row of the container's full width so the
centring is a no-op.

### `FontParams` is positional brace-init

C++17 has no designated initialisers, so removing a struct field without
removing its initialiser (or the reverse) shifts every value after it and the
compiler says nothing — a short list just zero-fills the tail. This happened
once while deleting `num3`; the only symptom was an unrelated-looking matrix
test failing. There is now a `static_assert` that the last field is non-zero,
which turns that silent failure into a build error.

The same hazard applies to `GlyphRecord`, whose initialiser genfont emits
positionally: **append fields only.**

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
