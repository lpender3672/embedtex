#!/usr/bin/env python3
"""StaTeX offline glyph generator (STX-FNT-04).

Rasterizes glyphs with Pillow's bundled FreeType, computes a signed distance
field per glyph (scipy), downsamples + encodes to 8-bit, derives em-normalized
metrics, and emits a constexpr C++ glyph store (statex_glyphs.gen.cpp) matching
lib/StaTeX/statex_glyphstore.h.

Env note: needs Pillow, numpy, scipy (all present in the project's Python).
freetype-py is NOT required — Pillow bundles FreeType. Source fonts are taken
from the system font directory; override per face in FACES.

Usage:
    python tools/genfont/genfont.py [--out PATH] [--render 256] [--max-sdf 48]
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

# Proper LaTeX fonts from a MiKTeX install: Latin Modern (Computer Modern's
# modern Unicode form) for text faces + Latin Modern Math for symbols/Greek.
MIKTEX = os.environ.get(
    "STATEX_MIKTEX",
    r"C:\Users\louis\AppData\Local\Programs\MiKTeX\fonts\opentype\public")
LM = os.path.join(MIKTEX, "lm")
LMM = os.path.join(MIKTEX, "lm-math", "latinmodern-math.otf")

LATIN_LOWER = list("abcdefghijklmnopqrstuvwxyz")
LATIN_UPPER = list("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
DIGITS = list("0123456789")
OPS = list("+-=()[]<>/.,;:!|")
GREEK_LOWER = [chr(c) for c in range(0x3B1, 0x3CA)]  # α..ω
SYMBOLS = ["√", "∑", "∫", "⋅", "×", "≤", "≥",
           "∞", "π", "θ", "ω", "α", "β",
           "γ", "ϕ"]
# Blackboard: parser keys by the ASCII letter, but we rasterize the
# double-struck Unicode glyph so \mathbb{R} looks right.
BB_MAP = {"R": "ℝ", "C": "ℂ", "N": "ℕ", "Z": "ℤ", "Q": "ℚ"}

# face_id -> (name, font path, glyph chars, render-substitution map)
FACES = {
    0: ("Roman", os.path.join(LM, "lmroman10-regular.otf"),
        LATIN_LOWER + LATIN_UPPER + DIGITS + OPS, {}),
    1: ("Italic", os.path.join(LM, "lmroman10-italic.otf"),
        LATIN_LOWER + LATIN_UPPER, {}),
    2: ("Bold", os.path.join(LM, "lmroman10-bold.otf"),
        LATIN_LOWER + LATIN_UPPER + DIGITS, {}),
    3: ("Symbol", LMM, SYMBOLS + GREEK_LOWER, {}),
    4: ("Blackboard", LMM, list("RCNZQ"), BB_MAP),
}


def em_fixed(v_em):
    """em (float) -> i16 fixed point in 1/256 em, clamped to i16."""
    x = int(round(v_em * 256.0))
    return max(-32768, min(32767, x))


def make_sdf(font, ch, render_px, pad_px, max_sdf, spread_em):
    """Render one glyph; return (sdf_u8 HxW, metrics dict) or (None, metrics)."""
    canvas = render_px * 3
    img = Image.new("L", (canvas, canvas), 0)
    draw = ImageDraw.Draw(img)
    ox, oy = canvas // 4, canvas * 3 // 4  # pen left, baseline
    draw.text((ox, oy), ch, fill=255, font=font, anchor="ls")
    a = np.asarray(img, dtype=np.uint8)

    advance_px = float(font.getlength(ch))
    ys, xs = np.where(a > 0)
    em = float(render_px)
    if len(xs) == 0:  # blank (e.g. space) — metrics only
        return None, dict(advance=advance_px / em, bearingX=0.0, height=0.0,
                          depth=0.0, italic=0.0, box=None)

    ink_l, ink_r = int(xs.min()), int(xs.max()) + 1
    ink_t, ink_b = int(ys.min()), int(ys.max()) + 1

    # Padded box for distance gradient on both sides of the contour.
    bx0, by0 = ink_l - pad_px, ink_t - pad_px
    bx1, by1 = ink_r + pad_px, ink_b + pad_px
    sub = a[by0:by1, bx0:bx1]
    inside = sub > 127
    din = ndimage.distance_transform_edt(inside)
    dout = ndimage.distance_transform_edt(~inside)
    sdf = din - dout  # render px, +inside

    # Downsample the distance field to fit max_sdf. Values stay in *render px*
    # (we're just sampling the field at a coarser grid).
    bh, bw = sdf.shape
    scale = min(1.0, max_sdf / max(bw, bh))
    out_w = max(1, int(round(bw * scale)))
    out_h = max(1, int(round(bh * scale)))
    sdf_img = Image.fromarray(sdf.astype(np.float32), mode="F")
    sdf_small = np.array(sdf_img.resize((out_w, out_h), Image.BILINEAR),
                         dtype=np.float32)

    # Encode signed distance in EM units against a fixed em spread, so the
    # sampler's decode (distEm = (enc-128)/127 * spreadEm) matches exactly and
    # stroke interiors saturate to full coverage regardless of glyph size.
    enc = np.clip(128.0 + (sdf_small / render_px) / spread_em * 127.0, 0, 255)
    sdf_u8 = enc.astype(np.uint8)

    metrics = dict(
        advance=advance_px / em,
        bearingX=(ink_l - ox) / em,
        height=(oy - ink_t) / em,
        depth=(ink_b - oy) / em,
        italic=max(0.0, (ink_r - (ox + advance_px)) / em),
        box=dict(x=(bx0 - ox) / em, y=(oy - by0) / em,
                 w=(bx1 - bx0) / em, h=(by1 - by0) / em),
    )
    return sdf_u8, metrics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(
        os.path.dirname(__file__), "..", "..", "lib", "StaTeX",
        "statex_glyphs.gen.cpp"))
    ap.add_argument("--render", type=int, default=256)
    ap.add_argument("--max-sdf", type=int, default=48)
    ap.add_argument("--pad", type=int, default=24)
    # Encoded distance range, in em, spanned per +/-127 levels about the
    # contour. Large enough that stroke interiors saturate (full coverage) yet
    # leave precision for the ~1px antialiasing band near the edge.
    ap.add_argument("--spread-em", type=float, default=0.125)
    args = ap.parse_args()

    spread_em = args.spread_em

    records = []     # tuples for sorting/emit
    sdf_blob = bytearray()
    skipped = []

    for face_id in sorted(FACES):
        name, path, glyphs, rmap = FACES[face_id]
        if not os.path.exists(path):
            print(f"  WARN face {name}: font not found: {path}", file=sys.stderr)
            continue
        font = ImageFont.truetype(path, args.render)
        seen = set()
        for ch in glyphs:
            cp = ord(ch)
            if cp in seen:
                continue
            seen.add(cp)
            render_ch = rmap.get(ch, ch)  # blackboard etc. substitute glyph
            sdf, m = make_sdf(font, render_ch, args.render, args.pad,
                              args.max_sdf, spread_em)
            if sdf is None or m["box"] is None:
                skipped.append((name, cp))
                continue
            off = len(sdf_blob)
            h, w = sdf.shape
            sdf_blob += sdf.tobytes()
            records.append(dict(
                face=face_id, cp=cp, w=w, h=h, off=off,
                advance=m["advance"], bearingX=m["bearingX"],
                height=m["height"], depth=m["depth"], italic=m["italic"],
                boxX=m["box"]["x"], boxY=m["box"]["y"],
                boxW=m["box"]["w"], boxH=m["box"]["h"]))

    # Sort by (face, codepoint) for binary search.
    records.sort(key=lambda r: (r["face"], r["cp"]))

    out = os.path.abspath(args.out)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"// GENERATED by tools/genfont/genfont.py — do not edit.\n")
        f.write(f"// render={args.render} max_sdf={args.max_sdf} "
                f"pad={args.pad} spread_em={spread_em}\n")
        f.write('#include "statex_glyphstore.h"\n\nnamespace statex {\nnamespace {\n\n')
        f.write(f"const u8 kSdf[] STATEX_FLASH = {{\n")
        for i in range(0, len(sdf_blob), 24):
            chunk = ",".join(str(b) for b in sdf_blob[i:i + 24])
            f.write("  " + chunk + ",\n")
        f.write("};\n\n")
        f.write("const GlyphRecord kGlyphs[] STATEX_FLASH = {\n")
        for r in records:
            f.write("  {{{face},{w},{h},0x{cp:X},{off},"
                    "{adv},{bx},{ht},{dp},{it},{x},{y},{bw},{bh}}},\n".format(
                        face=r["face"], w=r["w"], h=r["h"], cp=r["cp"],
                        off=r["off"], adv=em_fixed(r["advance"]),
                        bx=em_fixed(r["bearingX"]), ht=em_fixed(r["height"]),
                        dp=em_fixed(r["depth"]), it=em_fixed(r["italic"]),
                        x=em_fixed(r["boxX"]), y=em_fixed(r["boxY"]),
                        bw=em_fixed(r["boxW"]), bh=em_fixed(r["boxH"])))
        f.write("};\n\n")
        f.write(f"constexpr int kCount = {len(records)};\n")
        f.write(f"constexpr int kSpread = {em_fixed(spread_em)};\n")
        f.write(f"constexpr int kEmPx = {args.render};\n\n")
        f.write("""}  // namespace

