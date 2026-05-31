#ifndef STATEX_LAYOUT_H
#define STATEX_LAYOUT_H

#include "statex_arena.h"
#include "statex_box.h"
#include "statex_node.h"
#include "statex_types.h"

namespace statex {

struct LayoutResult {
  Handle rootBox;  // NO_NODE on failure
  bool ok;
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
    u8 phase;    // 0 = pre (push children), 1 = post (combine)
    float size;  // em pixel size for this node (scripts shrink it)
  };

  Handle combine(Handle atom, const Node& n, const Handle* boxOf, float size);

  Arena& _arena;
  const NodeStore& _nodes;
  BoxStore& _boxes;
  float _size;
};

}  // namespace statex

#endif  // STATEX_LAYOUT_H
