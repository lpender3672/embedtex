#include "statex_box.h"

namespace statex {

BoxStore::BoxStore(Arena& arena, u16 maxBoxes, u16 maxChildren)
    : _ok(false),
      _boxes(nullptr),
      _cap(maxBoxes),
      _count(0),
      _children(nullptr),
      _childCap(maxChildren),
      _childCount(0) {
  _boxes = arena.allocArray<Box>(maxBoxes);
  _children = arena.allocArray<Handle>(maxChildren);
  _ok = (_boxes != nullptr) && (_children != nullptr);
}

Handle BoxStore::make(BoxKind kind) {
  if (!_ok || _count >= _cap) return NO_NODE;
  const Handle h = _count++;
  Box& b = _boxes[h];
  b.kind = kind;
  b.face = Face::Roman;
  b.width = b.height = b.depth = b.shift = 0.0f;
  b.emPx = 0.0f;
  return h;
}

Handle BoxStore::makeChar(c32 ch, Face face, float emPx, float w, float h,
                          float d) {
  const Handle bh = make(BoxKind::Char);
  if (!valid(bh)) return NO_NODE;
  Box& b = _boxes[bh];
  b.face = face;
  b.emPx = emPx;
  b.width = w;
  b.height = h;
  b.depth = d;
  b.ch = ch;
  return bh;
}

Handle BoxStore::makeRule(float w, float h, float d) {
  const Handle bh = make(BoxKind::Rule);
  if (!valid(bh)) return NO_NODE;
  Box& b = _boxes[bh];
  b.width = w;
  b.height = h;
  b.depth = d;
  return bh;
}

Handle BoxStore::makeList(BoxKind kind, const Handle* items, u16 count, float w,
                          float h, float d) {
  if (!_ok || _count >= _cap) return NO_NODE;
  for (u16 i = 0; i < count; ++i) {
    if (!valid(items[i])) return NO_NODE;
  }
  if (count > _childCap - _childCount) return NO_NODE;
  const u16 first = _childCount;
  for (u16 i = 0; i < count; ++i) _children[_childCount++] = items[i];
  const Handle bh = make(kind);
  if (!valid(bh)) return NO_NODE;
  Box& b = _boxes[bh];
  b.width = w;
  b.height = h;
  b.depth = d;
  b.children = Span{first, count};
  return bh;
}

}  // namespace statex
