#include "statex_draw.h"

namespace statex {
namespace {

struct Item {
  Handle h;
  float x;
  float baseline;
};

constexpr int kChildTmp = 256;

}  // namespace

bool drawTree(Arena& arena, const BoxStore& boxes, Handle root, float x,
              float baseline, Graphics2D& g) {
  if (!valid(root)) return false;

  const u32 cap = static_cast<u32>(boxes.count()) + 8u;
  Item* stack = arena.allocArray<Item>(cap);
  if (stack == nullptr) return false;

  u32 sp = 0;
  stack[sp++] = Item{root, x, baseline};

  while (sp > 0) {
    const Item it = stack[--sp];
    const Box& b = boxes.get(it.h);
    switch (b.kind) {
      case BoxKind::Char:
        g.drawGlyph(b.ch, it.x, it.baseline, b.scale);
        break;

      case BoxKind::Rule:
        g.drawRule(it.x, it.baseline - b.height, b.width, b.height + b.depth);
        break;

      case BoxKind::HList:
      case BoxKind::VList: {
        const u16 n = b.children.count;
        if (n > kChildTmp) return false;
        Item tmp[kChildTmp];
        float penX = it.x;
        for (u16 i = 0; i < n; ++i) {
          const Handle ch = boxes.child(it.h, i);
          const Box& cb = boxes.get(ch);
          float cx;
          if (b.kind == BoxKind::HList) {
            cx = penX;
            penX += cb.width;
          } else {  // VList: center children horizontally
            cx = it.x + (b.width - cb.width) * 0.5f;
          }
          tmp[i] = Item{ch, cx, it.baseline - cb.shift};
        }
        // Push reverse so children emit left-to-right (pre-order).
        if (sp + n > cap) return false;
        for (int i = n - 1; i >= 0; --i) stack[sp++] = tmp[i];
        break;
      }

      default:
        return false;
    }
  }
  return true;
}

}  // namespace statex
