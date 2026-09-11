#ifndef GLYPHSTORE_TYPES_H
#define GLYPHSTORE_TYPES_H

// The primitives shared by the glyph store and by StaTeX proper.
//
// This is the whole of what a font needs to describe itself: fixed-width
// integer aliases, a codepoint, and the face a glyph belongs to. Everything
// that interprets those -- TeX's rule that letters are italic in math mode,
// node handles, spans -- is StaTeX semantics and lives in statex_types.h.
//
// Keeping the line here is what lets lib/glyphstore build and link with no
// parser and no layout (STX-MEM-01), so a font adapter can consume glyphs
// without dragging in the renderer.

#include <cstddef>
#include <cstdint>

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

}  // namespace statex

#endif  // GLYPHSTORE_TYPES_H
