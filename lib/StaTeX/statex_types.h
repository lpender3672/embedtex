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
