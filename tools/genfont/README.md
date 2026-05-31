# genfont — StaTeX offline SDF glyph generator (STX-FNT-04)

Off-device tool that turns source fonts into the checked-in `constexpr` glyph
store the firmware compiles. The device never rasterizes outlines or reads font
files (STX-RES-02/03); all of that happens here, once, on the host.

## Pipeline

```
TTF --Pillow/FreeType--> high-res glyph raster
    --scipy distance_transform_edt--> signed distance field (render px)
    --downsample + encode--> 8-bit SDF (128 = contour)
    --Pillow metrics--> em-normalized advance/bearing/height/depth/italic
    --emit--> lib/StaTeX/statex_glyphs.gen.cpp  (matches statex_glyphstore.h)
```

## Environment (the quirks, resolved)

- **Python 3** with **Pillow, numpy, scipy, fontTools** — all already present in
  the project's Python (3.10). No virtualenv needed.
- **`freetype-py` is NOT required.** Pillow bundles its own FreeType, so glyph
  rasterization (`ImageFont.truetype` + `getmask`) and metrics (`getlength`,
  ink bbox) come from Pillow directly. (Trying to `import freetype` fails — that's
  expected and fine.)
- **Source fonts** are read from the system font directory, default
  `C:\Windows\Fonts`, overridable via the `STATEX_FONTDIR` env var. The
  face→font mapping lives in `FACES` in `genfont.py`:
  - Roman → `times.ttf`, Italic → `timesi.ttf`, Bold → `timesbd.ttf`
  - Symbol/Greek → `seguisym.ttf`; Blackboard → placeholder (seguisym)
  Swap these for Latin Modern / STIX / Cambria Math for production-quality math.
- Two gotchas already handled in the script: a PIL→numpy array is read-only
  (we copy with `np.array`), and codepoints duplicated across glyph lists are
  de-duped per face (the table must have unique (face, codepoint) keys).

## Run

```sh
python tools/genfont/genfont.py            # writes lib/StaTeX/statex_glyphs.gen.cpp
python tools/genfont/genfont.py --render 256 --max-sdf 48 --pad 24 --spread-store 6
```

Output today: ~231 glyphs across 5 faces, ~416 KB of SDF data (well within the
Teensy 4.1's ~7.7 MB free flash; `--gc-sections` strips it if unreferenced).

## Verify

`pio test -e native -f test_glyphstore` compiles the generated table and checks
lookup across faces, absent-glyph handling, face isolation, and SDF contour
sanity. Re-run it after regenerating.

## Consuming the output

`lib/StaTeX/statex_glyphstore.h` declares the format and accessors
(`findGlyphRecord`, `glyphSdfData`, `glyphSdfSpread`, `glyphEmPx`). The next
phases (FNT-02 SDF sampler, FNT-03 antialiasing, metric-driven layout) read
from these — not yet wired into the render path.
