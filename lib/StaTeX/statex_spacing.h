#ifndef STATEX_SPACING_H
#define STATEX_SPACING_H

// TeX's inter-atom spacing (The TeXbook, Chapter 18, p.181).
//
// A math list is not just its glyphs butted together: TeX inserts glue between
// adjacent atoms according to their two spacing classes, which is what makes
// `a + b` read as arithmetic and `ab` read as a product. StaTeX's parser has
// always classified atoms (Node::atomType) but layout never consulted it --
// defect ATOM-SPACING.
//
// The table is transcribed verbatim from lib/MicroTeX/core/glue.cpp:40-49,
// which is itself the TeXbook table. Keeping the four-character strings rather
// than pre-reducing them means this file can be diffed against the source
// line by line, and it keeps the display/script columns that the style
// dimension (plan Phase 2) will need.
//
//       ORD   OP    BIN   REL   OPEN  CLOSE  PUNCT  INNER
//  ORD    0     1    (2)   (3)    0     0      0     (1)
//  OP     1     1     *    (3)    0     0      0     (1)
//  BIN   (2)   (2)    *     *    (2)    *      *     (2)
//  REL   (3)   (3)    *     0    (3)    0      0     (3)
//  OPEN   0     0     *     0     0     0      0      0
//  CLOSE  0     1    (2)   (3)    0     0      0     (1)
//  PUNCT (1)   (1)    *    (1)   (1)   (1)    (1)    (1)
//  INNER (1)    1    (2)   (3)   (1)    0     (1)    (1)
//
// 0 none, 1 thin, 2 medium, 3 thick. Parenthesised entries apply in display
// and text style only, not in script or scriptscript -- which is why each
// cell carries four digits rather than one. `*` cases cannot arise: a Bin
// atom is demoted to Ord unless it sits between two operands (see
// `demotesBinaryOperator` below), so BIN-BIN and friends are unreachable.

#include "statex_node.h"
#include "statex_types.h"

namespace statex {

/** Style classes, in the order the table's four digits are written. */
enum class StyleClass : u8 {
  Display = 0,
  Text = 1,
  Script = 2,
  ScriptScript = 3,
};

constexpr int kSpacingTypes = 8;  // Ordinary..Inner; None folds to Ordinary

constexpr char kInterAtomGlue[kSpacingTypes][kSpacingTypes][5] = {
    /* ORD   */ {"0000", "1111", "2200", "3300", "0000", "0000", "0000", "1100"},
    /* OP    */ {"1111", "1111", "0000", "3300", "0000", "0000", "0000", "1100"},
    /* BIN   */ {"2200", "2200", "0000", "0000", "2200", "0000", "0000", "2200"},
    /* REL   */ {"3300", "3300", "0000", "0000", "3300", "0000", "0000", "3300"},
    /* OPEN  */ {"0000", "0000", "0000", "0000", "0000", "0000", "0000", "0000"},
    /* CLOSE */ {"0000", "1111", "2200", "3300", "0000", "0000", "0000", "1100"},
    /* PUNCT */ {"1100", "1100", "0000", "1100", "1100", "1100", "1100", "1100"},
    /* INNER */ {"1100", "1111", "2200", "3300", "1100", "0000", "1100", "1100"},
};

/**
 * 1 mu = quad/18, where quad comes from the font marked "mufontid" --
 * cmsy10, whose quad is 1.000003 em (lib/MicroTeX/res/font/cmsy10.def.cpp:5).
 * So one mu is an eighteenth of the em, to within a rounding error far below
 * a pixel at any size StaTeX renders.
 */
constexpr float kMuPerEm = 1.0f / 18.0f;

/**
 * The table's digits are glue *classes*, not widths. Class 1 is a thin space
 * of 3mu, class 2 a medium space of 4mu, class 3 a thick space of 5mu --
 * lib/MicroTeX/core/glue.cpp:7-12, whose rows are {space, stretch, shrink} in
 * mu. StaTeX sets rigid glue, so only the first column is used: nothing here
 * stretches or shrinks, because there is no line breaking to drive it.
 */
constexpr u8 kGlueClassMu[4] = {0, 3, 4, 5};

/** Anything past Inner (i.e. None) is spaced as an Ordinary, per TeX. */
constexpr int spacingIndex(AtomType t) {
  return static_cast<u8>(t) > static_cast<u8>(AtomType::Inner)
             ? 0
             : static_cast<int>(static_cast<u8>(t));
}

/**
 * Width of the glue TeX would set between a `left` atom and a `right` one, as
 * a fraction of the em. Zero means the two atoms touch.
 */
constexpr float interAtomGlueEm(AtomType left, AtomType right,
                                StyleClass style) {
  const char code = kInterAtomGlue[spacingIndex(left)][spacingIndex(right)]
                                  [static_cast<int>(style)];
  return static_cast<float>(kGlueClassMu[code - '0']) * kMuPerEm;
}

/**
 * TeX demotes a Bin atom to Ord when it cannot be read as a binary operator:
 * when nothing precedes it, or what precedes it is itself an operator, a
 * relation, an opening delimiter or punctuation. That is what makes the minus
 * in `-x` unary and the plus in `(+1)` a sign rather than an addition.
 *
 * Mirrors RowAtom::_binSet (lib/MicroTeX/atom/atom_row.cpp:49-54).
 */
constexpr bool demotesBinaryOperator(AtomType previous) {
  return previous == AtomType::BinaryOp || previous == AtomType::BigOp ||
         previous == AtomType::Relation || previous == AtomType::Opening ||
         previous == AtomType::Punctuation;
}

}  // namespace statex

#endif  // STATEX_SPACING_H
