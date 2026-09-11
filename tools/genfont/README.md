# genfont — StaTeX's offline font pipeline (STX-FNT-04)

Off-device tools that turn Computer Modern into the checked-in tables the
firmware compiles. The device never rasterises an outline or opens a font file
(STX-RES-02/03): all of it happens here, once, on the host.

There are three tools, and they run in this order.

| tool | reads | writes |
|---|---|---|
| `mksymbols.py` | `lib/MicroTeX/res/**`, `unicode-math-table.tex` | `symbols.tsv` |
| `genfont.py` | `symbols.tsv`, `tests/oracle/fonts/*.ttf` | `lib/glyphstore/statex_glyphs.gen.cpp` |
| `mksymtable.py` | `symbols.tsv` | `lib/StaTeX/statex_symbols.gen.cpp` |

`diffatlas.py` compares two generated atlases and is the check that guards all
of it.

## Why a table and not a list

Every fact about a symbol — which font draws it, at which slot, with which
metrics, under which name, in which spacing class — is already in the vendored
MicroTeX tree. It was previously *transcribed* into Python: 25 slot entries, 34
size-variant entries, 6 font paths, and a 14-entry symbol list. At 600 symbols
that does not scale, and every hand-copied slot number is a chance to pair one
glyph's picture with another's measurements.

So `mksymbols.py` does the join once and checks the result in as `symbols.tsv`.
It resolves:

- **name → (fontId, slot)** from `res/sym/*.def.cpp`
- **fontId → font** from the *positional* `REG_FONT` order in
  `res/reg/builtin_font_reg.cpp` — nothing writes the number down, it is the
  index in that list
- **font → TTF** from each `DEF_FONT` line, whose `fonts/...` subtree happens
  to be exactly the layout of `tests/oracle/fonts`
- **name → AtomType** from `res/builtin/tex_symbols.res.cpp`
- **name → codepoint** from `res/builtin/formula_mappings.res.cpp`, then
  `unicode-math-table.tex`, then the private-use block

## Fonts

Everything is rasterised from the Computer Modern TTFs vendored at
`tests/oracle/fonts/` — the very files MicroTeX's metric tables describe, and
the ones the differential oracle rasterises. Nothing is read from the system
font directory.

That shared source is the point. genfont once needed a MiKTeX install for Latin
Modern, which meant nobody else could regenerate the atlas *and* that shapes
came from one family while metrics came from another. The second is what made
the Greek come out upright.

Shapes come from the raster; **metrics come from the TFM tables**, never from
the ink. An ink bounding box is not TeX's design box, and for the big operators
in cmex10 the difference is more than an em — a display summation is positioned
from its declared height and depth, which are deliberately not its extent.

The TTFs are addressed by raw slot, not by character: their cmap maps slot *n*
to codepoint *n*, so `chr(slot)` is how you ask Pillow for one.

## Environment

Python 3 with **Pillow, numpy and scipy**. No virtualenv, no `fontTools`, and
`freetype-py` is not required — Pillow bundles FreeType.

`unicode-math-table.tex` is optional and only fills codepoints MicroTeX's own
tables do not give (42 of 600). It ships with CTAN's `unicode-math`:

```sh
mpm --install=unicode-math        # MiKTeX
```

Without it those 42 fall back to private-use codepoints, which render
identically — the codepoint is StaTeX's lookup key, not a claim about the
character's identity.

## Run

```sh
python tools/genfont/mksymbols.py
python tools/genfont/genfont.py
python tools/genfont/mksymtable.py --check-atlas lib/glyphstore/statex_glyphs.gen.cpp
```

`genfont.py` takes `--render 256 --max-sdf 48 --pad 24 --spread-em 0.125` (the
SDF parameters, and the current output is built at exactly these) plus
`--symbols all|legacy`. `legacy` restricts the Symbol face to the set that
predated `symbols.tsv`, which is how the table-driven path was proved to change
no glyph.

Output today: **1023 glyphs**, ~1.5 MB of SDF data, and **600 symbols**.

## Verify

```sh
cmake --build build && ctest --test-dir build
```

After regenerating, the check that matters is not `cmp`. Two atlases differ
byte-for-byte as soon as one glyph is added or the blob is written in a
different order, and the question worth asking is whether a glyph that *already
existed* moved:

```sh
python tools/genfont/diffatlas.py OLD.cpp NEW.cpp --common-only
```

It compares `{(face, codepoint, variant): metrics + SDF bytes}`, so it is
invariant to ordering and to glyphs appearing. Exit status is 1 if any shared
glyph changed, so it works as a gate.

Change the SDF parameters and *every* glyph changes, which throws that check
away. That is why a parameter change is always its own commit, judged against
the oracle rather than against the previous atlas.

## Consuming the output

`lib/glyphstore/statex_glyphstore.h` declares the record format and the
accessors (`findGlyphRecord`, `findGlyphVariant`, `findGlyphVariantAtLeast`,
`glyphSdfData`). Layout reads them directly; `statex_sdf.cpp` samples the field.

`GlyphRecord` is emitted **positionally**, so the field order in `genfont.py`
has to match the struct exactly. A sentinel in the generated file asserts every
field's position at compile time — append a field on one side only and the last
one silently reads zero.
