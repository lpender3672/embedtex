"""Read TeX font metrics out of MicroTeX's vendored font definitions.

Why this exists
---------------
genfont derives a glyph's advance, height and depth by measuring the *ink* of a
rasterised bitmap. For ordinary letters that is accurate enough -- measured
against MicroTeX's TFM values the advances agree to within 0.4%, which is the
1/256 em quantisation of GlyphRecord itself.

It is not accurate enough for the big glyphs in cmex10. TeX positions a big
operator with `shift = (height + depth) / 2 + axis`, taken from the TFM, and
those numbers are deliberately not the ink extent: cmex10 slot 88 (the display
summation) has TFM height 0.100 and depth 1.500, totalling 1.600 em, while its
ink measures 1.400 em tall. Using the ink would place every big operator 0.1 em
out, and the error would look like a layout bug rather than a metric one.

So for any glyph whose placement depends on its declared metrics, take the
metrics from here and only the shape from the rasteriser.

Format (lib/MicroTeX/res/font_def.res.h): each `res/font/<name>.def.cpp` holds

    METRICS_START
    <slot>, <width>, <height>, <depth>, <italic>,
    ...
    METRICS_END

with every value an em fraction of the design size.
"""
import os
import re

_HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.normpath(
    os.path.join(_HERE, "..", "..", "lib", "MicroTeX", "res", "font"))

_NUM = r"[-+]?[0-9]*\.?[0-9]+"
_ROW = re.compile(
    r"^\s*(\d+)\s*,\s*(%s)\s*,\s*(%s)\s*,\s*(%s)\s*,\s*(%s)\s*,\s*$" %
    (_NUM, _NUM, _NUM, _NUM))


class Metrics(object):
    """One glyph's TFM metrics, in em fractions of the design size."""

    __slots__ = ("width", "height", "depth", "italic")

    def __init__(self, width, height, depth, italic):
        self.width = width
        self.height = height
        self.depth = depth
        self.italic = italic

    def __repr__(self):
        return ("Metrics(w=%.6f h=%.6f d=%.6f it=%.6f)"
                % (self.width, self.height, self.depth, self.italic))


_cache = {}


def load(font):
    """All metrics for one font, as {slot: Metrics}. `font` is a bare name."""
    if font in _cache:
        return _cache[font]
    path = os.path.join(FONT_DIR, font + ".def.cpp")
    if not os.path.exists(path):
        raise IOError(
            "no TFM definition for %r at %s -- the vendored MicroTeX tree is "
            "the source of truth for metrics and it must be present" %
            (font, path))
    table = {}
    inside = False
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "METRICS_START" in line:
                inside = True
                continue
            if "METRICS_END" in line:
                break
            if not inside:
                continue
            m = _ROW.match(line)
            if m is None:
                continue
            table[int(m.group(1))] = Metrics(
                float(m.group(2)), float(m.group(3)), float(m.group(4)),
                float(m.group(5)))
    if not table:
        raise IOError("parsed no metrics from %s -- has the format changed?"
                      % path)
    _cache[font] = table
    return table


def get(font, slot):
    """One glyph's metrics. Raises rather than guessing: a silently missing
    metric would show up much later as a mysterious placement error."""
    table = load(font)
    if slot not in table:
        raise KeyError("%s has no slot %d (it has %d slots, %d..%d)"
                       % (font, slot, len(table), min(table), max(table)))
    return table[slot]


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3:
        print(get(sys.argv[1], int(sys.argv[2])))
    else:
        for name in ("cmex10", "cmsy10", "cmr10", "cmmi10", "msbm10"):
            print("%-8s %4d slots" % (name, len(load(name))))
