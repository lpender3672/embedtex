#ifndef STATEX_DRAW_H
#define STATEX_DRAW_H

#include "statex_arena.h"
#include "statex_box.h"
#include "statex_graphics.h"
#include "statex_types.h"

namespace statex {

/**
 * Walk a box tree and emit draw primitives to `g`, placing the root with its
 * baseline-left origin at (x, baseline). Iterative over an explicit work-stack
 * in the arena (STX-EXE-03) — no recursion proportional to box-tree depth.
 *
 * Emission order is pre-order, left-to-right (painter order). Returns false on
 * work-stack exhaustion (STX-MEM-03).
 */
bool drawTree(Arena& arena, const BoxStore& boxes, Handle root, float x,
              float baseline, Graphics2D& g);

}  // namespace statex

#endif  // STATEX_DRAW_H
