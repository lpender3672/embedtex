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

Handle NodeStore::makeChar(c32 ch, AtomType type, Face face) {
  if (!_ok || _count >= _cap) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Char;
  n.atomType = type;
  n.face = face;
  n.ch = ch;
  return h;
}

Handle NodeStore::makeFrac(Handle num, Handle den, bool rule) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (!valid(num) || !valid(den)) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Frac;
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
  n.atomType = AtomType::Ordinary;
  n.script = Node::ScriptData{base, sup, sub};
  return h;
}

Handle NodeStore::makeSqrt(Handle base, Handle index) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (!valid(base)) return NO_NODE;
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Sqrt;
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
  n.atomType = AtomType::Ordinary;
  n.children = Span{first, count};
  return h;
}

Handle NodeStore::makeMatrix(u16 rows, u16 cols, MatrixEnv env,
                             const Handle* cells, u16 count) {
  if (!_ok || _count >= _cap) return NO_NODE;
  if (count != static_cast<u16>(rows * cols)) return NO_NODE;
  for (u16 i = 0; i < count; ++i) {
    if (!valid(cells[i])) return NO_NODE;
  }
  if (count > _childCap - _childCount) return NO_NODE;
  const u16 first = _childCount;
  for (u16 i = 0; i < count; ++i) _children[_childCount++] = cells[i];
  const Handle h = _count++;
  Node& n = _nodes[h];
  n.kind = Kind::Matrix;
  n.atomType = AtomType::Inner;
  n.matrix = Node::MatrixData{rows, cols, env, Span{first, count}};
  return h;
}

}  // namespace statex
