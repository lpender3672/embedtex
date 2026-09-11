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
up to three blocks this module reads.

    METRICS_START
    <slot>, <width>, <height>, <depth>, <italic>,
    ...
    METRICS_END

    LARGERS_START
    <slot>, <larger slot>, <font id of the larger glyph>,
    ...
    LARGERS_END

    EXTENSIONS_START
    <slot>, <top>, <middle>, <repeat>, <bottom>,      -1 where a piece is absent
    ...
    EXTENSIONS_END

with every metric value an em fraction of the design size. LARGERS is TeX's
chain of purpose-cut size steps for a growing delimiter; EXTENSIONS is the
recipe for stacking one taller than any single cut.

Note the last row of every block is written *without* a trailing comma. Reading
it is not optional: cmsy10's final row is slot 199 and cmex10's is slot 196,
both of which real symbols use, and cmex10 196 is named by two EXTENSIONS
recipes. An earlier version of this parser required the comma and silently
dropped one slot per font.
"""
import os
import re

_HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.normpath(
    os.path.join(_HERE, "..", "..", "lib", "MicroTeX", "res", "font"))

_NUM = r"[-+]?[0-9]*\.?[0-9]+"


def _row(n):
    """A block row of `n` numeric fields, with the trailing comma optional."""
    return re.compile(r"^\s*(\d+)\s*,\s*" +
                      r"\s*,\s*".join(["(%s)" % _NUM] * (n - 1)) +
                      r"\s*,?\s*$")


_ROW_METRICS = _row(5)
_ROW_LARGERS = _row(3)
_ROW_EXTENSIONS = _row(5)


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


class Larger(object):
    """The next size up for one slot, possibly in a different font."""

    __slots__ = ("slot", "font_id")

    def __init__(self, slot, font_id):
        self.slot = slot
        self.font_id = font_id

    def __repr__(self):
        return "Larger(slot=%d font=%d)" % (self.slot, self.font_id)


class Extension(object):
    """The pieces TeX stacks to build an arbitrarily tall delimiter.

    `middle` is -1 for most delimiters but not all: cmex10 slots 56 and 57 --
    the big braces -- do have one, which is why there is a kPieceMiddle.
    """

    __slots__ = ("top", "middle", "repeat", "bottom")

    def __init__(self, top, middle, repeat, bottom):
        self.top = top
        self.middle = middle
        self.repeat = repeat
        self.bottom = bottom

    def __repr__(self):
        return ("Extension(top=%d mid=%d rep=%d bot=%d)"
                % (self.top, self.middle, self.repeat, self.bottom))


def _blocks(path):
    """Split a def.cpp into {block name: [raw lines]}.

    One pass over the file collecting every block, because the blocks appear in
    a fixed order and stopping at the first END would hide the later ones.
    """
    out, name = {}, None
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if name is None:
                m = re.search(r"(METRICS|LARGERS|EXTENSIONS)_START", line)
                if m:
                    name = m.group(1)
                    out[name] = []
                continue
            if re.search(r"(METRICS|LARGERS|EXTENSIONS)_END", line):
                name = None
                continue
            out[name].append(line)
    return out


_cache = {}


def _load_all(font):
    if font in _cache:
        return _cache[font]
    path = os.path.join(FONT_DIR, font + ".def.cpp")
    if not os.path.exists(path):
        raise IOError(
            "no TFM definition for %r at %s -- the vendored MicroTeX tree is "
            "the source of truth for metrics and it must be present" %
            (font, path))
    blocks = _blocks(path)

    metrics = {}
    for line in blocks.get("METRICS", []):
        m = _ROW_METRICS.match(line)
        if m:
            metrics[int(m.group(1))] = Metrics(
                float(m.group(2)), float(m.group(3)), float(m.group(4)),
                float(m.group(5)))
    if not metrics:
        raise IOError("parsed no metrics from %s -- has the format changed?"
                      % path)

    largers = {}
    for line in blocks.get("LARGERS", []):
        m = _ROW_LARGERS.match(line)
        if m:
            largers[int(m.group(1))] = Larger(int(float(m.group(2))),
                                              int(float(m.group(3))))

    extensions = {}
    for line in blocks.get("EXTENSIONS", []):
        m = _ROW_EXTENSIONS.match(line)
        if m:
            extensions[int(m.group(1))] = Extension(
                int(float(m.group(2))), int(float(m.group(3))),
                int(float(m.group(4))), int(float(m.group(5))))

    # A row the regex fails to match is dropped silently, which is how the
    # missing-trailing-comma bug survived. Count what the file declares and
    # insist we read all of it.
    for kind, table in (("METRICS", metrics), ("LARGERS", largers),
                        ("EXTENSIONS", extensions)):
        declared = sum(1 for ln in blocks.get(kind, []) if ln.strip())
        if declared != len(table):
            raise IOError(
                "%s: read %d of %d %s rows -- a row did not match the expected "
                "format" % (path, len(table), declared, kind))

    _cache[font] = (metrics, largers, extensions)
    return _cache[font]


def load(font):
    """All metrics for one font, as {slot: Metrics}. `font` is a bare name."""
    return _load_all(font)[0]


def largers(font):
    """Size-step chain for one font, as {slot: Larger}. Empty if it has none."""
    return _load_all(font)[1]


def extensions(font):
    """Extensible recipes for one font, as {slot: Extension}."""
    return _load_all(font)[2]


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
        for name in ("cmex10", "cmsy10", "cmr10", "cmmi10", "msbm10",
                     "stmary10"):
            print("%-10s %4d slots  %3d largers  %3d extensions"
                  % (name, len(load(name)), len(largers(name)),
                     len(extensions(name))))
