#!/usr/bin/env python3
"""StaTeX offline glyph generator (STX-FNT-04).

Rasterizes glyphs with Pillow's bundled FreeType, computes a signed distance
field per glyph (scipy), downsamples + encodes to 8-bit, derives em-normalized
metrics, and emits a constexpr C++ glyph store (statex_glyphs.gen.cpp) matching
lib/glyphstore/statex_glyphstore.h.

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
import tfm
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

# Every face is rasterised from the Computer Modern TTFs vendored at
# tests/oracle/fonts/ -- the very fonts MicroTeX's metric tables describe. That
# is not just tidiness: genfont used to need a MiKTeX install for Latin Modern,
# which meant nobody else could regenerate the atlas, and it meant shapes came
# from one font family while metrics came from another. The second of those is
# what made the Greek come out upright.
CM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                  "..", "..", "tests", "oracle", "fonts")

LATIN_LOWER = list("abcdefghijklmnopqrstuvwxyz")
LATIN_UPPER = list("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
DIGITS = list("0123456789")
OPS = list("+-=()[]<>/.,;:!|")
SYMBOLS = ["√", "∑", "∫", "⋅", "×", "≤", "≥",
           "∞", "π", "θ", "ω", "α", "β",
           "γ", "ϕ",
           # ASCII `-` and `*` are set from the symbol font in math mode, not
           # from the text face: TeX's mathcode sends them to family 2. The
           # text hyphen is 0.332em where the math minus is 0.778em, so using
           # the wrong one misplaces everything after it. LMM's advances for
           # these match cmsy10's TFM (0.778015 vs 0.777781, 0.500000 vs
           # 0.500002), so the Symbol face is the right home for them.
           "−", "∗"]

# face_id -> (name, font path, glyph chars, render-substitution map, TFM font)
#
# The TFM font, when given, is the authority for advance/height/depth/italic;
# only the SHAPE comes from the raster. That matters because an ink bounding
# box is not TeX's design box: measured against cmr10 and cmmi10 the atlas was
# up to 0.023 em too tall and gave *every* glyph ~0.0117 em of depth where the
# TFM says zero -- the antialiasing fringe below the baseline counted as ink.
# Advances were already fine (within the 1/256 em quantisation); heights and
# depths were not, and they are what drive matrix row pitch, script shifts and
# fraction gaps.
#
# Only ASCII codepoints are taken from the TFM, because only those sit at a
# slot equal to their codepoint. Greek and the symbol set need a real
# codepoint -> slot map and keep their raster metrics for now. Every TFM lookup
# is cross-checked against the raster advance, so a wrong mapping is a build
# error rather than a subtly wrong table.
FACES = {
    # Shape AND metrics from cmr10. These used to disagree -- shapes from
    # Latin Modern, metrics from Computer Modern -- which is the same
    # inconsistency that made the Greek upright, just with two designs close
    # enough that it never showed.
    0: ("Roman", os.path.join(CM, "latin", "cmr10.ttf"),
        LATIN_LOWER + LATIN_UPPER + DIGITS + OPS, {}, "cmr10"),
    # Math italic is cmmi10, NOT the text italic. They are different designs:
    # lmroman10-italic's 'a' is ~10% narrower than cmmi10's and sits at a
    # different bearing, which showed up as a persistent shape and width
    # mismatch against the oracle for every letter. Letters live at their ASCII
    # slots in cmmi10, so addressing them by character is safe.
    1: ("Italic", os.path.join(CM, "base", "cmmi10.ttf"),
        LATIN_LOWER + LATIN_UPPER, {}, "cmmi10"),
    2: ("Bold", os.path.join(CM, "latin", "optional", "cmbx10.ttf"),
        LATIN_LOWER + LATIN_UPPER + DIGITS, {}, "cmbx10"),
    # Both of these are entirely TFM_SLOT-redirected, so their nominal font is
    # never opened; every glyph is drawn from the Computer Modern file the
    # mapping names. The Symbol face lists only the symbols the parser has
    # commands for -- it used to carry the whole U+03B1..U+03C9 Greek range,
    # 19 of which were unreachable and existed only as atlas weight.
    3: ("Symbol", None, SYMBOLS, {}, None),
    4: ("Blackboard", None, list("RCNZQ"), {}, None),
}

# --- size variants ---------------------------------------------------------
# TeX does not enlarge a symbol by scaling it. Computer Modern ships purpose-cut
# glyphs at each size in cmex10, and TeX walks a chain from the text-size glyph
# to progressively larger ones. Scaling a 10pt design instead distorts stroke
# weight and, worse, puts the glyph in the wrong place: a display summation is
# positioned by `shift = (height + depth)/2 + axis` from its OWN metrics, and
# those differ from the text sigma's by more than an em.
#
# Slots are TeX font positions. The vendored TTFs carry a cmap that maps a slot
# straight to the codepoint of the same number, which is also how the oracle's
# FreeType rasteriser reaches them, so `chr(slot)` is the right way to ask
# Pillow for one.
#
# Metrics come from the TFM tables (tfm.py), never from the raster: see the
# module docstring there for why the difference matters here specifically.
#
# (face, codepoint, variant) -> (ttf relative to CM, tfm font name, slot)
# The vendored TTF for each TFM font. A TFM_SLOT entry says "this glyph is
# really <font> slot <n>", and that has to govern the RASTER as well as the
# metrics -- otherwise the atlas carries the right measurements around the
# wrong picture.
#
# This is how the Greek came out upright: TFM_SLOT correctly said cmmi10, but
# the shape was still rasterised from Latin Modern Math at U+03B1..U+03C9,
# where Unicode puts the *upright* Greek letters. TeX's math Greek is cmmi10's
# italic. Exactly the mistake that made Face::Italic wrong once already, one
# level down.
TFM_FONT_FILE = {
    "cmr10": "latin/cmr10.ttf",
    "cmmi10": "base/cmmi10.ttf",
    "cmsy10": "maths/cmsy10.ttf",
    "cmex10": "base/cmex10.ttf",
    "msbm10": "maths/msbm10.ttf",
    "cmbx10": "latin/optional/cmbx10.ttf",
}

_font_cache = {}


def cm_font(name, render_px):
    """The vendored TTF for a TFM font name, or None if it is not one we ship."""
    rel = TFM_FONT_FILE.get(name)
    if rel is None:
        return None
    key = (name, render_px)
    if key not in _font_cache:
        path = os.path.join(CM, *rel.split("/"))
        if not os.path.exists(path):
            raise SystemExit("vendored font missing: " + path)
        _font_cache[key] = ImageFont.truetype(path, render_px)
    return _font_cache[key]


# Must match kFirstPieceVariant in lib/glyphstore/statex_glyphstore.h.
PIECE_TOP, PIECE_REPEAT, PIECE_BOTTOM = 8, 9, 10

VARIANTS = {
    # Display-size big operators. `\sum` and `\int` in display style.
    (3, "∑", 1): ("base/cmex10.ttf", "cmex10", 88),
    (3, "∫", 1): ("base/cmex10.ttf", "cmex10", 90),
    # Radicals. TeX's chain is cmsy10 112 (the base surd, variant 0 below)
    # then cmex10 112 -> 113 -> 114 -> 115, each ~0.6em deeper than the last:
    # totals 1.20, 1.80, 2.40, 3.00 em against the base's 1.00.
    # (cmex10 116 is the repeatable piece of the extensible recipe, which needs
    # assembly rather than a plain record, so it is not a chain entry.)
    (3, "√", 1): ("base/cmex10.ttf", "cmex10", 112),
    (3, "√", 2): ("base/cmex10.ttf", "cmex10", 113),
    (3, "√", 3): ("base/cmex10.ttf", "cmex10", 114),
    (3, "√", 4): ("base/cmex10.ttf", "cmex10", 115),
    # Growing delimiters. Base is the text glyph in cmr10 (variant 0, emitted
    # by the FACES loop); the chain then runs through cmex10. Totals are 1.20,
    # 1.80 and 2.40 em against the text bracket's ~0.75.
    #   [  cmr10 91 -> cmex10 163 -> 104 -> 183
    #   ]  cmr10 93 -> cmex10 164 -> 105 -> 184
    #   (  cmr10 40 -> cmex10 161 -> 179 -> 181
    #   )  cmr10 41 -> cmex10 162 -> 180 -> 182
    # Each chain continues past this in cmex10, but the next step is the
    # extensible recipe rather than a single glyph, so it stops here.
    (0, "[", 1): ("base/cmex10.ttf", "cmex10", 163),
    (0, "[", 2): ("base/cmex10.ttf", "cmex10", 104),
    (0, "[", 3): ("base/cmex10.ttf", "cmex10", 183),
    (0, "]", 1): ("base/cmex10.ttf", "cmex10", 164),
    (0, "]", 2): ("base/cmex10.ttf", "cmex10", 105),
    (0, "]", 3): ("base/cmex10.ttf", "cmex10", 184),
    (0, "(", 1): ("base/cmex10.ttf", "cmex10", 161),
    (0, "(", 2): ("base/cmex10.ttf", "cmex10", 179),
    (0, "(", 3): ("base/cmex10.ttf", "cmex10", 181),
    (0, ")", 1): ("base/cmex10.ttf", "cmex10", 162),
    (0, ")", 2): ("base/cmex10.ttf", "cmex10", 180),
    (0, ")", 3): ("base/cmex10.ttf", "cmex10", 182),
    # The last single glyph before each chain reaches its recipe, 3.00 em.
    # Leaving these out made `matrix_3x1` assemble a delimiter from pieces
    # where TeX just picks a taller bracket.
    (0, "[", 4): ("base/cmex10.ttf", "cmex10", 34),
    (0, "]", 4): ("base/cmex10.ttf", "cmex10", 35),
    (0, "(", 4): ("base/cmex10.ttf", "cmex10", 195),
    (0, ")", 4): ("base/cmex10.ttf", "cmex10", 33),

    # Extensible pieces. Past the last single glyph TeX stops choosing and
    # starts BUILDING: each font's EXTENSIONS block gives
    # `slot, top, mid, repeat, bottom`, and the delimiter is assembled by
    # stacking the top, as many repeats as it takes, and the bottom.
    #
    #   [  50, 50, -1, 54, 52      ]  51, 51, -1, 55, 53
    #   (  48, 48, -1, 66, 64      )  49, 49, -1, 67, 65
    #
    # These live at variant numbers >= kFirstPieceVariant so the "smallest that
    # fits" walk never mistakes a 0.6 em repeat tile for a size step. None of
    # these four has a middle piece, so only three of the slots are used.
    (0, "[", PIECE_TOP): ("base/cmex10.ttf", "cmex10", 50),
    (0, "[", PIECE_REPEAT): ("base/cmex10.ttf", "cmex10", 54),
    (0, "[", PIECE_BOTTOM): ("base/cmex10.ttf", "cmex10", 52),
    (0, "]", PIECE_TOP): ("base/cmex10.ttf", "cmex10", 51),
    (0, "]", PIECE_REPEAT): ("base/cmex10.ttf", "cmex10", 55),
    (0, "]", PIECE_BOTTOM): ("base/cmex10.ttf", "cmex10", 53),
    (0, "(", PIECE_TOP): ("base/cmex10.ttf", "cmex10", 48),
    (0, "(", PIECE_REPEAT): ("base/cmex10.ttf", "cmex10", 66),
    (0, "(", PIECE_BOTTOM): ("base/cmex10.ttf", "cmex10", 64),
    (0, ")", PIECE_TOP): ("base/cmex10.ttf", "cmex10", 49),
    (0, ")", PIECE_REPEAT): ("base/cmex10.ttf", "cmex10", 67),
    (0, ")", PIECE_BOTTOM): ("base/cmex10.ttf", "cmex10", 65),
}


# Characters whose math glyph is NOT at their ASCII slot in the face's own TFM.
# TeX's OT1 text encoding puts other things at these positions, so math takes
# them from elsewhere. Found by the advance cross-check below, not by guessing.
#
# (face_id, char) -> (tfm font, slot)
TFM_SLOT = {
    (0, "<"): ("cmmi10", 60),   # cmr10 slot 60 is an inverted exclamation
    (0, ">"): ("cmmi10", 62),
    (0, "|"): ("cmsy10", 106),  # cmr10 slot 124 is a double quote
    # Symbol-face glyphs are rasterised from Latin Modern Math but must carry
    # Computer Modern's metrics, because that is what MicroTeX lays out with.
    # The base surd in particular is positioned from its own depth.
    (3, "√"): ("cmsy10", 112),
    # Greek is math italic, i.e. cmmi10 -- not the symbol font. MicroTeX's
    # cmmi10 table is indexed 33..195, which is why omega lands on slot 33
    # rather than at the end of a contiguous Greek run.
    (3, "α"): ("cmmi10", 174),
    (3, "β"): ("cmmi10", 175),
    (3, "γ"): ("cmmi10", 176),
    (3, "θ"): ("cmmi10", 181),
    (3, "π"): ("cmmi10", 188),
    (3, "ω"): ("cmmi10", 33),
    (3, "ϕ"): ("cmmi10", 193),
    # Relations and operators, all cmsy10.
    (3, "∞"): ("cmsy10", 49),
    (3, "≤"): ("cmsy10", 183),
    (3, "≥"): ("cmsy10", 184),
    (3, "⋅"): ("cmsy10", 162),
    (3, "×"): ("cmsy10", 163),
    (3, "−"): ("cmsy10", 161),
    (3, "∗"): ("cmsy10", 164),
    # The TEXT-size big operators; the display cuts are variant 1 in VARIANTS.
    # cmex10's LARGERS maps 80 -> 88 and 82 -> 90.
    (3, "∑"): ("cmex10", 80),
    (3, "∫"): ("cmex10", 82),
    # Blackboard bold is msbm10, at each letter's own ASCII slot.
    (4, "R"): ("msbm10", 82),
    (4, "C"): ("msbm10", 67),
    (4, "N"): ("msbm10", 78),
    (4, "Z"): ("msbm10", 90),
    (4, "Q"): ("msbm10", 81),
}

# Above this, TFM and raster describe different GLYPHS -- i.e. the slot mapping
# is wrong. Below it they describe the same character in two fonts that merely
# disagree slightly, and the TFM wins, because MicroTeX lays out from the TFM
# and only draws from the TTF.
#
# The threshold is set from the two populations, which are well separated:
#
#   wrong slot        '<' 0.50 em, '>' 0.31, '|' 0.72
#   same character    cmmi10 'P' 0.05, msbm10 'Q' 0.11, LMM integral 0.19
#                     (the last resolved by reading the advance as width +
#                      italic, which is how OpenType usually stores it)
#
# 0.20 sits in the gap. If a future glyph lands between 0.11 and 0.31 the
# check cannot decide, and the right response is to ask the oracle which slot
# MicroTeX actually draws rather than to widen this further.
TFM_SLOT_SANITY_EM = 0.20


def tfm_metrics(face_id, ch, cp, font):
    """TeX's metrics for a character, or None if there is no mapping.

    Only ASCII by default: in these fonts a character sits at the slot equal to
    its codepoint, which is not true of Greek or the symbol set.
    """
    override = TFM_SLOT.get((face_id, ch))
    if override is not None:
        return tfm.get(override[0], override[1]), override[0], override[1]
    if font is None or cp >= 128:
        return None
    try:
        return tfm.get(font, cp), font, cp
    except KeyError:
        return None


def em_fixed(v_em):
    """em (float) -> i16 fixed point in 1/256 em, clamped to i16."""
    x = int(round(v_em * 256.0))
    return max(-32768, min(32767, x))


def make_sdf(font, ch, render_px, pad_px, max_sdf, spread_em):
    """Render one glyph; return (sdf_u8 HxW, metrics dict) or (None, metrics)."""
    # Canvas: 3 em above the baseline and 3 em below, 5 em to the right of the
    # pen. Generous because the extension font is: cmex10's display summation
    # descends 1.5 em below the baseline and its integral 2.22 em, where an
    # ordinary letter descends about 0.2. The original canvas was 3 em square
    # with the baseline at three-quarter height, leaving 0.75 em of room below
    # it, and it silently sliced the bottom off every large glyph -- the SDF
    # was built from half a sigma and the atlas looked fine until the glyph was
    # drawn next to the real one.
    canvas = render_px * 6
    img = Image.new("L", (canvas, canvas), 0)
    draw = ImageDraw.Draw(img)
    ox, oy = render_px, render_px * 3  # pen left, baseline
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

    # Ink touching the border means the glyph was clipped and every metric
    # derived below is a measurement of the crop, not of the glyph. Refuse
    # rather than emit a plausible-looking record.
    if ink_l == 0 or ink_t == 0 or ink_r >= canvas or ink_b >= canvas:
        raise SystemExit(
            "glyph %r clipped by the raster canvas: ink %d..%d x %d..%d in a "
            "%dpx canvas with the pen at (%d, %d). Enlarge the canvas."
            % (ch, ink_l, ink_r, ink_t, ink_b, canvas, ox, oy))

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
        os.path.dirname(__file__), "..", "..", "lib", "glyphstore",
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

    tfm_used = [0]
    from_cm = [0]
    drift = []
    records = []     # tuples for sorting/emit
    sdf_blob = bytearray()
    skipped = []

    for face_id in sorted(FACES):
        name, path, glyphs, rmap, tfm_font = FACES[face_id]
        # A missing face used to WARN and silently drop every glyph in it while
        # still exiting 0 -- the atlas came out short and the self-checks
        # passed. It is a hard error now.
        if path is not None and not os.path.exists(path):
            raise SystemExit(f"face {name}: font not found: {path}")
        font = ImageFont.truetype(path, args.render) if path is not None else None
        seen = set()
        for ch in glyphs:
            cp = ord(ch)
            if cp in seen:
                continue
            seen.add(cp)
            render_ch = rmap.get(ch, ch)  # blackboard etc. substitute glyph
            src = font
            # When TFM_SLOT names a Computer Modern slot for this glyph, draw
            # it from there too. Metrics and shape must come from the same
            # place or the atlas is internally inconsistent.
            override = TFM_SLOT.get((face_id, ch))
            if override is not None:
                cm = cm_font(override[0], args.render)
                if cm is not None:
                    src = cm
                    render_ch = chr(override[1])
                    from_cm[0] += 1
            if src is None:
                raise SystemExit(
                    f"face {name} U+{cp:04X}: no font to draw it from. A face "
                    f"with no nominal font needs a TFM_SLOT entry for every "
                    f"glyph.")
            sdf, m = make_sdf(src, render_ch, args.render, args.pad,
                              args.max_sdf, spread_em)
            if sdf is None or m["box"] is None:
                skipped.append((name, cp))
                continue
            adv, hgt, dep, ital = (m["advance"], m["height"], m["depth"],
                                   m["italic"])
            found = tfm_metrics(face_id, ch, cp, tfm_font)
            if found is not None:
                t, t_font, t_slot = found
                # A slot whose advance disagrees with the raster is the wrong
                # slot. Fail loudly: silently mixing one glyph's shape with
                # another's metrics is the hardest kind of bug to see.
                # An OpenType advance often already folds in what TeX calls
                # the italic correction: Latin Modern's integral measures
                # 0.6641 against cmex10's width 0.4722 + italic 0.1944 =
                # 0.6667. Accept either reading before calling the slot wrong.
                gap = min(abs(t.width - m["advance"]),
                          abs(t.width + t.italic - m["advance"]))
                if gap > TFM_SLOT_SANITY_EM:
                    raise SystemExit(
                        "%s U+%04X: %s slot %d has advance %.4f but the "
                        "raster measures %.4f -- that is a different glyph, so "
                        "the slot mapping is wrong. Add an entry to TFM_SLOT."
                        % (name, cp, t_font, t_slot, t.width, m["advance"]))
                if gap > 1.0 / 256.0:
                    # Codepoints, not characters: this report goes to a
                    # console whose encoding is not ours to choose, and a
                    # Greek letter on cp1252 would take the build down.
                    drift.append("%s U+%04X: %s slot %d advance %.4f vs TTF "
                                 "%.4f" % (name, cp, t_font, t_slot, t.width,
                                           m["advance"]))
                adv, hgt, dep, ital = t.width, t.height, t.depth, t.italic
                tfm_used[0] += 1
            off = len(sdf_blob)
            h, w = sdf.shape
            sdf_blob += sdf.tobytes()
            records.append(dict(
                face=face_id, cp=cp, variant=0, w=w, h=h, off=off,
                advance=adv, bearingX=m["bearingX"],
                height=hgt, depth=dep, italic=ital,
                boxX=m["box"]["x"], boxY=m["box"]["y"],
                boxW=m["box"]["w"], boxH=m["box"]["h"]))

    # --- size variants -----------------------------------------------------
    # Shape from the raster, metrics from the TFM. See VARIANTS above.
    for (face_id, ch, variant), (rel, tfm_font, slot) in sorted(
            VARIANTS.items(), key=lambda kv: (kv[0][0], ord(kv[0][1]), kv[0][2])):
        path = os.path.join(CM, *rel.split("/"))
        if not os.path.exists(path):
            raise SystemExit(f"variant source font missing: {path}")
        font = ImageFont.truetype(path, args.render)
        sdf, m = make_sdf(font, chr(slot), args.render, args.pad,
                          args.max_sdf, spread_em)
        if sdf is None or m["box"] is None:
            raise SystemExit(
                f"variant {ch!r} v{variant} rendered blank from {rel} slot {slot}")
        t = tfm.get(tfm_font, slot)
        off = len(sdf_blob)
        h, w = sdf.shape
        sdf_blob += sdf.tobytes()
        records.append(dict(
            face=face_id, cp=ord(ch), variant=variant, w=w, h=h, off=off,
            advance=t.width, bearingX=m["bearingX"],
            height=t.height, depth=t.depth, italic=t.italic,
            boxX=m["box"]["x"], boxY=m["box"]["y"],
            boxW=m["box"]["w"], boxH=m["box"]["h"]))
        print(f"  variant U+{ord(ch):04X} v{variant} <- {rel} slot {slot}  "
              f"tfm w={t.width:.4f} h={t.height:.4f} d={t.depth:.4f} it={t.italic:.4f}")

    # Sort by (face, codepoint, variant) for binary search. Variant last keeps
    # every size of one symbol in a contiguous run, so a chain walk is a linear
    # scan from the base record rather than a second search per step.
    records.sort(key=lambda r: (r["face"], r["cp"], r["variant"]))

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
            # Positional brace-init: the field order here must match
            # GlyphRecord exactly, and appending a field means appending here.
            f.write("  {{{face},{w},{h},{var},0x{cp:X},{off},"
                    "{adv},{bx},{ht},{dp},{it},{x},{y},{bw},{bh}}},\n".format(
                        face=r["face"], w=r["w"], h=r["h"], var=r["variant"],
                        cp=r["cp"],
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

// Records are sorted by (face, codepoint, variant), so every size of one
// symbol is a contiguous run and the base record is the first of it.
const GlyphRecord* findGlyphRecord(Face face, c32 cp) {
  const u8 f = static_cast<u8>(face);
  int lo = 0, hi = kCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const GlyphRecord& g = kGlyphs[mid];
    if (g.face == f && g.codepoint == cp && g.variant == 0) return &g;
    if (g.face < f || (g.face == f && g.codepoint < cp) ||
        (g.face == f && g.codepoint == cp && g.variant < 0)) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

const GlyphRecord* findGlyphVariant(Face face, c32 cp, u8 variant) {
  if (variant == 0) return findGlyphRecord(face, cp);
  const GlyphRecord* base = findGlyphRecord(face, cp);
  if (base == nullptr) return nullptr;
  const u8 f = static_cast<u8>(face);
  for (const GlyphRecord* g = base; g < kGlyphs + kCount; ++g) {
    if (g->face != f || g->codepoint != cp) break;
    if (g->variant == variant) return g;
  }
  return nullptr;
}

const GlyphRecord* findGlyphVariantAtLeast(Face face, c32 cp, i16 minTotalEm) {
  const GlyphRecord* base = findGlyphRecord(face, cp);
  if (base == nullptr) return nullptr;
  const u8 f = static_cast<u8>(face);
  const GlyphRecord* best = base;
  for (const GlyphRecord* g = base; g < kGlyphs + kCount; ++g) {
    if (g->face != f || g->codepoint != cp) break;
    if (g->variant >= kFirstPieceVariant) break;  // recipe pieces, not sizes
    best = g;  // remember the largest seen, for the overflow case
    if (g->height + g->depth >= minTotalEm) return g;
  }
  return best;
}

const GlyphRecord* findLargestGlyphVariant(Face face, c32 cp) {
  const GlyphRecord* base = findGlyphRecord(face, cp);
  if (base == nullptr) return nullptr;
  const u8 f = static_cast<u8>(face);
  // Walk forward through the run. Chains are 1-2 entries long, so a scan is
  // cheaper than a second binary search and cannot run away: the loop stops at
  // the first record that is not this (face, codepoint).
  const GlyphRecord* best = base;
  for (const GlyphRecord* g = base + 1; g < kGlyphs + kCount; ++g) {
    if (g->face != f || g->codepoint != cp) break;
    if (g->variant >= kFirstPieceVariant) break;  // recipe pieces, not sizes
    best = g;
  }
  return best;
}

}  // namespace statex
""")

    print(f"wrote {out}")
    if drift:
        print("  TTF/TFM advance drift beyond 1/256 em (TFM used):")
        for line in drift:
            print("    " + line)
    print(f"  metrics from TFM: {tfm_used[0]} of {len(records)} glyphs")
    print(f"  shapes redirected to Computer Modern: {from_cm[0]}")
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
