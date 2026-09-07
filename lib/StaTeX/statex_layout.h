#ifndef STATEX_LAYOUT_H
#define STATEX_LAYOUT_H

#include "statex_arena.h"
#include "statex_box.h"
#include "statex_node.h"
#include "statex_parser.h"
#include "statex_spacing.h"
#include "statex_style.h"
#include "statex_types.h"

namespace statex {

struct LayoutResult {
  Handle rootBox;  // NO_NODE on failure
  bool ok;
  // Why, when !ok. STX-ERR-02 wants a single refusal reason reaching the
  // public API, so layout reports in the same currency as the parser rather
  // than collapsing everything to OutOfMemory.
  ParseError error = ParseError::Ok;
};

/**
 * Lays out an atom tree into a box tree (STX-DAT-03/04). The traversal is an
 * iterative post-order over an explicit work-stack allocated from the arena
 * (STX-EXE-01/02), so call-stack depth is bounded (STX-EXE-03) and any
 * exhaustion converges on a single refusal (STX-MEM-03).
 *
 * The metric model is deterministic (em-fraction constants * pixel size). It is
 * intentionally simple pending the MicroTeX differential oracle; scripts/sqrt
 * use approximate positioning.
 */
class Layout {
 public:
  Layout(Arena& arena, const NodeStore& nodes, BoxStore& boxes, float sizePx);

  LayoutResult run(Handle rootAtom);

 private:
  struct WorkItem {
    Handle atom;
    u8 phase;         // 0 = pre (push children), 1 = post (combine)
    float size;       // em pixel size, = _size * styleSizeFactor(style)
    TexStyle style;   // TeX's math style for this node
  };

  Handle combine(Handle atom, const Node& n, const Handle* boxOf, float size,
                 TexStyle style);

  /**
   * Build a delimiter taller than any single glyph by stacking its extensible
   * recipe: the top piece, as many repeat tiles as it takes to span
   * `minTotalPx`, then the bottom piece. Returns NO_NODE when `ch` has no
   * recipe, so the caller can fall back to the largest single variant.
   *
   * Pieces abut: consecutive baselines are separated by the upper piece's
   * depth plus the lower piece's height, which is what makes the seams
   * invisible. The result is centred on the math axis like any delimiter.
   */
  Handle makeExtensibleDelimiter(c32 ch, float size, float minTotalPx,
                                 float axis);


  // Set by combine() when it refuses for a reason more specific than
  // exhaustion; reset at the start of each run().
  ParseError _failure = ParseError::Ok;

  Arena& _arena;
  const NodeStore& _nodes;
  BoxStore& _boxes;
  float _size;
};

}  // namespace statex

#endif  // STATEX_LAYOUT_H
