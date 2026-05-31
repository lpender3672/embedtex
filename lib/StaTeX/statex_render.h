#ifndef STATEX_RENDER_H
#define STATEX_RENDER_H

#include "statex_arena.h"
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

  ParseError render(const c32* src, int len, float sizePx, float originX,
                    float baseline, Graphics2D& g,
                    RenderStats* stats = nullptr);

  u32 highWater() const { return _arena.highWater(); }

 private:
  Arena _arena;
  RenderCaps _caps;
};

}  // namespace statex

#endif  // STATEX_RENDER_H
