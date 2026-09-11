#!/usr/bin/env python3
"""Build tools/genfont/symbols.tsv -- the symbol table, joined once.

StaTeX keys glyphs by (Face, codepoint); MicroTeX keys them by (fontId, slot).
Reconciling the two used to be done by hand, which is why genfont.py carried 25
hand-written TFM_SLOT entries and 34 hand-written VARIANTS entries. At 600
symbols that does not scale, so the join happens here, once, and the result is
checked in.

Everything but unicode-math-table.tex is vendored in this repo, so regenerating
symbols.tsv needs no TeX installation -- the same reasoning that removed the old
Latin Modern dependency. The unicode-math join only fills in codepoints that
MicroTeX's own tables do not already give, and its results are captured in the
checked-in file.

    python tools/genfont/mksymbols.py [--unicode-math PATH] [--out PATH]

Sources
-------
res/sym/*.def.cpp          E(fontId, slot, name)         name -> where to draw
res/reg/builtin_font_reg.cpp  REG_FONT order             fontId -> font name
res/font/<name>.def.cpp    DEF_FONT(name, path, ...)     font name -> TTF
res/builtin/tex_symbols.res.cpp   sym()/del()            name -> AtomType
res/builtin/formula_mappings.res.cpp                     name -> codepoint
res/builtin/symbol_mapping.res.cpp                       name -> codepoint
unicode-math-table.tex (optional)                        name -> codepoint
"""

from __future__ import annotations

import argparse
import collections
import io
import os
import re
import sys

import tfm

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
RES = os.path.join(REPO, "lib", "MicroTeX", "res")

DEFAULT_UNICODE_MATH = os.path.join(
    os.environ.get("LOCALAPPDATA", ""), "Programs", "MiKTeX", "tex", "latex",
    "unicode-math", "unicode-math-table.tex")

# MicroTeX registers symbol files in this order and later registrations
# overwrite earlier ones (`_symbolMappings[name] = ...`). Replicated here so a
# name defined twice resolves the same way we do and MicroTeX does.
SYM_ORDER = ["amsfonts", "amssymb", "base", "symspecial", "stmaryrd"]

# AtomType spelling in MicroTeX -> the enumerator in statex_node.h.
# `acc` has no target: StaTeX has no accent machinery, and an accent placed as
# an ordinary atom renders as a hat floating on the baseline. Excluded outright
# so the command fails cleanly rather than drawing something wrong.
ATOM = {
    "ord": "Ordinary",
    "op": "BigOp",
    "bin": "BinaryOp",
    "rel": "Relation",
    "open": "Opening",
    "close": "Closing",
    "punct": "Punctuation",
}

# TeX defines \int as \intop\nolimits (and \oint, \smallint likewise): the
# limits sit beside the operator, not above and below it. Every other big
# operator takes limits in display style.
NO_LIMITS = {"int", "oint", "smallint"}

# Private-use block for symbols with no Unicode codepoint. These are an
# internal lookup key only -- nothing outside this repo ever sees one (see
# statex_serialize.h: the serializer is a test fixture), so reassigning one
# later is a regeneration, not a format break.
PUA_BASE = 0xE000

# Every named symbol lives on the one math-symbol face. It is assembled from 15
# source fonts and keyed by codepoint; that is what makes a face enumerator
# unnecessary here. Alphabet faces (\mathsf, \mathfrak) are a different
# problem -- there the *same* ASCII letter needs a different design.
SYMBOL_FACE = 3


def read(path: str) -> str:
    return io.open(path, encoding="utf-8", errors="replace").read()


def parse_symbols() -> tuple[dict, list]:
    """name -> (fontId, slot), following MicroTeX's registration order."""
    out, dups = {}, []
    for stem in SYM_ORDER:
        path = os.path.join(RES, "sym", stem + ".def.cpp")
        if not os.path.isfile(path):
            raise SystemExit("missing symbol file: %s" % path)
        for m in re.finditer(r"E\((\d+),\s*(\d+),\s*(\w+)\)", read(path)):
            name, entry = m.group(3), (int(m.group(1)), int(m.group(2)))
            if name in out and out[name] != entry:
                dups.append((name, out[name], entry, stem))
            out[name] = entry
    return out, dups


