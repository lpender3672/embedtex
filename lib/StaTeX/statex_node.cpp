#include "statex_node.h"

namespace statex {

NodeStore::NodeStore(Arena& arena, u16 maxNodes, u16 maxChildren)
    : _ok(false),
      _nodes(nullptr),
      _cap(maxNodes),
      _count(0),
      _children(nullptr),
      _childCap(maxChildren),
      _childCount(0) {
  _nodes = arena.allocArray<Node>(maxNodes);
  _children = arena.allocArray<Handle>(maxChildren);
  _ok = (_nodes != nullptr) && (_children != nullptr);
}

Handle NodeStore::makeChar(c32 ch, AtomType type) {
  return makeChar(ch, type, defaultMathFace(ch), false);
}

Handle NodeStore::makeChar(c32 ch, AtomType type, Face face,
                           bool takesLimits) {
  if (!_ok || _count >= _cap) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Char;
  n.atomType = type;
  n.face = face;
  n.takesLimits = takesLimits;
  n.ch = ch;
  return h;
}

Handle NodeStore::makeFrac(Handle num, Handle den, bool rule) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (!valid(num) || !valid(den)) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Frac;
  n.takesLimits = false;
  n.atomType = AtomType::Inner;
  n.frac = Node::FracData{num, den, rule};
  return h;
}

Handle NodeStore::makeScript(Handle base, Handle sup, Handle sub) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (!valid(base)) return NO_NODE;
  // At least one script must be present, else this node is meaningless.
  if (!valid(sup) && !valid(sub)) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Script;
  n.takesLimits = false;
  // A scripted atom keeps its base's spacing class -- `\sum_{i}^{n}` is still
  // a big operator and must be spaced as one, not as an Ordinary. Mirrors
  // ScriptsAtom::leftType/rightType (lib/MicroTeX/atom/atom_basic.h:564-570).
  n.atomType = _nodes[base].atomType;
  n.script = Node::ScriptData{base, sup, sub};
  return h;
}

Handle NodeStore::makeSqrt(Handle base, Handle index) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (!valid(base)) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Sqrt;
  n.takesLimits = false;
  n.atomType = AtomType::Ordinary;
  n.sqrt = Node::SqrtData{base, index};
  return h;
}

Handle NodeStore::makeRow(const Handle* items, u16 count) {
  if (!_ok || _count >= _cap) return NO_NODE;
  // All children must be valid (propagate prior exhaustion).
  for (u16 i = 0; i < count; ++i) {
    if (!valid(items[i])) return NO_NODE;
  }
  if (count > _childCap - _childCount) return NO_NODE;
  const u16 first = _childCount;
  for (u16 i = 0; i < count; ++i) {
    _children[_childCount++] = items[i];
  }
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Row;
  n.takesLimits = false;
  n.atomType = AtomType::Ordinary;
  n.children = Span{first, count};
  return h;
}

Handle NodeStore::makeMatrix(u16 rows, u16 cols, MatrixEnv env,
                             const Handle* cells, u16 count) {
  if (!_ok || _count >= _cap) return NO_NODE;
  // Compare in a width that cannot overflow. `rows * cols` truncated to u16
  // let (256, 256) past this guard with count 0, producing a node claiming a
  // 65536-cell grid over an empty span -- cell() and cellAt() would then read
  // off the end of the child buffer.
  const u32 product = static_cast<u32>(rows) * static_cast<u32>(cols);
  if (product != static_cast<u32>(count)) return NO_NODE;
  // A grid with no rows or no columns is not a matrix. Without this, any
  // (n, 0) pair satisfies the product check with count 0 and yields a node
  // claiming n rows over an empty span.
  if (rows == 0 || cols == 0) return NO_NODE;
  for (u16 i = 0; i < count; ++i) {
    if (!valid(cells[i])) return NO_NODE;
  }
  if (count > _childCap - _childCount) return NO_NODE;
  const u16 first = _childCount;
  for (u16 i = 0; i < count; ++i) _children[_childCount++] = cells[i];
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Matrix;
  n.takesLimits = false;
  n.atomType = AtomType::Inner;
  n.matrix = Node::MatrixData{rows, cols, env, Span{first, count}};
  return h;
}

}  // namespace statex
