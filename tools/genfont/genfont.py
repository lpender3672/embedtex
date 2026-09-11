#!/usr/bin/env python3
"""StaTeX offline glyph generator (STX-FNT-04).

Rasterizes glyphs with Pillow's bundled FreeType, computes a signed distance
field per glyph (scipy), downsamples + encodes to 8-bit, derives em-normalized
metrics, and emits a constexpr C++ glyph store (statex_glyphs.gen.cpp) matching
lib/glyphstore/statex_glyphstore.h.

Env note: needs Pillow, numpy, scipy (all present in the project's Python).
freetype-py is NOT required -- Pillow bundles FreeType. Source fonts are the
Computer Modern TTFs vendored at tests/oracle/fonts, the same files the
differential oracle rasterises; nothing is read from the system font directory.

Which glyphs exist is not written down here. The Symbol face and every size
chain come from tools/genfont/symbols.tsv and MicroTeX's own metric tables --
see mksymbols.py -- so adding a symbol is a data change, not a code change.

Usage:
    python tools/genfont/genfont.py [--out PATH] [--render 256] [--max-sdf 48]
                                    [--symbols all|legacy]
"""
import argparse
import io
import os
import sys

import numpy as np
import mksymbols
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

# The Symbol face is no longer a hand-kept list. It comes from symbols.tsv,
# which tools/genfont/mksymbols.py joins out of MicroTeX's own tables, so a
# symbol's codepoint, source font and slot are all read rather than typed.
SYMBOLS_TSV = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "symbols.tsv")

# Glyphs the Symbol face needs that are not *named* symbols: the radical is
# reached through \sqrt, which is structural syntax, so it has no entry in
# res/sym and would otherwise drop out of the atlas.
STRUCTURAL = [
    (0x221A, "cmsy10", 112),
]

# The Symbol face before symbols.tsv existed. Kept only so `--symbols legacy`
# can regenerate exactly the old selection and prove the table-driven path
# changes no glyph; nothing else reads it.
#
# ASCII `-` and `*` are set from the symbol font in math mode, not from the
# text face: TeX's mathcode sends them to family 2. The text hyphen is 0.332em
# where the math minus is 0.778em, so using the wrong one misplaces everything
# after it.
LEGACY_SYMBOL_CPS = [ord(c) for c in
                     "√∑∫⋅×≤≥∞πθωαβγϕ−∗"]

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
    # Filled in from symbols.tsv by main(); see load_symbol_face.
    3: ("Symbol", None, [], {}, None),
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
# fontId -> (metrics table name, TTF path), derived rather than transcribed.
# MicroTeX numbers its fonts *positionally* -- a fontId is the index of
# REG_FONT(x) in res/reg/builtin_font_reg.cpp -- and each font's DEF_FONT line
# names its TTF under a "fonts/" subtree that is byte-for-byte the layout of
# tests/oracle/fonts. So the whole mapping is a lookup plus a prefix swap, and
# the six-entry table that used to live here is gone.
#
# Keyed by metrics table name, not by TTF: cmmi10_unchanged, cmti10_unchanged,
# r10_unchanged and moustache each share a TTF with another font while carrying
# a different METRICS block, and collapsing them picks up the wrong widths.
_FONT_BY_ID = mksymbols.parse_fonts()
TFM_FONT_FILE = {name: rel for name, rel in _FONT_BY_ID.values()}
_FONT_NAME_BY_ID = {i: name for i, (name, _) in _FONT_BY_ID.items()}


def truetype(path, px):
    """Open a font with Pillow's BASIC layout engine.

    Not cosmetic. These TTFs are addressed by raw slot -- `chr(slot)` -- and
    when Pillow is built with RAQM it runs the string through HarfBuzz, which
    applies Unicode semantics to what is really a font position. cmex10 slot
    173 is the casualty: chr(173) is U+00AD SOFT HYPHEN, which HarfBuzz drops
    as an invisible formatting character, so the glyph rasterises blank and
    with a zero advance. It is one of the size steps for a growing angle
    bracket. BASIC maps the codepoint through the cmap and draws it.
    """
    return ImageFont.truetype(path, px,
                              layout_engine=ImageFont.Layout.BASIC)


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
        _font_cache[key] = truetype(path, render_px)
    return _font_cache[key]


# Must match kFirstPieceVariant in lib/glyphstore/statex_glyphstore.h.
# A middle piece is rare but real: cmex10 slots 56 and 57, the big braces, have
# one, so the recipe is not always just top/repeat/bottom.
PIECE_TOP, PIECE_MIDDLE, PIECE_REPEAT, PIECE_BOTTOM = 64, 65, 66, 67