def parse_fonts() -> dict:
    """fontId -> (def-file name, TTF path relative to the font root).

    fontIds are *positional*: the index of REG_FONT(x) in the registration
    list. Nothing writes the number down, which is why this has to be derived
    rather than transcribed.

    Keyed by def-file name, not by TTF: cmmi10_unchanged, cmti10_unchanged,
    r10_unchanged and moustache each share a TTF with another font but carry a
    different metrics table, so collapsing them would silently pick up the
    wrong widths.
    """
    reg = read(os.path.join(RES, "reg", "builtin_font_reg.cpp"))
    order = [m.group(1) for m in re.finditer(r"REG_FONT\((\w+)\)", reg)]
    if not order:
        raise SystemExit("parsed no REG_FONT entries -- has the format changed?")

    path_of = {}
    for f in os.listdir(os.path.join(RES, "font")):
        if not f.endswith(".def.cpp"):
            continue
        m = re.search(r"DEF_FONT\((\w+),\s*([^,]+),",
                      read(os.path.join(RES, "font", f)))
        if m:
            path_of[m.group(1)] = m.group(2).strip()

    out = {}
    for i, name in enumerate(order):
        if name in path_of:
            # DEF_FONT paths are "fonts/<sub>/<file>.ttf" and that subtree is
            # exactly the layout of tests/oracle/fonts, so the mapping is a
            # prefix swap rather than a table.
            rel = path_of[name]
            assert rel.startswith("fonts/"), rel
            out[i] = (name, rel[len("fonts/"):])
    return out


def parse_atomtypes() -> dict:
    """name -> (AtomType spelling, is-delimiter)."""
    txt = read(os.path.join(RES, "builtin", "tex_symbols.res.cpp"))
    out = {}
    for m in re.finditer(r"\b(sym|del)\((\w+),\s*(\w+)\)", txt):
        out[m.group(3)] = (m.group(2), m.group(1) == "del")
    return out


def parse_offline_codepoints() -> dict:
    """name -> codepoint, from MicroTeX's own vendored tables.

    formula_mappings.res.cpp is the productive one: it maps a codepoint to the
    LaTeX that produces it, and where that LaTeX is a bare control word the
    mapping inverts cleanly.
    """
    out = {}
    fm = read(os.path.join(RES, "builtin", "formula_mappings.res.cpp"))
    for m in re.finditer(r'\{\s*(\d+)\s*,\s*"' + "\\\\" * 2 + r'(\w+)"\s*\}', fm):
        out.setdefault(m.group(2), int(m.group(1)))
    sm = read(os.path.join(RES, "builtin", "symbol_mapping.res.cpp"))
    for m in re.finditer(r"""\{\s*(?:'(\\?.)'|(\d+))\s*,\s*"([^"]+)"\s*\}""", sm):
        cp = ord(m.group(1).lstrip("\\")) if m.group(1) else int(m.group(2))
        out.setdefault(m.group(3), cp)
    return out


def parse_unicode_math(path: str) -> dict:
    r"""name -> codepoint from \UnicodeMathSymbol{"0221A}{\sqrt}{\mathop}{...}.

    The trailing description field also records alternative spellings as
    "/name", which is where a few AMS names live that are not the primary
    entry -- \centerdot is U+00B7's "/centerdot", not an entry of its own.
    Primary names win over those.
    """
    if not path or not os.path.isfile(path):
        return {}
    primary, alias = {}, {}
    pat = (r'\\UnicodeMathSymbol\{"([0-9A-Fa-f]+)\}\{\\(\w+)\s*\}'
           r'\{\\(\w+)\}\{([^}]*)\}')
    for m in re.finditer(pat, read(path)):
        cp = int(m.group(1), 16)
        primary.setdefault(m.group(2), cp)
        for a in re.findall(r"/(\w+)", m.group(4)):
            alias.setdefault(a, cp)
    for name, cp in alias.items():
        primary.setdefault(name, cp)
    return primary


# The codepoints the existing atlas is already built around. MicroTeX's own
# tables reproduce all fourteen; unicode-math does not (it has no bare \alpha),
# which is why the offline tables are the primary source and unicode-math only
# fills gaps. Asserted rather than trusted: if a regeneration ever moves one of
# these, every one of the 248 existing glyph records moves with it and the
# byte-identical check that guards the generator rewrite is gone.
# symbol_mapping.res.cpp is a *reverse* map -- codepoint to name, used to turn a
# typed character into a symbol -- so reading it as name-to-codepoint is right
# for `plus` and `comma` but wrong wherever MicroTeX's slot draws the math
# glyph rather than the ASCII one. cmsy10 slot 164 is U+2217 ASTERISK OPERATOR,
# which sits on the math axis; the ASCII asterisk sits at cap height. StaTeX
# already maps `*` to U+2217 (mathModeGlyph), so without this override \ast
# would get a second, identical record at U+002A.
OVERRIDE = {
    "ast": 0x2217,
}

