#include "statex_layout.h"

namespace statex {
namespace {

// Metric model, in em-fractions of the pixel size. Deterministic; simple.
constexpr float CHAR_W = 0.5f;
constexpr float CHAR_H = 0.7f;
constexpr float CHAR_D = 0.0f;
constexpr float RULE_T = 0.04f;
constexpr float FRAC_GAP = 0.15f;
constexpr float AXIS = 0.25f;
constexpr float SUP_SHIFT = 0.45f;
constexpr float SUB_SHIFT = 0.20f;
constexpr float SCRIPT_SCALE = 0.7f;
constexpr float RADICAL_W = 0.45f;
constexpr float SQRT_PAD = 0.10f;

constexpr c32 RADICAL_GLYPH = 0x221A;  // √

constexpr int kRowTmp = 256;  // max children combined in one row

inline float fmax2(float a, float b) { return a > b ? a : b; }

}  // namespace

Layout::Layout(Arena& arena, const NodeStore& nodes, BoxStore& boxes,
               float sizePx)
    : _arena(arena), _nodes(nodes), _boxes(boxes), _size(sizePx) {}

Handle Layout::combine(Handle atom, const Node& n, const Handle* boxOf) {
  switch (n.kind) {
    case Kind::Char:
      return _boxes.makeChar(n.ch, CHAR_W * _size, CHAR_H * _size,
                             CHAR_D * _size, 1.0f);

    case Kind::Row: {
      Handle tmp[kRowTmp];
      const u16 cnt = n.children.count;
      if (cnt > kRowTmp) return NO_NODE;
      float W = 0, H = 0, D = 0;
      for (u16 i = 0; i < cnt; ++i) {
        const Handle cb = boxOf[_nodes.child(atom, i)];
        if (!valid(cb)) return NO_NODE;
        const Box& b = _boxes.get(cb);
        W += b.width;
        H = fmax2(H, b.height + b.shift);
        D = fmax2(D, b.depth - b.shift);
        tmp[i] = cb;
      }
      return _boxes.makeList(BoxKind::HList, tmp, cnt, W, H, D);
    }

    case Kind::Frac: {
      const Handle num = boxOf[n.frac.num];
      const Handle den = boxOf[n.frac.den];
      if (!valid(num) || !valid(den)) return NO_NODE;
      Box& nb = _boxes.get(num);
      Box& db = _boxes.get(den);
      const float rt = RULE_T * _size;
      const float gap = FRAC_GAP * _size;
      const float axis = AXIS * _size;
      const float W = fmax2(nb.width, db.width);

      const float numShift = axis + rt * 0.5f + gap + nb.depth;
      const float denShift = axis - rt * 0.5f - gap - db.height;
      nb.shift = numShift;
      db.shift = denShift;

      Handle rule = _boxes.makeRule(W, rt * 0.5f, rt * 0.5f);
      if (!valid(rule)) return NO_NODE;
      _boxes.get(rule).shift = axis;

      const float H = numShift + nb.height;
      const float D = db.depth - denShift;
      const Handle parts[3] = {num, rule, den};
      return _boxes.makeList(BoxKind::VList, parts, 3, W, H, D);
    }

    case Kind::Script: {
      const Handle base = boxOf[n.script.base];
      if (!valid(base)) return NO_NODE;
      const Box& bb = _boxes.get(base);
      Handle parts[3];
      u16 np = 0;
      parts[np++] = base;
      float W = bb.width;
      float H = bb.height;
      float D = bb.depth;
      if (valid(n.script.sup)) {
        const Handle s = boxOf[n.script.sup];
        if (!valid(s)) return NO_NODE;
        Box& sb = _boxes.get(s);
        sb.width *= SCRIPT_SCALE;
        sb.height *= SCRIPT_SCALE;
        sb.depth *= SCRIPT_SCALE;
        sb.scale *= SCRIPT_SCALE;
        sb.shift = SUP_SHIFT * _size;
        W += sb.width;
        H = fmax2(H, sb.shift + sb.height);
        D = fmax2(D, sb.depth - sb.shift);
        parts[np++] = s;
      }
      if (valid(n.script.sub)) {
        const Handle s = boxOf[n.script.sub];
        if (!valid(s)) return NO_NODE;
        Box& sb = _boxes.get(s);
        sb.width *= SCRIPT_SCALE;
        sb.height *= SCRIPT_SCALE;
        sb.depth *= SCRIPT_SCALE;
        sb.scale *= SCRIPT_SCALE;
        sb.shift = -SUB_SHIFT * _size;
        W += sb.width;
        H = fmax2(H, sb.shift + sb.height);
        D = fmax2(D, sb.depth - sb.shift);
        parts[np++] = s;
      }
      return _boxes.makeList(BoxKind::HList, parts, np, W, H, D);
    }

    case Kind::Sqrt: {
      const Handle base = boxOf[n.sqrt.base];
      if (!valid(base)) return NO_NODE;
      const Box& bb = _boxes.get(base);
      const float rad = RADICAL_W * _size;
      const float pad = SQRT_PAD * _size;
      const float H = bb.height + pad;
      const float D = bb.depth;
      const Handle radBox = _boxes.makeChar(RADICAL_GLYPH, rad, H, D, 1.0f);
      if (!valid(radBox)) return NO_NODE;
      const Handle parts[2] = {radBox, base};
      return _boxes.makeList(BoxKind::HList, parts, 2, rad + bb.width, H, D);
    }

    case Kind::Matrix: {
      const u16 rows = n.matrix.rows;
      const u16 cols = n.matrix.cols;
      if (rows == 0 || cols == 0) {
        return _boxes.makeList(BoxKind::HList, nullptr, 0, 0, 0, 0);
      }
      constexpr int MAXD = 64;
      if (rows > MAXD || cols > MAXD) return NO_NODE;
      float colW[MAXD] = {0};
      float rowH[MAXD] = {0};
      float rowD[MAXD] = {0};
      for (u16 r = 0; r < rows; ++r) {
        for (u16 c = 0; c < cols; ++c) {
          const Handle cb = boxOf[_nodes.cell(atom, r, c)];
          if (!valid(cb)) return NO_NODE;
          const Box& b = _boxes.get(cb);
          colW[c] = fmax2(colW[c], b.width);
          rowH[r] = fmax2(rowH[r], b.height);
          rowD[r] = fmax2(rowD[r], b.depth);
        }
      }
      const float colGap = 0.6f * _size;
      const float rowGap = 0.3f * _size;
      const float axis = AXIS * _size;

      float totalH = 0;
      for (u16 r = 0; r < rows; ++r) totalH += rowH[r] + rowD[r];
      totalH += (rows - 1) * rowGap;
      float W = 0;
      for (u16 c = 0; c < cols; ++c) W += colW[c] + colGap;
      const float H = totalH * 0.5f + axis;
      const float D = totalH * 0.5f - axis;

      Handle rowBoxes[MAXD];
      float cursorTop = H;  // top edge above baseline
      for (u16 r = 0; r < rows; ++r) {
        Handle cellsB[MAXD];
        for (u16 c = 0; c < cols; ++c) {
          const Handle cb = boxOf[_nodes.cell(atom, r, c)];
          Box& b = _boxes.get(cb);
          b.shift = 0.0f;
          b.width = colW[c] + colGap;  // pad to column width
          cellsB[c] = cb;
        }
        const Handle rb =
            _boxes.makeList(BoxKind::HList, cellsB, cols, W, rowH[r], rowD[r]);
        if (!valid(rb)) return NO_NODE;
        _boxes.get(rb).shift = cursorTop - rowH[r];  // row baseline offset
        cursorTop -= (rowH[r] + rowD[r] + rowGap);
        rowBoxes[r] = rb;
      }
      const Handle grid =
          _boxes.makeList(BoxKind::VList, rowBoxes, rows, W, H, D);
      if (!valid(grid)) return NO_NODE;
      if (n.matrix.env == MatrixEnv::Plain) return grid;

      const c32 lch = (n.matrix.env == MatrixEnv::Bracket) ? U'[' : U'(';
      const c32 rch = (n.matrix.env == MatrixEnv::Bracket) ? U']' : U')';
      const float dW = 0.3f * _size;
      const Handle L = _boxes.makeChar(lch, dW, H, D, 1.0f);
      const Handle R = _boxes.makeChar(rch, dW, H, D, 1.0f);
      if (!valid(L) || !valid(R)) return NO_NODE;
      const Handle parts[3] = {L, grid, R};
      return _boxes.makeList(BoxKind::HList, parts, 3, dW * 2 + W, H, D);
    }

    default:
      return NO_NODE;
  }
}

LayoutResult Layout::run(Handle rootAtom) {
  if (!valid(rootAtom)) return {NO_NODE, false};
  if (!_boxes.ok()) return {NO_NODE, false};

  const u16 nc = _nodes.count();
  Handle* boxOf = _arena.allocArray<Handle>(nc == 0 ? 1 : nc);
  const u32 workCap = static_cast<u32>(nc) * 2u + 8u;
  WorkItem* work = _arena.allocArray<WorkItem>(workCap);
  if (boxOf == nullptr || work == nullptr) return {NO_NODE, false};
  for (u16 i = 0; i < nc; ++i) boxOf[i] = NO_NODE;

  u32 sp = 0;
  work[sp++] = WorkItem{rootAtom, 0};
  bool failed = false;

  while (sp > 0 && !failed) {
    const WorkItem it = work[--sp];
    const Node& n = _nodes.get(it.atom);
    if (it.phase == 0) {
      if (sp >= workCap) {
        failed = true;
        break;
      }
      work[sp++] = WorkItem{it.atom, 1};
      // Push children (any order; all complete before this node's phase 1).
      auto push = [&](Handle c) {
        if (!valid(c) || failed) return;
        if (sp >= workCap) {
          failed = true;
          return;
        }
        work[sp++] = WorkItem{c, 0};
      };
      switch (n.kind) {
        case Kind::Row:
          for (u16 i = 0; i < n.children.count; ++i) push(_nodes.child(it.atom, i));
          break;
        case Kind::Frac:
          push(n.frac.num);
          push(n.frac.den);
          break;
        case Kind::Script:
          push(n.script.base);
          push(n.script.sup);
          push(n.script.sub);
          break;
        case Kind::Sqrt:
          push(n.sqrt.base);
          push(n.sqrt.index);
          break;
        case Kind::Matrix: {
          const u16 total = static_cast<u16>(n.matrix.rows * n.matrix.cols);
          for (u16 k = 0; k < total; ++k) push(_nodes.cellAt(it.atom, k));
          break;
        }
        default:
          break;
      }
    } else {
      const Handle b = combine(it.atom, n, boxOf);
      if (!valid(b)) {
        failed = true;
        break;
      }
      boxOf[it.atom] = b;
    }
  }

  if (failed) return {NO_NODE, false};
  return {boxOf[rootAtom], true};
}

}  // namespace statex
