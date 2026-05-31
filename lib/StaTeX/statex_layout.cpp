#include "statex_layout.h"

#include "statex_fontparams.h"
#include "statex_glyphstore.h"

namespace statex {
namespace {

inline float fmax2(float a, float b) { return a > b ? a : b; }
inline float em(i16 v, float size) { return v / 256.0f * size; }

constexpr c32 RADICAL_GLYPH = 0x221A;  // √
constexpr int kRowTmp = 256;
constexpr int kMatMax = 64;

}  // namespace

Layout::Layout(Arena& arena, const NodeStore& nodes, BoxStore& boxes,
               float sizePx)
    : _arena(arena), _nodes(nodes), _boxes(boxes), _size(sizePx) {}

Handle Layout::combine(Handle atom, const Node& n, const Handle* boxOf,
                       float size) {
  const FontParams& p = fontParams();
  switch (n.kind) {
    case Kind::Char: {
      const GlyphRecord* g = findGlyphRecord(n.face, n.ch);
      float w, h, d;
      if (g != nullptr) {
        w = em(g->advance, size);
        h = em(g->height, size);
        d = em(g->depth, size);
      } else {  // glyph absent from atlas: reserve fallback space, draw nothing
        w = p.fallbackAdvance * size;
        h = p.fallbackHeight * size;
        d = 0.0f;
      }
      return _boxes.makeChar(n.ch, n.face, size, w, h, d);
    }

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
      const float rt = p.ruleThickness * size;
      const float gap = p.fracGap * size;
      const float axis = p.axisHeight * size;
      const float W = fmax2(nb.width, db.width);
      const float numShift = axis + rt * 0.5f + gap + nb.depth;
      const float denShift = axis - rt * 0.5f - gap - db.height;
      nb.shift = numShift;
      db.shift = denShift;
      const Handle rule = _boxes.makeRule(W, rt * 0.5f, rt * 0.5f);
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
      const bool hasSup = valid(n.script.sup);
      const bool hasSub = valid(n.script.sub);
      const float supY = p.supShift * size;
      const float subY = p.subShift * size;

      Handle scriptPart = NO_NODE;
      float partW = 0, partH = 0, partD = 0;
      if (hasSup && hasSub) {
        Handle sup = boxOf[n.script.sup];
        Handle sub = boxOf[n.script.sub];
        if (!valid(sup) || !valid(sub)) return NO_NODE;
        Box& su = _boxes.get(sup);
        Box& sb = _boxes.get(sub);
        su.shift = supY;
        sb.shift = -subY;
        partW = fmax2(su.width, sb.width);
        partH = supY + su.height;
        partD = subY + sb.depth;
        const Handle col[2] = {sup, sub};
        scriptPart = _boxes.makeList(BoxKind::VList, col, 2, partW, partH, partD);
      } else if (hasSup) {
        scriptPart = boxOf[n.script.sup];
        if (!valid(scriptPart)) return NO_NODE;
        Box& su = _boxes.get(scriptPart);
        su.shift = supY;
        partW = su.width;
        partH = supY + su.height;
        partD = su.depth - supY;
      } else {  // sub only
        scriptPart = boxOf[n.script.sub];
        if (!valid(scriptPart)) return NO_NODE;
        Box& sb = _boxes.get(scriptPart);
        sb.shift = -subY;
        partW = sb.width;
        partH = sb.height - subY;
        partD = subY + sb.depth;
      }
      const float W = bb.width + partW;
      const float H = fmax2(bb.height, partH);
      const float D = fmax2(bb.depth, partD);
      const Handle parts[2] = {base, scriptPart};
      return _boxes.makeList(BoxKind::HList, parts, 2, W, H, D);
    }

    case Kind::Sqrt: {
      const Handle base = boxOf[n.sqrt.base];
      if (!valid(base)) return NO_NODE;
      const Box& bb = _boxes.get(base);
      const float H = bb.height + p.sqrtPad * size;
      const float D = bb.depth;
      const GlyphRecord* g = findGlyphRecord(Face::Symbol, RADICAL_GLYPH);
      float radW;
      Handle radBox;
      if (g != nullptr && g->boxH > 0) {
        // Scale by the glyph's TOTAL box height (the radical sits mostly below
        // the baseline, so its `height` field is tiny); span the radicand.
        const float targetH = bb.height + bb.depth + p.sqrtPad * size;
        float radEmPx = targetH / (g->boxH / 256.0f);
        if (radEmPx > 8.0f * size) radEmPx = 8.0f * size;  // sanity clamp
        radW = em(g->advance, radEmPx);
        radBox = _boxes.makeChar(RADICAL_GLYPH, Face::Symbol, radEmPx, radW, H, D);
      } else {
        radW = p.radicalWidth * size;
        radBox = _boxes.makeChar(RADICAL_GLYPH, Face::Symbol, size, radW, H, D);
      }
      if (!valid(radBox)) return NO_NODE;
      const Handle parts[2] = {radBox, base};
      return _boxes.makeList(BoxKind::HList, parts, 2, radW + bb.width, H, D);
    }

    case Kind::Matrix: {
      const u16 rows = n.matrix.rows;
      const u16 cols = n.matrix.cols;
      if (rows == 0 || cols == 0) {
        return _boxes.makeList(BoxKind::HList, nullptr, 0, 0, 0, 0);
      }
      if (rows > kMatMax || cols > kMatMax) return NO_NODE;
      float colW[kMatMax] = {0}, rowH[kMatMax] = {0}, rowD[kMatMax] = {0};
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
      const float colGap = p.matrixColGap * size;
      const float rowGap = p.matrixRowGap * size;
      const float axis = p.axisHeight * size;
      float totalH = 0;
      for (u16 r = 0; r < rows; ++r) totalH += rowH[r] + rowD[r];
      totalH += (rows - 1) * rowGap;
      float W = 0;
      for (u16 c = 0; c < cols; ++c) W += colW[c] + colGap;
      const float H = totalH * 0.5f + axis;
      const float D = totalH * 0.5f - axis;

      Handle rowBoxes[kMatMax];
      float cursorTop = H;
      for (u16 r = 0; r < rows; ++r) {
        Handle cellsB[kMatMax];
        for (u16 c = 0; c < cols; ++c) {
          const Handle cb = boxOf[_nodes.cell(atom, r, c)];
          Box& b = _boxes.get(cb);
          b.shift = 0.0f;
          b.width = colW[c] + colGap;
          cellsB[c] = cb;
        }
        const Handle rb =
            _boxes.makeList(BoxKind::HList, cellsB, cols, W, rowH[r], rowD[r]);
        if (!valid(rb)) return NO_NODE;
        _boxes.get(rb).shift = cursorTop - rowH[r];
        cursorTop -= (rowH[r] + rowD[r] + rowGap);
        rowBoxes[r] = rb;
      }
      const Handle grid =
          _boxes.makeList(BoxKind::VList, rowBoxes, rows, W, H, D);
      if (!valid(grid)) return NO_NODE;
      if (n.matrix.env == MatrixEnv::Plain) return grid;

      const c32 lch = (n.matrix.env == MatrixEnv::Bracket) ? U'[' : U'(';
      const c32 rch = (n.matrix.env == MatrixEnv::Bracket) ? U']' : U')';
      // Scale a delimiter glyph to span the matrix height.
      const GlyphRecord* dg = findGlyphRecord(Face::Roman, lch);
      float dW;
      Handle L, R;
      if (dg != nullptr && dg->boxH > 0) {
        float dEm = (H + D) / (dg->boxH / 256.0f);
        if (dEm > 8.0f * size) dEm = 8.0f * size;  // sanity clamp
        dW = em(dg->advance, dEm);
        L = _boxes.makeChar(lch, Face::Roman, dEm, dW, H, D);
        R = _boxes.makeChar(rch, Face::Roman, dEm, dW, H, D);
      } else {
        dW = p.delimWidth * size;
        L = _boxes.makeChar(lch, Face::Roman, size, dW, H, D);
        R = _boxes.makeChar(rch, Face::Roman, size, dW, H, D);
      }
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

  const FontParams& p = fontParams();

  u32 sp = 0;
  work[sp++] = WorkItem{rootAtom, 0, _size};
  bool failed = false;

  while (sp > 0 && !failed) {
    const WorkItem it = work[--sp];
    const Node& n = _nodes.get(it.atom);
    if (it.phase == 0) {
      if (sp >= workCap) {
        failed = true;
        break;
      }
      work[sp++] = WorkItem{it.atom, 1, it.size};
      const float childScript = fmax2(it.size * p.scriptScale, p.scriptMinPx);
      auto push = [&](Handle c, float sz) {
        if (!valid(c) || failed) return;
        if (sp >= workCap) {
          failed = true;
          return;
        }
        work[sp++] = WorkItem{c, 0, sz};
      };
      switch (n.kind) {
        case Kind::Row:
          for (u16 i = 0; i < n.children.count; ++i)
            push(_nodes.child(it.atom, i), it.size);
          break;
        case Kind::Frac:
          push(n.frac.num, it.size);
          push(n.frac.den, it.size);
          break;
        case Kind::Script:
          push(n.script.base, it.size);
          push(n.script.sup, childScript);
          push(n.script.sub, childScript);
          break;
        case Kind::Sqrt:
          push(n.sqrt.base, it.size);
          push(n.sqrt.index, childScript);
          break;
        case Kind::Matrix: {
          const u16 total = static_cast<u16>(n.matrix.rows * n.matrix.cols);
          for (u16 k = 0; k < total; ++k) push(_nodes.cellAt(it.atom, k), it.size);
          break;
        }
        default:
          break;
      }
    } else {
      const Handle b = combine(it.atom, n, boxOf, it.size);
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
