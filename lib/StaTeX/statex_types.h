#ifndef STATEX_TYPES_H
#define STATEX_TYPES_H

// TeX semantics over the shared primitives: how a character behaves in math
// mode, plus the handle and span types the node store is built from.
// No heap, no exceptions, no RTTI, no STL -- see docs/StaTeX-requirements.md.
//
// The integer aliases, c32 and Face live one layer down in lib/glyphstore, so
// the font can be built and linked without the parser or layout.

#include "glyphstore_types.h"

namespace statex {

/**
 * The face TeX's default math alphabet gives a character: letters are math
 * variables and set in italic, everything else (digits, operators,
 * delimiters) upright. An explicit \mathrm / \mathbf / \mathit / \mathbb
 * overrides this for its argument.
 *
 * Lives here so the parser, the node factories and the serializer all share
 * one statement of the rule instead of three copies that can drift.
 */
inline constexpr Face defaultMathFace(c32 c) {
  const bool alpha = (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z');
  return alpha ? Face::Italic : Face::Roman;
}

/**
 * A few ASCII characters are not themselves in math mode: TeX's `\mathcode`
 * sends them to the symbol font, and the glyph you get is a different design
 * with different metrics from the text character of the same name.
 *
 *   `-`  is not the hyphen. It is U+2212 MINUS (cmsy10 slot 161), which is
 *        0.778 em wide against the text hyphen's 0.332 -- so a formula with a
 *        minus in it was 0.445 em narrow, at every size, and everything after
 *        the minus was in the wrong place.
 *   `*`  is not the ASCII asterisk. It is U+2217 ASTERISK OPERATOR (cmsy10
 *        slot 164), which sits on the math axis rather than at cap height.
 *
 * This overrides an explicit \mathrm or \mathbf, and should: the character's
 * mathcode names family 2 (the symbol font) outright, so the surrounding
 * alphabet does not get a say. Returns the character unchanged when there is
 * no such mapping, so callers can apply it unconditionally.
 */
inline constexpr c32 mathModeGlyph(c32 c) {
  switch (c) {
    case U'-': return 0x2212;
    case U'*': return 0x2217;
    default: return c;
  }
}

/** True when `mathModeGlyph` would send this character to the symbol font. */
inline constexpr bool isMathModeSymbol(c32 c) { return mathModeGlyph(c) != c; }

/**
 * A handle is a u16 index into a node store, used instead of a pointer
 * (STX-DAT-01). The all-ones value is reserved to mean "no node".
 */
using Handle = u16;
constexpr Handle NO_NODE = static_cast<Handle>(0xFFFF);

inline constexpr bool valid(Handle h) { return h != NO_NODE; }

/**
 * A Span references a contiguous run inside an arena-backed buffer:
 * `count` elements starting at index `first`. Used for child lists and
 * string slices (STX-DAT-02/03) so nodes embed no owning containers.
 */
struct Span {
  u16 first = 0;
  u16 count = 0;
};

}  // namespace statex

#endif  // STATEX_TYPES_H