# Names whose source font cannot draw them, with the evidence.
#
# The vendored TTFs are addressed by raw slot -- their cmap maps slot n to
# codepoint n -- and that holds for every Computer Modern face. special.ttf
# does not honour it: it carries eight distinct glyphs, maps 216 of its 256
# slots to a single .notdef, and aliases slots 101 and 109 to one glyph. The
# shared glyph's ink has no descender (0.01 em measured), which matches
# texteuro's declared metrics (h 0.68, d 0.013) and not textmu's (h 0.45,
# d 0.20) -- so it is the euro sign, and \textmu was drawing it.
#
# Found by looking at a contact sheet, not by any assertion: the glyph is not
# blank, its advance matches its TFM to 0.05 em, and it resolves through every
# table correctly. genfont now refuses two used slots of one font that
# rasterise identically with different metrics, so this cannot recur silently.
BROKEN = {
    "textmu": "special.ttf aliases slot 109 to slot 101, so it draws a euro",
}

PINNED = {
    "alpha": 0x03B1, "beta": 0x03B2, "cdot": 0x22C5, "gamma": 0x03B3,
    "geq": 0x2265, "infty": 0x221E, "int": 0x222B, "leq": 0x2264,
    "omega": 0x03C9, "phi": 0x03D5, "pi": 0x03C0, "sum": 0x2211,
    "theta": 0x03B8, "times": 0x00D7,
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--unicode-math", default=DEFAULT_UNICODE_MATH,
                    help="unicode-math-table.tex (optional; fills codepoints "
                         "MicroTeX's tables do not give)")
    ap.add_argument("--out", default=os.path.join(HERE, "symbols.tsv"))
    args = ap.parse_args()

    sym, dups = parse_symbols()
    fonts = parse_fonts()
    atoms = parse_atomtypes()
    offline = parse_offline_codepoints()
    umath = parse_unicode_math(args.unicode_math)

    print("names in res/sym:            %d" % len(sym))
    print("names with an AtomType:      %d" % len(set(sym) & set(atoms)))
    print("unicode-math entries:        %d" % len(umath))
    for name, first, second, stem in dups:
        print("  note: %r defined twice %s then %s; %s wins (MicroTeX order)"
              % (name, first, second, stem))

    # --- select ------------------------------------------------------------
    rows, dropped = {}, collections.Counter()
    for name, (font_id, slot) in sym.items():
        at = atoms.get(name)
        if at is None:
            dropped["no AtomType"] += 1
            continue
        if at[0] == "acc":
            dropped["accent (no accent layout yet)"] += 1
            continue
        if name in BROKEN:
            dropped["source font cannot draw it"] += 1
            continue
        if at[0] not in ATOM:
            dropped["unmapped AtomType %s" % at[0]] += 1
            continue
        if font_id not in fonts:
            dropped["fontId %d has no DEF_FONT" % font_id] += 1
            continue
        # A few delimiters have no single-glyph design at all: their metrics
        # row declares zero height and zero depth and exists only to carry an
        # EXTENSIONS recipe, so the glyph is always stacked from pieces.
        # \lmoustache and \rmoustache are the whole of it. Rasterising their
        # slot draws whatever else lives at that position in the shared TTF --
        # which is how the advance cross-check found them. They need the
        # \left/\right path rather than a glyph, so they wait for it.
        font_name = fonts[font_id][0]
        try:
            m = tfm.get(font_name, slot)
        except KeyError:
            dropped["no metrics row"] += 1
            continue
        if m.height == 0 and m.depth == 0 and \
                tfm.extensions(font_name).get(slot) is not None:
            dropped["assembly-only, needs the delimiter path"] += 1
            continue
        rows[name] = dict(name=name, font_id=font_id, slot=slot,
                          atom=ATOM[at[0]], delim=at[1])

    # --- codepoints --------------------------------------------------------
    for name, r in rows.items():
        if name in OVERRIDE:
            r["cp"], r["src"] = OVERRIDE[name], "override"
        elif name in offline:
            r["cp"], r["src"] = offline[name], "microtex"
        elif name in umath:
            r["cp"], r["src"] = umath[name], "unicode-math"
        else:
            r["cp"], r["src"] = None, "pua"

    # --- collisions --------------------------------------------------------
    # Two names on one codepoint is fine when they are aliases for the same
    # glyph (\to and \rightarrow are literally the same cmsy10 slot). It is not
    # fine when the glyphs differ: \hbar and \hslash are distinct designs, and
    # one key cannot address both. The name unicode-math itself assigns to that
    # codepoint keeps it; the other is moved to the private-use block.
    by_cp = collections.defaultdict(list)
    for name, r in rows.items():
        if r["cp"] is not None:
            by_cp[r["cp"]].append(name)

    aliases = demoted = rehomed = 0
    claimed = {r["cp"] for r in rows.values() if r["cp"] is not None}
    for cp, names in sorted(by_cp.items()):
        if len(names) < 2:
            continue
        if len({(rows[n]["font_id"], rows[n]["slot"]) for n in names}) == 1:
            aliases += 1
            continue
        keep = None
        for n in sorted(names):
            if umath.get(n) == cp:
                keep = n
                break
        if keep is None:
            keep = sorted(names)[0]
        for n in sorted(names):
            if n == keep:
                continue
            # Prefer the codepoint unicode-math gives this name over a
            # private-use one: \checkmark losing U+2720 to a collision is no
            # reason to deny it U+2713, which is the character it actually is.
            alt = umath.get(n)
            if alt is not None and alt not in claimed:
                rows[n]["cp"], rows[n]["src"] = alt, "unicode-math"
                claimed.add(alt)
                rehomed += 1
            else:
                rows[n]["cp"], rows[n]["src"] = None, "pua"
                demoted += 1

    # --- private use -------------------------------------------------------
    # Assigned in sorted-name order so the allocation is reproducible: the same
    # inputs always produce the same file.
    need_pua = sorted(n for n, r in rows.items() if r["cp"] is None)
    taken = {r["cp"] for r in rows.values() if r["cp"] is not None}
    nxt = PUA_BASE
    for name in need_pua:
        while nxt in taken:
            nxt += 1
        rows[name]["cp"] = nxt
        taken.add(nxt)
        nxt += 1

    # --- self-checks -------------------------------------------------------
    bad = ["%s: got U+%04X, want U+%04X" % (n, rows[n]["cp"], want)
           for n, want in sorted(PINNED.items())
           if n in rows and rows[n]["cp"] != want]
    if bad:
        raise SystemExit(
            "codepoints moved for symbols the existing atlas is built around:\n"
            "  " + "\n  ".join(bad) + "\n"
            "Every one of the 248 existing glyph records would move with them.")
    seen = {}
    for name, r in sorted(rows.items()):
        key = r["cp"]
        if key in seen and (rows[seen[key]]["font_id"], rows[seen[key]]["slot"]) \
                != (r["font_id"], r["slot"]):
            raise SystemExit(
                "U+%04X names two different glyphs (%s and %s) -- the collision "
                "policy did not resolve it" % (key, seen[key], name))
        seen[key] = name

    # --- report ------------------------------------------------------------
    src = collections.Counter(r["src"] for r in rows.values())
    print()
    print("emitted:                     %d" % len(rows))
    for k, v in sorted(dropped.items()):
        print("  dropped %-34s %d" % (k + ":", v))
    print("codepoints from MicroTeX:    %d" % src["microtex"])
    print("codepoints from unicode-math:%d" % src["unicode-math"])
    print("codepoints in private use:   %d" % src["pua"])
    print("codepoints from an override:  %d" % src["override"])
    print("alias collisions (same glyph, kept):  %d" % aliases)
    print("real collisions rehomed via unicode-math: %d" % rehomed)
    print("real collisions demoted to PUA:       %d" % demoted)

    # --- emit --------------------------------------------------------------
    cols = ["name", "codepoint", "cp_source", "atomtype", "takes_limits",
            "delimiter", "face", "tfm_font", "slot", "ttf", "unicode_math"]
    with io.open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# GENERATED by tools/genfont/mksymbols.py -- do not edit.\n")
        f.write("# Joined from lib/MicroTeX/res (vendored) and "
                "unicode-math-table.tex.\n")
        f.write("# codepoint is StaTeX's lookup key, not a claim about the "
                "character's identity;\n"
                "# cp_source=pua means no Unicode codepoint names this glyph.\n"
                "# unicode_math is filled in only where unicode-math-table.tex "
                "disagrees with the\n"
                "# chosen codepoint. MicroTeX's tables win because they are "
                "what the existing\n"
                "# atlas was built from; the column records the alternative so "
                "switching is a\n"
                "# one-column edit rather than an archaeology exercise.\n")
        f.write("\t".join(cols) + "\n")
        for name in sorted(rows):
            r = rows[name]
            font_name, ttf = fonts[r["font_id"]]
            limits = r["atom"] == "BigOp" and name not in NO_LIMITS
            f.write("\t".join([
                name,
                "0x%04X" % r["cp"],
                r["src"],
                r["atom"],
                "1" if limits else "0",
                "1" if r["delim"] else "0",
                str(SYMBOL_FACE),
                font_name,
                str(r["slot"]),
                ttf,
                ("0x%04X" % umath[name]) if (name in umath
                                             and umath[name] != r["cp"]) else "",
            ]) + "\n")

    print("\nwrote %s (%d symbols)" % (args.out, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
