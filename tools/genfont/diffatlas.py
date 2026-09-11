#!/usr/bin/env python3
"""Compare two generated glyph atlases record by record.

`cmp` answers the wrong question. Two atlases differ byte-for-byte the moment
the blob is written in a different order or a single glyph is added, even when
every glyph they share is pixel-identical -- and the check that matters when
the symbol suite grows from 248 records to ~2000 is precisely "did any glyph
that already existed move?".

So this parses both files into {(face, codepoint, variant): (metrics, sdf
bytes)} and compares on that, which is invariant to blob ordering and to
glyphs being added or removed.

    python tools/genfont/diffatlas.py OLD.cpp NEW.cpp
    python tools/genfont/diffatlas.py OLD.cpp NEW.cpp --common-only

Exit status is 1 if any shared glyph differs, so it works as a build gate.
"""

from __future__ import annotations

import argparse
import io
import re
import sys

FIELDS = ["face", "sdfW", "sdfH", "variant", "codepoint", "sdfOffset",
          "advance", "bearingX", "height", "depth", "italic",
          "boxX", "boxY", "boxW", "boxH"]
METRIC_FIELDS = ["sdfW", "sdfH", "advance", "bearingX", "height", "depth",
                 "italic", "boxX", "boxY", "boxW", "boxH"]


def parse(path: str) -> dict:
    """{(face, cp, variant): {field: value, 'sdf': bytes}} from a .gen.cpp."""
    text = io.open(path, encoding="utf-8", errors="replace").read()

    m = re.search(r"const u8 kSdf\[\][^=]*=\s*\{(.*?)\n\};", text, re.S)
    if m is None:
        raise SystemExit("%s: no kSdf blob found" % path)
    blob = bytes(int(v, 0) for v in re.findall(r"-?\d+", m.group(1)))

    m = re.search(r"const GlyphRecord kGlyphs\[\][^=]*=\s*\{(.*?)\n\};",
                  text, re.S)
    if m is None:
        raise SystemExit("%s: no kGlyphs table found" % path)

    out = {}
    for row in re.finditer(r"\{([^{}]*)\}", m.group(1)):
        vals = [v.strip() for v in row.group(1).split(",")]
        if len(vals) != len(FIELDS):
            raise SystemExit("%s: record has %d fields, expected %d -- "
                             "GlyphRecord changed shape?"
                             % (path, len(vals), len(FIELDS)))
        r = {k: int(v, 0) for k, v in zip(FIELDS, vals)}
        n = r["sdfW"] * r["sdfH"]
        r["sdf"] = blob[r["sdfOffset"]:r["sdfOffset"] + n]
        if len(r["sdf"]) != n:
            raise SystemExit("%s: record U+%04X runs past the blob"
                             % (path, r["codepoint"]))
        out[(r["face"], r["codepoint"], r["variant"])] = r
    return out


def describe(key) -> str:
    face, cp, var = key
    return "face %d U+%04X%s" % (face, cp, (" v%d" % var) if var else "")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--common-only", action="store_true",
                    help="do not report glyphs added or removed, only changes "
                         "to glyphs present in both")
    ap.add_argument("--max-report", type=int, default=20)
    args = ap.parse_args()

    a, b = parse(args.old), parse(args.new)
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    common = sorted(set(a) & set(b))

    print("old: %d glyphs   new: %d glyphs   shared: %d"
          % (len(a), len(b), len(common)))
    if not args.common_only:
        print("removed: %d   added: %d" % (len(only_a), len(only_b)))
        for k in only_a[:args.max_report]:
            print("  removed %s" % describe(k))
        for k in only_b[:args.max_report]:
            print("  added   %s" % describe(k))
        if len(only_b) > args.max_report:
            print("  ... and %d more added" % (len(only_b) - args.max_report))

    metric_diff, pixel_diff = [], []
    for k in common:
        ra, rb = a[k], b[k]
        changed = [f for f in METRIC_FIELDS if ra[f] != rb[f]]
        if changed:
            metric_diff.append((k, changed, ra, rb))
        if ra["sdf"] != rb["sdf"]:
            pixel_diff.append(k)

    print()
    print("shared glyphs with changed metrics: %d" % len(metric_diff))
    for k, changed, ra, rb in metric_diff[:args.max_report]:
        detail = ", ".join("%s %d->%d" % (f, ra[f], rb[f]) for f in changed)
        print("  %s: %s" % (describe(k), detail))
    if len(metric_diff) > args.max_report:
        print("  ... and %d more" % (len(metric_diff) - args.max_report))

    print("shared glyphs with changed pixels:  %d" % len(pixel_diff))
    for k in pixel_diff[:args.max_report]:
        ra, rb = a[k], b[k]
        worst = max((abs(x - y) for x, y in zip(ra["sdf"], rb["sdf"])),
                    default=0)
        n = sum(1 for x, y in zip(ra["sdf"], rb["sdf"]) if x != y)
        print("  %s: %d of %d bytes differ, worst delta %d"
              % (describe(k), n, len(ra["sdf"]), worst))
    if len(pixel_diff) > args.max_report:
        print("  ... and %d more" % (len(pixel_diff) - args.max_report))

    if metric_diff or pixel_diff:
        print("\nVERDICT: %d shared glyph(s) changed"
              % len({k for k, _, _, _ in metric_diff} | set(pixel_diff)))
        return 1
    print("\nVERDICT: every shared glyph is identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