def _chain(font, slot):
    """Walk one glyph's LARGERS chain, then its extensible recipe.

    TeX does not enlarge a symbol by scaling it: Computer Modern ships
    purpose-cut glyphs at each size and LARGERS links them. Past the last cut
    TeX stops choosing and starts BUILDING, stacking the pieces an EXTENSIONS
    row names.

    The join between the two is the subtle part. A chain's final link points at
    the slot that *owns* the extensible recipe, so following it would add the
    recipe's anchor as one more size step -- and since a repeat tile is about
    0.6 em, the "smallest that fits" walk would then prefer it to the 2.4 em
    bracket it exists to extend. Terminating the walk at a slot with an
    EXTENSIONS entry is what stops that, and it reproduces exactly where the
    hand-written table used to stop.

    Returns {variant: (metrics table name, slot)}.
    """
    out = {}
    variant, seen = 1, set()
    f, sl = font, slot
    while True:
        lg = tfm.largers(f).get(sl)
        if lg is None or (f, sl) in seen:
            break
        seen.add((f, sl))
        nxt = _FONT_NAME_BY_ID.get(lg.font_id)
        if nxt is None:
            break
        if tfm.extensions(nxt).get(lg.slot) is not None:
            f, sl = nxt, lg.slot
            break
        if variant >= PIECE_TOP:
            raise SystemExit(
                "%s slot %d: size chain reached variant %d, which collides "
                "with the extensible piece range starting at %d"
                % (font, slot, variant, PIECE_TOP))
        out[variant] = (nxt, lg.slot)
        f, sl, variant = nxt, lg.slot, variant + 1

    ext = tfm.extensions(f).get(sl)
    if ext is not None:
        for label, piece in ((PIECE_TOP, ext.top), (PIECE_MIDDLE, ext.middle),
                             (PIECE_REPEAT, ext.repeat),
                             (PIECE_BOTTOM, ext.bottom)):
            if piece >= 0:
                out[label] = (f, piece)
    return out


def build_variants(base):
    """(face, codepoint) -> (tfm font, slot)  =>  variant records to emit.

    (face, codepoint, variant) -> (ttf relative to CM, tfm font name, slot)
    """
    out = {}
    for (face_id, cp), (font, slot) in sorted(base.items()):
        for variant, (vfont, vslot) in _chain(font, slot).items():
            rel = TFM_FONT_FILE.get(vfont)
            if rel is None:
                raise SystemExit(
                    "variant of U+%04X needs font %r, which has no vendored "
                    "TTF" % (cp, vfont))
            out[(face_id, cp, variant)] = (rel, vfont, vslot)
    return out



# Characters whose math glyph is NOT at their ASCII slot in the face's own TFM.
# TeX's OT1 text encoding puts other things at these positions, so math takes
# them from elsewhere. Found by the advance cross-check below, not by guessing.
#
# (face_id, char) -> (tfm font, slot)
TFM_SLOT = {
    (0, "<"): ("cmmi10", 60),   # cmr10 slot 60 is an inverted exclamation
    (0, ">"): ("cmmi10", 62),
    (0, "|"): ("cmsy10", 106),  # cmr10 slot 124 is a double quote
    # Blackboard bold is msbm10, at each letter's own ASCII slot. \mathbb is a
    # style rather than a set of named symbols, so this one stays written down.
    (4, "R"): ("msbm10", 82),
    (4, "C"): ("msbm10", 67),
    (4, "N"): ("msbm10", 78),
    (4, "Z"): ("msbm10", 90),
    (4, "Q"): ("msbm10", 81),
}


def load_symbol_face(subset):
    """The Symbol face, read from symbols.tsv.

    Returns (chars, {(3, char): (tfm font, slot)}). The nineteen entries this
    replaces were correct, but each was a slot number typed by hand from a
    table that was already in the repo.
    """
    if not os.path.exists(SYMBOLS_TSV):
        raise SystemExit(
            "%s is missing -- run tools/genfont/mksymbols.py to build it"
            % SYMBOLS_TSV)
    entries = []
    with io.open(SYMBOLS_TSV, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or line.startswith("name\t"):
                continue
            f = line.rstrip("\n").split("\t")
            if len(f) < 10:
                continue
            entries.append((int(f[1], 16), f[7], int(f[8])))
    entries.extend(STRUCTURAL)

    if subset == "legacy":
        keep = set(LEGACY_SYMBOL_CPS)
        entries = [e for e in entries if e[0] in keep]
        missing = keep - {cp for cp, _, _ in entries}
        if missing:
            raise SystemExit("legacy subset missing: %s"
                             % ", ".join("U+%04X" % c for c in sorted(missing)))

    chars, slots, seen = [], {}, set()
    for cp, font, slot in entries:
        if cp in seen:
            continue
        seen.add(cp)
        ch = chr(cp)
        chars.append(ch)
        slots[(3, ch)] = (font, slot)
    return chars, slots


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
    ap.add_argument("--symbols", choices=("all", "legacy"), default="all",
                    help="'all' takes the Symbol face from symbols.tsv; "
                         "'legacy' restricts it to the set that predated that "
                         "file, so the table-driven path can be proved to "
                         "change no glyph")
    args = ap.parse_args()

    spread_em = args.spread_em

    # The Symbol face and every size chain are read, not written down.
    sym_chars, sym_slots = load_symbol_face(args.symbols)
    name, path, _, rmap, tfm_font = FACES[3]
    FACES[3] = (name, path, sym_chars, rmap, tfm_font)
    TFM_SLOT.update(sym_slots)

    # Base slots for every glyph the atlas will hold, so a size chain can be
    # looked for behind each of them. Faces 0-2 address their letters at the
    # ASCII slot of the character itself; everything else is redirected.
    base_slots = {}
    for face_id in sorted(FACES):
        _, _, glyphs, _, face_font = FACES[face_id]
        for ch in glyphs:
            hit = TFM_SLOT.get((face_id, ch))
            if hit is not None:
                base_slots[(face_id, ord(ch))] = hit
            elif face_font is not None and ord(ch) < 128:
                base_slots[(face_id, ord(ch))] = (face_font, ord(ch))
    variants = build_variants(base_slots)

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
        font = truetype(path, args.render) if path is not None else None
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
    for (face_id, cp_v, variant), (rel, tfm_font, slot) in sorted(
            variants.items()):
        ch = chr(cp_v)
        path = os.path.join(CM, *rel.split("/"))
        if not os.path.exists(path):
            raise SystemExit(f"variant source font missing: {path}")
        font = truetype(path, args.render)
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
