#ifndef STATEX_ARENA_H
#define STATEX_ARENA_H

#include "statex_types.h"

namespace statex {

/**
 * A bump allocator over a fixed, caller-owned byte buffer (STX-MEM-02).
 *
 * - No heap: the buffer is provided by the caller (static/BSS on target).
 * - Exhaustion returns nullptr instead of crashing (STX-MEM-03): the render
 *   request then unwinds to a refusal. A failed alloc does NOT consume space.
 * - reset() frees everything in O(1) by rewinding the top (STX-MEM-04). It is
 *   valid precisely because everything stored here is POD (STX-DAT-01).
 * - No individual free (STX-MEM-06).
 */
class Arena {
 public:
  Arena(u8* buffer, u32 capacity);

  /**
   * Allocate `size` bytes aligned to `align` (must be a power of two).
   * Returns a pointer into the buffer, or nullptr if it does not fit.
   * Memory is left uninitialized.
   */
  void* alloc(u32 size, u32 align);

  /**
   * Allocate storage for `n` objects of type T (uninitialized, not constructed).
   * Returns nullptr on exhaustion or on size overflow.
   */
  template <typename T>
  T* allocArray(u32 n) {
    if (n != 0 && static_cast<u32>(sizeof(T)) > (0xFFFFFFFFu / n)) {
      return nullptr;  // size_t overflow guard
    }
    return static_cast<T*>(alloc(n * static_cast<u32>(sizeof(T)),
                                 static_cast<u32>(alignof(T))));
  }

  /** Allocate one uninitialized T. */
  template <typename T>
  T* allocOne() {
    return allocArray<T>(1);
  }

  /** Wholesale free: rewind to empty. High-water mark is preserved. */
  void reset();

  /** Current top, for scoped/phase rewinding (STX-MEM-05). */
  u32 mark() const { return _top; }

  /**
   * Free everything allocated since `m` (a value from mark()). O(1), POD-safe.
   * A region rewind, not per-object free (consistent with STX-MEM-06).
   */
  void rewind(u32 m) {
    if (m <= _top) _top = m;
  }

  u32 capacity() const { return _cap; }
  u32 used() const { return _top; }
  u32 remaining() const { return _cap - _top; }
  u32 highWater() const { return _high; }

 private:
  u8* _buf;
  u32 _cap;
  u32 _top;
  u32 _high;
};

}  // namespace statex

#endif  // STATEX_ARENA_H
