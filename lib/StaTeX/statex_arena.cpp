#include "statex_arena.h"

#include <cstdint>

namespace statex {

Arena::Arena(u8* buffer, u32 capacity)
    : _buf(buffer), _cap(capacity), _top(0), _high(0) {}

void* Arena::alloc(u32 size, u32 align) {
  if (align == 0) align = 1;

  // Align the *address* (not just the offset) so returned pointers honor
  // alignof(T) regardless of where the buffer itself sits in memory.
  const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(_buf);
  const std::uintptr_t cur = base + _top;
  const std::uintptr_t aligned =
      (cur + (align - 1)) & ~static_cast<std::uintptr_t>(align - 1);
  const u32 pad = static_cast<u32>(aligned - cur);

  // Capacity checks ordered to avoid unsigned overflow (_top <= _cap always).
  if (pad > _cap - _top) return nullptr;           // padding alone overflows
  const u32 afterPad = _top + pad;
  if (size > _cap - afterPad) return nullptr;      // body does not fit

  _top = afterPad + size;
  if (_top > _high) _high = _top;
  return reinterpret_cast<void*>(aligned);
}

void Arena::reset() {
  _top = 0;  // high-water mark intentionally preserved
}

}  // namespace statex