const u8* glyphSdfData() { return kSdf; }
int glyphRecordCount() { return kCount; }
int glyphSdfSpread() { return kSpread; }
int glyphEmPx() { return kEmPx; }

const GlyphRecord* findGlyphRecord(Face face, c32 cp) {
  const u8 f = static_cast<u8>(face);
  int lo = 0, hi = kCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const GlyphRecord& g = kGlyphs[mid];
    if (g.face == f && g.codepoint == cp) return &g;
    if (g.face < f || (g.face == f && g.codepoint < cp)) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

}  // namespace statex
""")

    print(f"wrote {out}")
    print(f"  glyphs: {len(records)}  sdf bytes: {len(sdf_blob)}  "
          f"({len(sdf_blob)/1024:.1f} KB)")
    print(f"  spread_em(1/256): {em_fixed(spread_em)}  em_px: {args.render}")
    if skipped:
        print(f"  skipped (blank/missing): {len(skipped)}")
    # Self-checks.
    assert records == sorted(records, key=lambda r: (r["face"], r["cp"]))
    for r in records:
        assert 0 < r["w"] <= args.max_sdf and 0 < r["h"] <= args.max_sdf
        assert r["off"] + r["w"] * r["h"] <= len(sdf_blob)
        assert r["advance"] > 0
    print("  self-checks OK")


if __name__ == "__main__":
    main()
