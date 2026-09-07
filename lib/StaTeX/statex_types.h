#ifndef STATEX_TYPES_H
#define STATEX_TYPES_H

// Fixed-width integer aliases and core value types for StaTeX.
// No heap, no exceptions, no RTTI, no STL — see docs/StaTeX-requirements.md.

#include <cstdint>
#include <cstddef>

namespace statex {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using c32 = char32_t;

/** Type face (STX-FNT-05). Lives here so nodes, the glyph store, layout, and
 *  draw can all share it without coupling. */
enum class Face : u8 {
  Roman = 0,
  Italic = 1,
  Bold = 2,
  Symbol = 3,
  Blackboard = 4,
};

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
