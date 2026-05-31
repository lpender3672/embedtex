#ifndef STATEX_NODE_H
#define STATEX_NODE_H

#include "statex_arena.h"
#include "statex_types.h"

namespace statex {

/**
 * Class identity of an atom node (STX-TYP-02). This is the tag that replaces
 * MicroTeX's RTTI/dynamic_cast dispatch. It is distinct from AtomType, which
 * carries TeX *spacing* semantics only.
 */
enum class Kind : u8 {
  None = 0,
  Char,    // a single character / symbol
  Row,     // ordered horizontal list of children
  Frac,    // numerator over denominator (with or without rule)
  Script,  // base with optional superscript and/or subscript
  Sqrt,    // radical with optional index
  Matrix,  // rows x cols grid of cells (bounded — STX-DAT-04)
};

/** Matrix environment delimiter style. */
enum class MatrixEnv : u8 {
  Plain = 0,  // matrix  (no delimiters)
  Bracket,    // bmatrix [ ]
  Paren,      // pmatrix ( )
};

/** TeX inter-atom spacing class (kept from MicroTeX's AtomType semantics). */
enum class AtomType : u8 {
  Ordinary = 0,
  BigOp,
  BinaryOp,
  Relation,
  Opening,
  Closing,
  Punctuation,
  Inner,
  None,
};

/**
 * A POD atom node (STX-DAT-01): trivially destructible, no owning members.
 * Child references are u16 handles; lists are Spans into the store's child
 * buffer. Nodes live in a NodeStore array and are addressed by Handle.
 */
struct Node {
  Kind kind;
  AtomType atomType;
  Face face;  // meaningful for Kind::Char (STX-FNT-05); Roman otherwise

  struct FracData {
    Handle num;
    Handle den;
    bool rule;
  };
  struct ScriptData {
    Handle base;
    Handle sup;  // NO_NODE if absent
    Handle sub;  // NO_NODE if absent
  };
  struct SqrtData {
    Handle base;
    Handle index;  // NO_NODE if absent
  };
  struct MatrixData {
    u16 rows;
    u16 cols;
    MatrixEnv env;
    Span cells;  // row-major, rows*cols handles into the child buffer
  };

  union {
    c32 ch;          // Kind::Char
    Span children;   // Kind::Row  (indices into NodeStore child buffer)
    FracData frac;   // Kind::Frac
    ScriptData script;  // Kind::Script
    SqrtData sqrt;   // Kind::Sqrt
    MatrixData matrix;  // Kind::Matrix
  };
};

/**
 * Fixed-capacity store of nodes, backed by arena memory (STX-MEM-02).
 *
 * All factories return NO_NODE on exhaustion, and composite factories return
 * NO_NODE if any required child is NO_NODE — so allocation failure deep in a
 * tree propagates up to a single refusal (STX-MEM-03 / STX-EXE-02), rather
 * than producing a half-built tree.
 */
class NodeStore {
 public:
  NodeStore(Arena& arena, u16 maxNodes, u16 maxChildren);

  /** True if the backing arrays were allocated. If false, all factories refuse. */
  bool ok() const { return _ok; }

  Handle makeChar(c32 ch, AtomType type = AtomType::Ordinary,
                  Face face = Face::Roman);
  Handle makeFrac(Handle num, Handle den, bool rule = true);
  Handle makeScript(Handle base, Handle sup, Handle sub);
  Handle makeSqrt(Handle base, Handle index = NO_NODE);
  /** Copies `count` child handles into the child buffer. */
  Handle makeRow(const Handle* items, u16 count);
  /** Builds a rows x cols matrix; copies row-major cell handles. */
  Handle makeMatrix(u16 rows, u16 cols, MatrixEnv env, const Handle* cells,
                    u16 count);

  const Node& get(Handle h) const { return _nodes[h]; }
  Node& get(Handle h) { return _nodes[h]; }

  /** i-th child of a Row node (by the Row's handle). */
  Handle child(Handle rowHandle, u16 i) const {
    const Node& n = _nodes[rowHandle];
    return _children[n.children.first + i];
  }

  /** Cell (r,c) of a Matrix node, row-major. */
  Handle cell(Handle matrixHandle, u16 r, u16 c) const {
    const Node& n = _nodes[matrixHandle];
    return _children[n.matrix.cells.first + r * n.matrix.cols + c];
  }
  /** k-th cell of a Matrix node in row-major order. */
  Handle cellAt(Handle matrixHandle, u16 k) const {
    const Node& n = _nodes[matrixHandle];
    return _children[n.matrix.cells.first + k];
  }

  u16 count() const { return _count; }
  u16 childCount() const { return _childCount; }

 private:
  bool _ok;
  Node* _nodes;
  u16 _cap;
  u16 _count;
  Handle* _children;
  u16 _childCap;
  u16 _childCount;
};

}  // namespace statex

#endif  // STATEX_NODE_H
