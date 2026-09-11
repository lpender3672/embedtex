#ifndef STATEX_RENDER_H
#define STATEX_RENDER_H

#include "statex_arena.h"
#include "statex_box.h"
#include "statex_draw.h"
#include "statex_graphics.h"
#include "statex_parser.h"
#include "statex_types.h"

namespace statex {

/**
 * Capacity knobs; all backed by the caller's scratch. Defaults are sized for an
 * embedded budget — a NodeStore+BoxStore at these caps needs ~25 KB, leaving
 * headroom in a typical scratch buffer. Raise them (and the scratch) if you
 * need larger formulas; if a formula exceeds them, render() refuses (STX-MEM-03).
 */
struct RenderCaps {
  u16 maxNodes = 512;
  u16 maxNodeChildren = 512;
  u16 maxBoxes = 512;
  u16 maxBoxChildren = 512;
  u16 maxDepth = 64;
  u16 maxOperands = 256;
};

/** Metrics of a completed render (for tests / callers that need bounds). */
struct RenderStats {
  float width;
  float height;
  float depth;
  u32 highWater;  // peak scratch bytes used (STX-MEM budget)
};

/**
 * The StaTeX public entry point (STX-API-01/02).
 *
 * The caller owns the scratch buffer; the renderer holds no heap and returns no
 * owning pointers. Each render() is fully self-contained: it resets the arena,
 * builds atoms, lays out boxes, frees the atom/parse scratch (STX-MEM-05), then
 * draws — so renders are reentrant and leave no residue. Any failure (malformed
 * input, or scratch exhaustion) is a defined refusal that resets cleanly
 * (STX-MEM-03 / STX-ERR-03); on refusal nothing is drawn.
 */
class Renderer {
 public:
  Renderer(u8* scratch, u32 scratchSize, RenderCaps caps = RenderCaps{});

  // `probe`, when non-null, records where each glyph was placed. It is a
  // diagnostic for the differential oracle -- see GlyphProbe in statex_draw.h
  // -- and does not affect what is drawn.
  ParseError render(const c32* src, int len, float sizePx, float originX,
                    float baseline, Graphics2D& g,
                    RenderStats* stats = nullptr,
                    GlyphProbe* probe = nullptr);

  /**
   * Extents only: parse and lay out, then stop. Nothing is drawn and no
   * Graphics2D is required.
   *
   * This is not the same as rendering into a Graphics2D that discards its
   * input. The draw walk calls the SDF sampler *before* it calls
   * blendCoverage, so a null backend still pays for every glyph rasterised;
   * measuring that way costs the same as drawing. Stopping after layout costs
   * roughly 6% of a render, because the sampler is ~94% of it.
   *
   * That matters wherever sizes are needed for many items and pixels for few:
   * a scrolling list must know every entry's height to lay out its scroll
   * range, and a font backend must report a glyph's metrics during text layout
   * without rasterising it (the same split as FreeType's FT_Load_Glyph vs
   * FT_Render_Glyph).
   *
   * `stats->highWater` carries the arena's cumulative high-water mark, which
   * Arena::reset() preserves by design -- so it is the layout peak only on a
   * Renderer that has never drawn.
   *
   * A formula that measures Ok can still refuse when drawn -- the draw walk
   * has its own bounds -- so callers that must not fail late should treat this
   * as necessary, not sufficient.
   */
  ParseError measure(const c32* src, int len, float sizePx,
                     RenderStats* stats);

  u32 highWater() const { return _arena.highWater(); }

 private:
  /** Shared prefix of render() and measure(): parse and lay out into `boxes`. */
  ParseError buildLayout(const c32* src, int len, float sizePx,
                         BoxStore& boxes, Handle* rootBox);

  Arena _arena;
  RenderCaps _caps;
};

}  // namespace statex

#endif  // STATEX_RENDER_H
