#ifndef STATEX_DRAW_H
#define STATEX_DRAW_H

#include "statex_arena.h"
#include "statex_box.h"
#include "statex_graphics.h"
#include "statex_types.h"

namespace statex {

/**
 * Pixel budget for one glyph's coverage bitmap. Published so layout can refuse
 * an oversized glyph *before* anything is drawn: the draw walk emits as it
 * goes, so a refusal discovered there would leave part of a formula on the
 * panel (STX-MEM-03 says a refusal has no side effects).
 */
constexpr int kMaxGlyphCoveragePx = 160 * 160;

/** Where the draw walk put one glyph, in the caller's device coordinates. */
struct GlyphPlacement {
  c32 ch;
  Face face;
  u8 variant;      // which size variant layout chose; 0 is the text-size cut
  float penX;      // origin the glyph is set from, before its bearing
  float baseline;  // baseline it sits on
  float emPx;      // em size it is set at
};

/**
 * Optional placement recorder for drawTree.
 *
 * Why this exists: the differential oracle can only compare two renders as
 * pixels, and pixels conflate *where a glyph was placed* with *how the
 * rasteriser inked it*. StaTeX samples an SDF atlas and the reference uses
 * FreeType's hinted outlines, and those disagree by 3-7% of ink mass on an
 * identically placed glyph -- enough to sink an image-similarity score for a
 * layout that is exactly right. Recording the pen positions lets a test assert
 * the thing layout is actually responsible for, and name the glyph that moved
 * instead of reporting a number about the whole picture.
 *
 * `count` is the number of glyphs the walk placed, which may exceed `cap`; in
 * that case only the first `cap` were written and the record is truncated.
 * Costs one null check per glyph when unused, which is what the device pays.
 */
struct GlyphProbe {
  GlyphPlacement* out = nullptr;
  u16 cap = 0;
  u16 count = 0;

  bool truncated() const { return count > cap; }
};

/**
 * Walk a box tree and emit draw primitives to `g`, placing the root with its
 * baseline-left origin at (x, baseline). Iterative over an explicit work-stack
 * in the arena (STX-EXE-03) — no recursion proportional to box-tree depth.
 *
 * Emission order is pre-order, left-to-right (painter order). Returns false on
 * work-stack exhaustion (STX-MEM-03).
 *
 * `probe`, when non-null, receives one entry per glyph placed, in emission
 * order. It never changes what is drawn.
 */
bool drawTree(Arena& arena, const BoxStore& boxes, Handle root, float x,
              float baseline, Graphics2D& g, GlyphProbe* probe = nullptr);

}  // namespace statex

#endif  // STATEX_DRAW_H
