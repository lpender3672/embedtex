#include "statex_draw.h"

#include "statex_glyphstore.h"
#include "statex_sdf.h"

namespace statex {
namespace {

struct Item {
  Handle h;
  float x;
  float baseline;
};

constexpr int kChildTmp = 256;
constexpr int kCovCap = kMaxGlyphCoveragePx;  // see statex_draw.h

}  // namespace

bool drawTree(Arena& arena, const BoxStore& boxes, Handle root, float x,
              float baseline, Graphics2D& g, GlyphProbe* probe) {
  if (!valid(root)) return false;

  const u32 cap = static_cast<u32>(boxes.count()) + 8u;
  Item* stack = arena.allocArray<Item>(cap);
  u8* cov = arena.allocArray<u8>(kCovCap);
  if (stack == nullptr || cov == nullptr) return false;

  u32 sp = 0;
  // The root's own shift counts too. Child shifts are applied where children
  // are pushed, so a shifted box that happened to BE the root -- a bare `\sum`,
  // which is centred on the axis rather than sat on the baseline -- was drawn
  // as though it had no shift at all, while the same glyph inside a row was
  // placed correctly.
  stack[sp++] = Item{root, x, baseline - boxes.get(root).shift};

  while (sp > 0) {
    const Item it = stack[--sp];
    const Box& b = boxes.get(it.h);
    switch (b.kind) {
      case BoxKind::Char: {
        // The EXACT variant layout chose. This used to ask for "the largest"
        // whenever the variant was non-zero, which happened to be right while
        // only big operators had variants (they have exactly one) and became
        // wrong the moment radicals got a four-entry chain: a formula measured
        // with variant 1 was drawn with variant 4.
        const GlyphRecord* rec = findGlyphVariant(b.face, b.ch, b.variant);
        if (rec == nullptr) break;  // missing glyph: draw nothing
        // Recorded before the coverage bitmap is built, so a glyph that is
        // placed but renders to nothing (a space, a sub-pixel mark) still
        // counts as placed. What layout decided and what the sampler managed
        // to ink are different questions and this probe answers the first.
        if (probe != nullptr) {
          if (probe->count < probe->cap && probe->out != nullptr) {
            probe->out[probe->count] =
                GlyphPlacement{b.ch, b.face, b.variant, it.x, it.baseline, b.emPx};
          }
          probe->count++;
        }
        const int emPx = static_cast<int>(b.emPx + 0.5f);
        GlyphCoverage gc{};
        if (!renderGlyphCoverage(*rec, glyphSdfData(), glyphSdfSpread(), emPx,
                                 cov, kCovCap, &gc)) {
          return false;  // oversized glyph: refuse (STX-MEM-03)
        }
        if (gc.w > 0 && gc.h > 0) {
          const int gx = static_cast<int>(it.x + emUnits(rec->boxX, b.emPx) + 0.5f);
          const int gy =
              static_cast<int>(it.baseline - emUnits(rec->boxY, b.emPx) + 0.5f);
          g.blendCoverage(gx, gy, gc.w, gc.h, cov);
        }
        break;
      }

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
