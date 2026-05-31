#ifndef STATEX_BOX_H
#define STATEX_BOX_H

#include "statex_arena.h"
#include "statex_types.h"

namespace statex {

/** Class identity of a layout box (STX-TYP-02; replaces Box RTTI). */
enum class BoxKind : u8 {
  None = 0,
  Char,   // a single glyph
  HList,  // horizontal list of children
  VList,  // vertical list of children (positioned by child shift)
  Rule,   // a filled rectangle (e.g. fraction bar)
};

/**
 * A POD layout box (STX-DAT-01). Metrics are in pixels. `shift` is the vertical
 * offset of this box relative to its parent's baseline (positive = up). For a
 * VList, children are positioned purely by their own `shift`. `scale` is the
 * font scale applied to a Char's glyph (1.0 at full size; <1 for scripts).
 */
struct Box {
  BoxKind kind;
  float width;
  float height;  // extent above baseline
  float depth;   // extent below baseline
  float shift;   // vertical offset from parent baseline (+up)
  float scale;   // glyph scale (Char)
  union {
    c32 ch;         // Char
    Span children;  // HList / VList (indices into box child buffer)
  };
};

/** Fixed-capacity, arena-backed store of boxes (STX-MEM-02). */
class BoxStore {
 public:
  BoxStore(Arena& arena, u16 maxBoxes, u16 maxChildren);
  bool ok() const { return _ok; }

  Handle makeChar(c32 ch, float w, float h, float d, float scale);
  Handle makeRule(float w, float h, float d);
  /** Build a list box with explicit metrics; copies child handles. */
  Handle makeList(BoxKind kind, const Handle* items, u16 count, float w,
                  float h, float d);

  const Box& get(Handle b) const { return _boxes[b]; }
  Box& get(Handle b) { return _boxes[b]; }
  Handle child(Handle listHandle, u16 i) const {
    return _children[_boxes[listHandle].children.first + i];
  }
  u16 count() const { return _count; }

 private:
  Handle make(BoxKind kind);
  bool _ok;
  Box* _boxes;
  u16 _cap;
  u16 _count;
  Handle* _children;
  u16 _childCap;
  u16 _childCount;
};

}  // namespace statex

#endif  // STATEX_BOX_H
