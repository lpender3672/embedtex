#include "statex_layout.h"

#include "statex_draw.h"
#include "statex_fontparams.h"
#include "statex_glyphstore.h"
#include "statex_spacing.h"

namespace statex {
namespace {

inline float fmax2(float a, float b) { return a > b ? a : b; }

constexpr c32 RADICAL_GLYPH = 0x221A;  // √
/**
 * Would this glyph's coverage bitmap fit the draw phase's budget at `emPx`?
 *
 * Mirrors the size arithmetic in statex_sdf.cpp exactly. Checking here means
 * an oversized glyph is refused during layout, before the draw walk has put
 * anything on the panel.
 */
inline bool glyphFitsCoverage(const GlyphRecord& g, float emPx) {
  const float scale = emPx / 256.0f;
  const int w = static_cast<int>(static_cast<float>(g.boxW) * scale + 0.5f);
  const int h = static_cast<int>(static_cast<float>(g.boxH) * scale + 0.5f);
  if (w <= 0 || h <= 0) return true;  // nothing to draw
  return w * h <= kMaxGlyphCoveragePx;
}

constexpr int kRowTmp = 256;
constexpr int kMatMax = 64;

}  // namespace

Layout::Layout(Arena& arena, const NodeStore& nodes, BoxStore& boxes,
               float sizePx)
    : _arena(arena), _nodes(nodes), _boxes(boxes), _size(sizePx) {}

Handle Layout::combine(Handle atom, const Node& n, const Handle* boxOf,
                       float size, TexStyle style) {
  const FontParams& p = fontParams();
  switch (n.kind) {
    case Kind::Char: {
      // A big operator is set from a purpose-cut larger design (cmex10) and
      // centred on the math axis, not sat on the baseline like an ordinary
      // glyph. Everything else takes the text-size record.
      //
      // TeX takes the display cut of a big operator only in display style
      // (atom_char.cpp:43); in text and below it keeps the text glyph.
      const bool bigOp =
          (n.atomType == AtomType::BigOp) && isDisplayStyle(style);
      const GlyphRecord* g = bigOp ? findLargestGlyphVariant(n.face, n.ch)
                                   : findGlyphRecord(n.face, n.ch);
      if (g == nullptr) {
        // STX-FNT-05: an unavailable (face, glyph) pair refuses. Reserving
        // fallback space and drawing nothing was worse than useless -- it
        // returned Ok while showing something other than the formula asked
        // for, which the caller had no way to detect.
        _failure = ParseError::MissingGlyph;
        return NO_NODE;
      }
      if (!glyphFitsCoverage(*g, size)) {
        _failure = ParseError::GlyphTooLarge;
        return NO_NODE;
      }
      const float h = emUnits(g->height, size);
      const float d = emUnits(g->depth, size);
      // The italic correction is part of a big operator's occupied width --
      // TeX appends it as a strut after the glyph (atom_char.cpp:46-48). For
      // \int that is 0.4444em on top of a 0.5556em advance, i.e. the reported
      // width is 1.0em rather than 0.56em. Ordinary glyphs do not get it here;
      // italic correction between adjacent characters is a separate rule.
      const float w = emUnits(g->advance, size) + (bigOp ? emUnits(g->italic, size) : 0.0f);
      const Handle bh =
          _boxes.makeChar(n.ch, n.face, size, w, h, d, g->variant);
      if (bigOp && valid(bh)) {
        // Centre on the math axis. MicroTeX writes this as a downward shift of
        // -(h + d)/2 - axis; StaTeX's shift is positive-up, so the sign flips.
        _boxes.get(bh).shift = (h + d) * 0.5f + p.axisHeight * size;
      }
      return bh;
    }

    case Kind::Row: {
      const u16 cnt = n.children.count;
      // Glue goes between atoms, so the box list can reach 2*cnt-1 entries.
      if (cnt != 0 && static_cast<u32>(cnt) * 2u - 1u > kRowTmp) return NO_NODE;

      // Pass 1: TeX's BIN->ORD demotion. This has to run over the whole row
      // before any spacing is looked up, and left to right, because a demoted
      // atom stops being a blocker for the one after it.
      AtomType type[kRowTmp];
      for (u16 i = 0; i < cnt; ++i) {
        type[i] = _nodes.get(_nodes.child(atom, i)).atomType;
      }
      for (u16 i = 0; i < cnt; ++i) {
        if (type[i] != AtomType::BinaryOp) continue;
        const bool noOperandBefore = (i == 0) || demotesBinaryOperator(type[i - 1]);
        const bool noOperandAfter = (i + 1 == cnt);
        if (noOperandBefore || noOperandAfter) type[i] = AtomType::Ordinary;
      }
      // MicroTeX has a second demotion branch keyed on an atom whose *right*
      // type is Bin while its left type is not. A StaTeX Node carries one
      // atomType rather than a left/right pair, so left == right always and
      // that branch is unreachable here. It matters only for a nested Row
      // ending in a binary operator, which TeX would report as Bin on the
      // right; StaTeX reports such a group as Ordinary, matching TeX's
      // treatment of a braced group.

      // Pass 2 preamble: italic correction between adjacent characters
      // (atom_row.cpp:189-196). A char box followed by another character gets
      // its italic correction added to its width -- UNLESS it is an ordinary
      // typed character, which TeX excludes via `isCharInMathMode()`. A named
      // symbol is a SymbolAtom, not a CharAtom, so it is not excluded, which
      // in StaTeX terms means Face::Symbol.
      //
      // This is what `\alpha\beta\gamma` was missing: alpha's 0.0037 plus
      // beta's 0.0528 is 0.0565 em, and the last glyph sat 0.0548 em short.
      //
      // Big operators are skipped because the Char arm has already folded
      // their correction into the width; TeX likewise skips them, because it
      // wraps an operator in an HBox and the cast to CharBox then fails.
      for (u16 i = 0; i + 1 < cnt; ++i) {
        const Node& cur = _nodes.get(_nodes.child(atom, i));
        if (cur.kind != Kind::Char || cur.face != Face::Symbol) continue;
        if (cur.atomType == AtomType::BigOp) continue;
        if (_nodes.get(_nodes.child(atom, i + 1)).kind != Kind::Char) continue;
        const GlyphRecord* cg = findGlyphRecord(cur.face, cur.ch);
        if (cg == nullptr || cg->italic == 0) continue;
        const Handle cb = boxOf[_nodes.child(atom, i)];
        if (valid(cb)) _boxes.get(cb).width += emUnits(cg->italic, size);
      }

      // Pass 2: lay the row out, inserting glue between neighbours.
      Handle tmp[kRowTmp];
      u16 out = 0;
      float W = 0, H = 0, D = 0;
      for (u16 i = 0; i < cnt; ++i) {
        if (i != 0) {
          const float g = interAtomGlueEm(type[i - 1], type[i], spacingClassOf(style)) * size;
          if (g > 0.0f) {
            // A zero-child HList with a width and no ink: TeX's glue. drawTree
            // walks it and paints nothing, which is exactly what is wanted.
            const Handle glue = _boxes.makeList(BoxKind::HList, nullptr, 0, g, 0, 0);
            if (!valid(glue)) return NO_NODE;
            tmp[out++] = glue;
            W += g;
          }
        }
        const Handle cb = boxOf[_nodes.child(atom, i)];
        if (!valid(cb)) return NO_NODE;
        const Box& b = _boxes.get(cb);
        W += b.width;
        H = fmax2(H, b.height + b.shift);
        D = fmax2(D, b.depth - b.shift);
        tmp[out++] = cb;
      }
      return _boxes.makeList(BoxKind::HList, tmp, out, W, H, D);
    }

    case Kind::Frac: {
      const Handle num = boxOf[n.frac.num];
      const Handle den = boxOf[n.frac.den];
      if (!valid(num) || !valid(den)) return NO_NODE;
      Box& nb = _boxes.get(num);
      Box& db = _boxes.get(den);

      // TeX82 rules 15a-15e (FractionAtom::createBox, atom_impl.cpp:105-195).
      // Both arms were already laid out in numerator and denominator style by
      // the traversal, so what is left is the vertical fitting.
      const float thickness = p.ruleThickness * size;
      const float axis = p.axisHeight * size;
      const bool display = isDisplayStyle(style);

      // 15b: the default shifts depend on style. StaTeX used one `fracGap`
      // constant of 0.12 em for every case, which is none of these.
      //
      // Only the ruled form exists: `Node::frac.rule` is always true because
      // the parser has no top or \over. Adding one means the other half of
      // TeX82 15c -- `num3` for the numerator shift and a clearance of 7 rule
      // thicknesses in display style, 3 otherwise -- plus a branch here. The
      // parameter is not carried until then rather than sitting unused.
      float shiftUp = display ? p.num1 * size : p.num2 * size;
      float shiftDown = display ? p.denom1 * size : p.denom2 * size;

      // 15d: each arm must clear the rule by `clr` -- three rule thicknesses
      // in display style, one otherwise. Where it does not, the shift grows
      // until it does. This is what stops a tall numerator touching the bar.
      const float clr = display ? 3.0f * thickness : thickness;
      const float delta = thickness * 0.5f;
      float kern1 = shiftUp - nb.depth - (axis + delta);
      float kern2 = axis - delta - (db.height - shiftDown);
      const float d1 = clr - kern1;
      const float d2 = clr - kern2;
      if (d1 > 0.0f) { shiftUp += d1; kern1 += d1; }
      if (d2 > 0.0f) { shiftDown += d2; kern2 += d2; }

      const float W = fmax2(nb.width, db.width);
      nb.shift = shiftUp;
      db.shift = -shiftDown;
      const Handle rule = _boxes.makeRule(W, delta, delta);
      if (!valid(rule)) return NO_NODE;
      _boxes.get(rule).shift = axis;

      const float H = shiftUp + nb.height;
      const float D = shiftDown + db.depth;
      const Handle parts[3] = {num, rule, den};
      const Handle vb = _boxes.makeList(BoxKind::VList, parts, 3, W, H, D);
      if (!valid(vb)) return NO_NODE;

      // nulldelimiterspace: 1.2pt each side, so a fraction never butts
      // straight up against its neighbour.
      const float pad = p.nullDelimiter * size;
      if (pad <= 0.0f) return vb;
      const Handle lead =
          _boxes.makeList(BoxKind::HList, nullptr, 0, pad, 0.0f, 0.0f);
      const Handle tail =
          _boxes.makeList(BoxKind::HList, nullptr, 0, pad, 0.0f, 0.0f);
      if (!valid(lead) || !valid(tail)) return NO_NODE;
      const Handle fenced[3] = {lead, vb, tail};
      return _boxes.makeList(BoxKind::HList, fenced, 3, W + 2.0f * pad, H, D);
    }

    case Kind::Script: {
      const Handle base = boxOf[n.script.base];
      if (!valid(base)) return NO_NODE;
      const Box& bb = _boxes.get(base);
      const bool hasSup = valid(n.script.sup);
      const bool hasSub = valid(n.script.sub);

      // Limits: in display style, scripts on an operator that takes them go
      // above and below rather than beside (BigOperatorAtom::createBox,
      // atom_basic.cpp:688-800). `\sum` takes limits, `\int` does not -- that
      // is per-symbol data, not derivable from the atom type, since both are
      // big operators.
      if (_nodes.get(n.script.base).takesLimits && isDisplayStyle(style)) {
        Box& bb2 = _boxes.get(base);
        const float pad = p.bigOpSpacing5 * size;
        // The base already carries its axis-centring shift from the Char arm,
        // so its reach in the parent's coordinates includes it.
        const float baseTop = bb2.height + bb2.shift;
        const float baseBot = bb2.depth - bb2.shift;

        float maxW = bb2.width;
        if (hasSup) maxW = fmax2(maxW, _boxes.get(boxOf[n.script.sup]).width);
        if (hasSub) maxW = fmax2(maxW, _boxes.get(boxOf[n.script.sub]).width);

        Handle stack[5];
        u16 nStack = 0;
        float H2 = baseTop, D2 = baseBot;

        // Every part is centred on the operator, so each gets a leading strut
        // carrying half its slack. StaTeX boxes have no horizontal shift.
        auto centred = [&](Handle box, float shift) -> Handle {
          Box& bx = _boxes.get(box);
          const float lead = (maxW - bx.width) * 0.5f;
          bx.shift = shift;
          if (lead <= 0.0f) return box;
          const Handle strut =
              _boxes.makeList(BoxKind::HList, nullptr, 0, lead, 0.0f, 0.0f);
          if (!valid(strut)) return NO_NODE;
          const Handle pair[2] = {strut, box};
          const Handle row = _boxes.makeList(BoxKind::HList, pair, 2, maxW,
                                             bx.height, bx.depth);
          if (valid(row)) _boxes.get(row).shift = shift;
          if (valid(row)) bx.shift = 0.0f;
          return row;
        };

        if (hasSup) {
          Box& su = _boxes.get(boxOf[n.script.sup]);
          // xi9 is the minimum gap; xi11 the minimum clearance above the
          // operator, which only bites when the limit has little depth.
          const float kern =
              fmax2(p.bigOpSpacing1 * size, p.bigOpSpacing3 * size - su.depth);
          const float shift = baseTop + kern + su.depth;
          const Handle h2 = centred(boxOf[n.script.sup], shift);
          if (!valid(h2)) return NO_NODE;
          stack[nStack++] = h2;
          H2 = shift + su.height + pad;
        }
        stack[nStack++] = base;
        if (hasSub) {
          Box& sb = _boxes.get(boxOf[n.script.sub]);
          const float k =
              fmax2(p.bigOpSpacing2 * size, p.bigOpSpacing4 * size - sb.height);
          const float shift = -(baseBot + k + sb.height);
          const Handle h2 = centred(boxOf[n.script.sub], shift);
          if (!valid(h2)) return NO_NODE;
          stack[nStack++] = h2;
          D2 = baseBot + k + sb.height + sb.depth + pad;
        }
        const float lead = (maxW - bb2.width) * 0.5f;
        if (lead > 0.0f) {
          // Centre the operator itself too, without disturbing its shift.
          const Handle strut =
              _boxes.makeList(BoxKind::HList, nullptr, 0, lead, 0.0f, 0.0f);
          if (!valid(strut)) return NO_NODE;
          for (u16 k2 = 0; k2 < nStack; ++k2) {
            if (stack[k2] != base) continue;
            const Handle pair[2] = {strut, base};
            const Handle row = _boxes.makeList(BoxKind::HList, pair, 2, maxW,
                                               bb2.height, bb2.depth);
            if (!valid(row)) return NO_NODE;
            _boxes.get(row).shift = bb2.shift;
            _boxes.get(base).shift = 0.0f;
            stack[k2] = row;
          }
        }
        return _boxes.makeList(BoxKind::VList, stack, nStack, maxW, H2, D2);
      }

      // Wrap a script in a row of exactly `width`, optionally preceded by a
      // kern. Declaring the full width is what defeats drawTree's VList
      // centring, so the script sits at the column's left edge as TeX sets it.
      auto rowOfWidth = [&](Handle box, float kernW, float width,
                            float shift) -> Handle {
        Box& bx = _boxes.get(box);
        const float h = bx.height, d = bx.depth;
        bx.shift = 0.0f;
        Handle items[2];
        u16 count = 0;
        if (kernW > 0.0f) {
          const Handle kern =
              _boxes.makeList(BoxKind::HList, nullptr, 0, kernW, 0.0f, 0.0f);
          if (!valid(kern)) return NO_NODE;
          items[count++] = kern;
        }
        items[count++] = box;
        const Handle row =
            _boxes.makeList(BoxKind::HList, items, count, width, h, d);
        if (valid(row)) _boxes.get(row).shift = shift;
        return row;
      };

      // TeX82 rules 18a-18f (ScriptsAtom::createBox, atom_basic.cpp:446-590).
      const float xh = p.xHeight * size;
      const float drt = p.ruleThickness * size;
      const float space = p.scriptSpacePx;  // absolute, see FontParams

      // 18a: preliminary shifts follow how far the base's ink reaches, pulled
      // back by supDrop/subDrop.
      //
      // An ordinary CHARACTER is exempt -- its scripts hang off the baseline,
      // not off its top. A big operator is NOT, even though it is also a
      // single character: TeX measures it from its axis-centred box
      // (atom_basic.cpp:487-500), which for a display integral reaches
      // 1.36 em up. Exempting it too put `\int_a^b`'s scripts 0.675 em out.
      //
      // The drops are taken at the SCRIPT's size, not the base's, which is why
      // they are scaled here rather than by `size`.
      const Node& baseNode = _nodes.get(n.script.base);
      const bool ordinaryChar =
          baseNode.kind == Kind::Char && baseNode.atomType != AtomType::BigOp;
      const float supSize = _size * styleSizeFactor(superscriptStyle(style));
      const float subSize = _size * styleSizeFactor(subscriptStyle(style));
      const float baseTopReach = bb.height + bb.shift;
      const float baseBotReach = bb.depth - bb.shift;

      // A big operator's italic correction is part of its width only when
      // nothing is subscripted to it (atom_basic.cpp:497). With a subscript
      // the correction is dropped, because the subscript tucks under the
      // operator's overhang instead of clearing it. The Char arm cannot know
      // that -- it does not see the scripts -- so it always adds the
      // correction and this removes it again. For a display integral that is
      // 0.444 em, which is where `\int_a^b` was losing its scripts.
      // The base's italic correction goes BETWEEN the base and a superscript:
      // a sloped letter leans out over the space its advance reserves, and the
      // script has to clear the overhang. TeX takes it from any character base
      // (ScriptsAtom's CharSymbol branch, atom_basic.cpp:505-516), not just
      // from big operators -- `y^{z}` needs y's 0.0359 em just as `\int^b`
      // needs the integral's 0.4444.
      //
      // It applies only when there is no subscript. With one, the correction
      // becomes a horizontal offset on the superscript alone, because the
      // subscript tucks under the overhang instead of clearing it.
      //
      // `x` has zero italic correction, which is why `x^2` was right all along
      // and `x^{y^{z}}` was not.
      float baseItalic = 0.0f;
      if (baseNode.kind == Kind::Char) {
        const GlyphRecord* bg =
            ordinaryChar ? findGlyphRecord(baseNode.face, baseNode.ch)
                         : findLargestGlyphVariant(baseNode.face, baseNode.ch);
        if (bg != nullptr) baseItalic = emUnits(bg->italic, size);
      }
      // A big operator's Char box already carries its correction (the Char arm
      // folds it in, which is right for a bare operator). Strip it here so the
      // rules below can put it back exactly where TeX puts it.
      if (!ordinaryChar && baseNode.kind == Kind::Char && baseItalic > 0.0f) {
        _boxes.get(base).width -= baseItalic;
      }
      if (hasSup && !hasSub && baseItalic > 0.0f) {
        _boxes.get(base).width += baseItalic;
      }
      const float bigOpItalic = hasSub ? baseItalic : 0.0f;
      float shiftUp =
          ordinaryChar ? 0.0f : baseTopReach - p.supDrop * supSize;
      float shiftDown =
          ordinaryChar ? 0.0f : baseBotReach + p.subDrop * subSize;

      Handle scriptPart = NO_NODE;
      float partW = 0, partH = 0, partD = 0;

      if (hasSup) {
        const Handle sup = boxOf[n.script.sup];
        if (!valid(sup)) return NO_NODE;
        Box& su = _boxes.get(sup);
        // 18c: which of the three superscript shifts applies.
        const float pShift = (style == TexStyle::Display) ? p.sup1 * size
                             : isCrampedStyle(style)      ? p.sup3 * size
                                                          : p.sup2 * size;
        shiftUp = fmax2(fmax2(shiftUp, pShift), su.depth + xh * 0.25f);

        if (!hasSub) {
          su.shift = shiftUp;
          partW = su.width + space;
          partH = shiftUp + su.height;
          partD = su.depth - shiftUp;
          scriptPart = sup;
        } else {
          const Handle sub = boxOf[n.script.sub];
          if (!valid(sub)) return NO_NODE;
          Box& sb = _boxes.get(sub);
          // 18e: with both scripts the subscript takes sub2, and the gap
          // between them must reach four rule thicknesses. Where it does not
          // the superscript rises; if that leaves its bottom under 4/5 of the
          // x-height, both move so it clears.
          shiftDown = fmax2(shiftDown, p.sub2 * size);
          const float gap = shiftUp - su.depth + shiftDown - sb.height;
          if (gap < 4.0f * drt) {
            shiftUp += 4.0f * drt - gap;
            const float psi = 0.8f * xh - (shiftUp - su.depth);
            if (psi > 0.0f) {
              shiftUp += psi;
              shiftDown -= psi;
            }
          }
          su.shift = shiftUp;
          sb.shift = -shiftDown;
          // 18f: both scripts are set in a column `msiz` wide, LEFT-aligned
          // (ScriptsAtom's default alignment), with the script space appended
          // to each. Left-aligned matters: drawTree centres VList children,
          // which is right for a fraction and for stacked limits but wrong
          // here -- centring pushed the narrower script inwards by half the
          // width difference, 13.6px of it on `\int_a^b`.
          //
          // Each script is therefore wrapped in a row of the full column
          // width, which makes the centring a no-op and leaves the script at
          // the column's left edge.
          const float msiz = fmax2(su.width, sb.width);
          partW = msiz + space;

          // The italic correction removed from the base's width above still
          // applies to the SUPERSCRIPT: it sits out over the operator's
          // overhang while the subscript tucks under it. TeX writes it as
          // `sup->_shift = delta` (atom_basic.cpp:577), a horizontal offset
          // inside the column; StaTeX boxes carry no horizontal shift, so it
          // becomes a leading strut.
          // BOTH rows must be the column's full width, or the centring this
          // is meant to defeat simply reappears: wrapping the subscript to
          // `partW` inside a column of `partW + italic` pushed it right by
          // half the italic, 14.25px of it on `\int_a^b`.
          const float colW = partW + bigOpItalic;
          const Handle supEntry = rowOfWidth(sup, bigOpItalic, colW, shiftUp);
          const Handle subEntry = rowOfWidth(sub, 0.0f, colW, -shiftDown);
          if (!valid(supEntry) || !valid(subEntry)) return NO_NODE;

          partW = colW;
          partH = shiftUp + su.height;
          partD = shiftDown + sb.depth;
          const Handle col[2] = {supEntry, subEntry};
          scriptPart =
              _boxes.makeList(BoxKind::VList, col, 2, colW, partH, partD);
        }
      } else {
        // 18b: a subscript alone takes sub1, and its top stays 4/5 of an
        // x-height below the baseline.
        const Handle sub = boxOf[n.script.sub];
        if (!valid(sub)) return NO_NODE;
        Box& sb = _boxes.get(sub);
        shiftDown =
            fmax2(fmax2(shiftDown, p.sub1 * size), sb.height - 0.8f * xh);
        sb.shift = -shiftDown;
        partW = sb.width + space;
        partH = sb.height - shiftDown;
        partD = shiftDown + sb.depth;
        scriptPart = sub;
      }
      if (!valid(scriptPart)) return NO_NODE;

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
      float baseW = bb.width;
      const float baseH = bb.height;
      const float baseD = bb.depth;

      // TeX's radical construction (NthRoot::createBox, atom_impl.cpp:200-232;
      // TeXbook Appendix G rule 11). Written in TeX's own terms so it can be
      // read against the source:
      //
      //   drt   default rule thickness, the vinculum's thickness
      //   clr   minimum clearance between the radicand and the vinculum.
      //         In display style it starts from the surd font's x-height;
      //         in script styles it is just drt. StaTeX has no style dimension
      //         yet and the oracle renders in display, so the display form is
      //         what agrees today.
      //   Then a surd is chosen tall enough to span radicand + clr + drt, and
      //   HALF of whatever slack that variant has spare is added back to clr,
      //   which is what stops a big surd from sitting tight against a short
      //   radicand.
      const float drt = p.ruleThickness * size;
      // Display style measures the clearance from the surd font's x-height;
      // every other style uses the rule thickness alone.
      const float base0 = isDisplayStyle(style) ? p.xHeight * size : drt;
      float clr = drt + base0 * 0.25f;

      const float totalH = baseH + baseD;

      // Pick the smallest variant that spans the radicand, rather than scaling
      // one design up: a scaled 10pt surd has the wrong stroke weight and,
      // because it is positioned from its own depth, ends up in the wrong
      // place too. Chain: cmsy10 112 -> cmex10 112..115.
      const float needEm = (totalH + clr + drt) / size;
      const i16 need = static_cast<i16>(needEm * 256.0f + 0.5f);
      const GlyphRecord* g =
          findGlyphVariantAtLeast(Face::Symbol, RADICAL_GLYPH, need);
      if (g == nullptr) {
        _failure = ParseError::MissingGlyph;
        return NO_NODE;
      }
      if (!glyphFitsCoverage(*g, size)) {
        _failure = ParseError::GlyphTooLarge;
        return NO_NODE;
      }
      const float surdH = emUnits(g->height, size);
      const float surdD = emUnits(g->depth, size);

      // Spare depth in the chosen variant, split evenly above and below.
      clr += (surdD - (totalH + clr)) * 0.5f;

      // TeX puts a 1mu space after the radicand, inside the overbar
      // (atom_impl.cpp:214), so the vinculum overhangs the content very
      // slightly rather than stopping flush against it. Without it a radical
      // is 1/18 em narrow, which is enough to shift anything centred against
      // it -- `rac{\sqrt{a}}{b}` had its denominator 0.03 em out.
      const float tailMu = kMuPerEm * size;
      baseW += tailMu;

      // The vinculum sits `clr` above the radicand's tallest ink.
      const Handle rule = _boxes.makeRule(baseW, drt, 0.0f);
      if (!valid(rule)) return NO_NODE;
      _boxes.get(rule).shift = baseH + clr;

      // OverBar (box_group.cpp:170-175) stacks strut(drt), rule(drt),
      // strut(clr), radicand -- so there is a rule thickness of AIR above the
      // vinculum as well as the rule itself. StaTeX counted only the rule.
      const float H = baseH + clr + 2.0f * drt;
      const float D = baseD;

      // The mu goes on as a TRAILING strut in a left-aligned row, not by
      // widening the radicand box: a VList centres its children, so simply
      // making the box wider drifts the content right by half the mu.
      const Handle tailStrut =
          _boxes.makeList(BoxKind::HList, nullptr, 0, tailMu, 0.0f, 0.0f);
      if (!valid(tailStrut)) return NO_NODE;
      const Handle rowItems[2] = {base, tailStrut};
      const Handle baseRow = _boxes.makeList(BoxKind::HList, rowItems, 2, baseW,
                                             _boxes.get(base).height,
                                             _boxes.get(base).depth);
      if (!valid(baseRow)) return NO_NODE;
      const Handle covered[2] = {rule, baseRow};
      const Handle radicand =
          _boxes.makeList(BoxKind::VList, covered, 2, baseW, H, D);
      if (!valid(radicand)) return NO_NODE;

      const float radW = emUnits(g->advance, size);
      const Handle radBox = _boxes.makeChar(RADICAL_GLYPH, Face::Symbol, size,
                                            radW, surdH, surdD, g->variant);
      if (!valid(radBox)) return NO_NODE;
      // rootSign->_shift = -(b->_height + clr), and StaTeX's shift is
      // positive-up where MicroTeX's is positive-down.
      _boxes.get(radBox).shift = baseH + clr;

      // The radical as a whole -- surd beside the overbarred radicand -- is
      // what an index is positioned against, so its reach has to account for
      // the surd's own shift, not just the radicand's.
      const float surdShift = baseH + clr;
      const float sqH = fmax2(H, surdH + surdShift);
      const float sqD = fmax2(D, surdD - surdShift);

      if (valid(n.sqrt.index)) {
        // NthRoot::createBox, atom_impl.cpp:234-251. The index rides in the
        // crook of the surd, raised by 0.55 of the radical's total height and
        // then pulled back over it by a NEGATIVE 10mu kern -- that overlap is
        // what puts it in the crook rather than beside it. StaTeX had the 0.55
        // but no kern, and measured against the radicand instead of the whole
        // radical, which left the index 0.31-0.50 em too far left.
        const Handle index = boxOf[n.sqrt.index];
        if (!valid(index)) return NO_NODE;
        Box& ib = _boxes.get(index);
        const float bottomShift = 0.55f * (sqH + sqD);
        ib.shift = bottomShift + ib.depth - sqD;

        // -10mu. A negative-width strut simply walks the pen backwards, which
        // is exactly what the draw walk's sequential advance does with it.
        const float negKern = -10.0f * kMuPerEm * size;
        const float pos = ib.width + negKern;

        Handle parts[5];
        u16 np = 0;
        if (pos < 0.0f) {
          // The kern would take the pen left of the origin; pad so the whole
          // construction still starts at x = 0.
          const Handle pad =
              _boxes.makeList(BoxKind::HList, nullptr, 0, -pos, 0.0f, 0.0f);
          if (!valid(pad)) return NO_NODE;
          parts[np++] = pad;
        }
        const Handle kern =
            _boxes.makeList(BoxKind::HList, nullptr, 0, negKern, 0.0f, 0.0f);
        if (!valid(kern)) return NO_NODE;
        parts[np++] = index;
        parts[np++] = kern;
        parts[np++] = radBox;
        parts[np++] = radicand;
        const float W = fmax2(pos, 0.0f) + radW + baseW;
        return _boxes.makeList(BoxKind::HList, parts, np, W,
                               fmax2(sqH, ib.shift + ib.height),
                               fmax2(sqD, ib.depth - ib.shift));
      }
      const Handle parts[2] = {radBox, radicand};
      return _boxes.makeList(BoxKind::HList, parts, 2, radW + baseW, sqH, sqD);
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

      // Row separation, TeX's way (atom_matrix.cpp:497,574-575): each row is
      // padded by HALF the separation above and below, rather than a whole gap
      // being inserted between rows. The two are not the same. TeX's total is
      // `sum(rowH + rowD) + rows * vsep`; a between-rows gap gives
      // `... + (rows-1) * gap`, so the grid is short by one separation and,
      // because the result is then centred on the axis, every row is displaced.
      //
      // Row extents come from the cells and are NOT floored to a fixed strut.
      // StaTeX used to clamp every row to 0.70/0.30 em, which made a row of
      // x-height letters as tall as a row of ascenders and put the pitch
      // 0.175 em out per row.
      const float halfGap = rowGap * 0.5f;
      for (u16 r = 0; r < rows; ++r) {
        rowH[r] += halfGap;
        rowD[r] += halfGap;
      }

      float totalH = 0;
      for (u16 r = 0; r < rows; ++r) totalH += rowH[r] + rowD[r];
      // Gaps go *between* columns, not after every one. Adding one per column
      // left a full gap of dead space on the grid's right edge, inside the
      // delimiters.
      float W = 0;
      for (u16 c = 0; c < cols; ++c) W += colW[c];
      W += (cols - 1) * colGap;
      const float H = totalH * 0.5f + axis;
      const float D = totalH * 0.5f - axis;

      Handle rowBoxes[kMatMax];
      float cursorTop = H;
      for (u16 r = 0; r < rows; ++r) {
        // Up to two entries per column: a leading strut carrying the column
        // gap and the left half of the centring pad, then the cell itself
        // with the right half absorbed into its advance.
        Handle rowItems[kMatMax * 2];
        u16 nItems = 0;
        for (u16 c = 0; c < cols; ++c) {
          const Handle cb = boxOf[_nodes.cell(atom, r, c)];
          const float cellW = _boxes.get(cb).width;
          const float pad = (colW[c] - cellW) * 0.5f;
          const float lead = (c > 0 ? colGap : 0.0f) + pad;
          if (lead > 0.0f) {
            const Handle strut =
                _boxes.makeList(BoxKind::HList, nullptr, 0, lead, 0.0f, 0.0f);
            if (!valid(strut)) return NO_NODE;
            rowItems[nItems++] = strut;
          }
          Box& b = _boxes.get(cb);
          b.shift = 0.0f;
          b.width = cellW + pad;
          rowItems[nItems++] = cb;
        }
        const Handle rb = _boxes.makeList(BoxKind::HList, rowItems, nItems, W,
                                          rowH[r], rowD[r]);
        if (!valid(rb)) return NO_NODE;
        _boxes.get(rb).shift = cursorTop - rowH[r];
        // rowH/rowD already carry half the separation each, so consecutive
        // rows abut: adding rowGap again here would double-count it.
        cursorTop -= (rowH[r] + rowD[r]);
        rowBoxes[r] = rb;
      }
      const Handle grid =
          _boxes.makeList(BoxKind::VList, rowBoxes, rows, W, H, D);
      if (!valid(grid)) return NO_NODE;
      if (n.matrix.env == MatrixEnv::Plain) return grid;

      const c32 lch = (n.matrix.env == MatrixEnv::Bracket) ? U'[' : U'(';
      const c32 rch = (n.matrix.env == MatrixEnv::Bracket) ? U']' : U')';

      // TeX's fenced-atom rule (FencedAtom::createBox, atom_impl.cpp:31-41):
      //
      //   delta = max(contentHeight - axis, contentDepth + axis)
      //   minh  = max(delta/500 * 901, 2*delta - 5pt)
      //
      // i.e. the delimiter spans at least ~1.8x the content's larger half,
      // measured from the axis rather than the baseline, with a 5pt shortfall
      // allowance so a delimiter is not grown for the sake of a few points.
      // Then the smallest variant reaching minh is chosen and CENTRED on the
      // axis. Scaling one text bracket to the content height instead -- what
      // StaTeX did -- gets both the stroke weight and, because the glyph is
      // placed from its own metrics, the vertical position wrong: every
      // bracketed matrix sat 1.40 em out.
      const float delta = fmax2(H - axis, D + axis);
      const float shortfall = 0.5f * size;  // 5pt at a 10pt design size
      const float minh = fmax2(delta * (901.0f / 500.0f), 2.0f * delta - shortfall);
      const i16 need = static_cast<i16>(minh / size * 256.0f + 0.5f);

      const GlyphRecord* lg = findGlyphVariantAtLeast(Face::Roman, lch, need);
      const GlyphRecord* rg = findGlyphVariantAtLeast(Face::Roman, rch, need);
      if (lg == nullptr || rg == nullptr) {
        _failure = ParseError::MissingGlyph;
        return NO_NODE;
      }

      // Past the largest single glyph, TeX stops choosing and starts building.
      // Falling back to the tallest bracket instead left every big matrix
      // visibly short -- the showcase's were 0.6 em under.
      if (lg->height + lg->depth < need || rg->height + rg->depth < need) {
        const Handle la = makeExtensibleDelimiter(lch, size, minh, axis);
        const Handle ra = makeExtensibleDelimiter(rch, size, minh, axis);
        if (valid(la) && valid(ra)) {
          const Box& lb = _boxes.get(la);
          const Box& rb = _boxes.get(ra);
          const float fenceH2 =
              fmax2(H, fmax2(lb.height + lb.shift, rb.height + rb.shift));
          const float fenceD2 =
              fmax2(D, fmax2(lb.depth - lb.shift, rb.depth - rb.shift));
          const Handle built[3] = {la, grid, ra};
          return _boxes.makeList(BoxKind::HList, built, 3,
                                 lb.width + W + rb.width, fenceH2, fenceD2);
        }
        // No recipe for this delimiter: the largest single glyph is still a
        // better answer than a distorted one.
      }
      if (!glyphFitsCoverage(*lg, size) || !glyphFitsCoverage(*rg, size)) {
        _failure = ParseError::GlyphTooLarge;
        return NO_NODE;
      }

      // center(): shift so the glyph's midpoint lands on the axis. MicroTeX
      // writes `_shift = -(total/2 - h) - axis` with shift positive-down.
      const float lH = emUnits(lg->height, size), lD = emUnits(lg->depth, size);
      const float rH = emUnits(rg->height, size), rD = emUnits(rg->depth, size);
      const float lShift = (lH + lD) * 0.5f - lH + axis;
      const float rShift = (rH + rD) * 0.5f - rH + axis;

      const float lW = emUnits(lg->advance, size);
      const float rW = emUnits(rg->advance, size);
      const Handle L =
          _boxes.makeChar(lch, Face::Roman, size, lW, lH, lD, lg->variant);
      const Handle R =
          _boxes.makeChar(rch, Face::Roman, size, rW, rH, rD, rg->variant);
      if (!valid(L) || !valid(R)) return NO_NODE;
      _boxes.get(L).shift = lShift;
      _boxes.get(R).shift = rShift;

      // The fence is as tall as whichever of the three parts reaches furthest.
      const float fenceH = fmax2(H, fmax2(lH + lShift, rH + rShift));
      const float fenceD = fmax2(D, fmax2(lD - lShift, rD - rShift));
      const Handle parts[3] = {L, grid, R};
      return _boxes.makeList(BoxKind::HList, parts, 3, lW + W + rW, fenceH,
                             fenceD);
    }

    default:
      return NO_NODE;
  }
}

Handle Layout::makeExtensibleDelimiter(c32 ch, float size, float minTotalPx,
                                       float axis) {
  const GlyphRecord* top = findGlyphVariant(Face::Roman, ch, kPieceTop);
  const GlyphRecord* rep = findGlyphVariant(Face::Roman, ch, kPieceRepeat);
  const GlyphRecord* bot = findGlyphVariant(Face::Roman, ch, kPieceBottom);
  if (top == nullptr || rep == nullptr || bot == nullptr) return NO_NODE;

  const float topH = emUnits(top->height, size);
  const float topD = emUnits(top->depth, size);
  const float repH = emUnits(rep->height, size);
  const float repD = emUnits(rep->depth, size);
  const float botH = emUnits(bot->height, size);
  const float botD = emUnits(bot->depth, size);
  const float repSpan = repH + repD;
  if (repSpan <= 0.0f) return NO_NODE;  // a zero-height tile would not converge

  const float fixed = (topH + topD) + (botH + botD);
  // TeX's loop is `while (total <= minHeight) add a tile`, so the assembled
  // delimiter strictly EXCEEDS the requested height -- at exact equality it
  // still adds one more. Rounding to ">= minHeight" instead left the showcase
  // one tile short in each bracket.
  int repeats = 0;
  while (fixed + repeats * repSpan <= minTotalPx) repeats++;
  // Bounded, because the caller's `minTotalPx` comes from content that could
  // in principle be enormous, and this loop allocates a box per tile.
  constexpr int kMaxRepeats = 32;
  if (repeats > kMaxRepeats) repeats = kMaxRepeats;

  const int pieces = repeats + 2;
  if (pieces > kMatMax) return NO_NODE;

  // Stack upward from the bottom piece, whose baseline is the assembly's own.
  Handle parts[kMatMax];
  int n = 0;
  const float w = emUnits(bot->advance, size);
  float shift = 0.0f;
  float prevH = botH;

  const Handle bottom =
      _boxes.makeChar(ch, Face::Roman, size, w, botH, botD, kPieceBottom);
  if (!valid(bottom)) return NO_NODE;
  parts[n++] = bottom;

  for (int i = 0; i < repeats; ++i) {
    shift += prevH + repD;
    const Handle tile =
        _boxes.makeChar(ch, Face::Roman, size, w, repH, repD, kPieceRepeat);
    if (!valid(tile)) return NO_NODE;
    _boxes.get(tile).shift = shift;
    parts[n++] = tile;
    prevH = repH;
  }

  shift += prevH + topD;
  const Handle head =
      _boxes.makeChar(ch, Face::Roman, size, w, topH, topD, kPieceTop);
  if (!valid(head)) return NO_NODE;
  _boxes.get(head).shift = shift;
  parts[n++] = head;

  const float H = shift + topH;
  const float D = botD;
  const Handle vb = _boxes.makeList(BoxKind::VList, parts, static_cast<u16>(n),
                                    w, H, D);
  if (!valid(vb)) return NO_NODE;
  // Centre on the axis, exactly as a single delimiter is centred.
  _boxes.get(vb).shift = (H + D) * 0.5f - H + axis;
  return vb;
}

LayoutResult Layout::run(Handle rootAtom) {
  _failure = ParseError::Ok;
  if (!valid(rootAtom)) return {NO_NODE, false, ParseError::OutOfMemory};
  if (!_boxes.ok()) return {NO_NODE, false, ParseError::OutOfMemory};

  const u16 nc = _nodes.count();
  Handle* boxOf = _arena.allocArray<Handle>(nc == 0 ? 1 : nc);
  const u32 workCap = static_cast<u32>(nc) * 2u + 8u;
  WorkItem* work = _arena.allocArray<WorkItem>(workCap);
  if (boxOf == nullptr || work == nullptr) {
    return {NO_NODE, false, ParseError::OutOfMemory};
  }
  for (u16 i = 0; i < nc; ++i) boxOf[i] = NO_NODE;

  const FontParams& p = fontParams();

  u32 sp = 0;
  // A formula starts in display style, which is what MicroTeX's default
  // Environment uses and therefore what the differential compares against.
  work[sp++] = WorkItem{rootAtom, 0, _size, TexStyle::Display};
  bool failed = false;

  while (sp > 0 && !failed) {
    const WorkItem it = work[--sp];
    const Node& n = _nodes.get(it.atom);
    if (it.phase == 0) {
      if (sp >= workCap) {
        failed = true;
        break;
      }
      work[sp++] = WorkItem{it.atom, 1, it.size, it.style};
      // Size follows from style, not from a per-level multiplication: the
      // three sizes are 1.0, 0.7 and 0.5 of the formula's em, and
      // scriptscript is the floor. Nesting a script inside a script inside a
      // script does not keep shrinking.
      auto push = [&](Handle c, TexStyle st) {
        if (!valid(c) || failed) return;
        if (sp >= workCap) {
          failed = true;
          return;
        }
        work[sp++] = WorkItem{c, 0, _size * styleSizeFactor(st), st};
      };
      switch (n.kind) {
        case Kind::Row:
          for (u16 i = 0; i < n.children.count; ++i)
            push(_nodes.child(it.atom, i), it.style);
          break;
        case Kind::Frac:
          push(n.frac.num, numeratorStyle(it.style));
          push(n.frac.den, denominatorStyle(it.style));
          break;
        case Kind::Script:
          push(n.script.base, it.style);
          push(n.script.sup, superscriptStyle(it.style));
          push(n.script.sub, subscriptStyle(it.style));
          break;
        case Kind::Sqrt:
          // The radicand is cramped: there is a vinculum over it.
          push(n.sqrt.base, crampStyle(it.style));
          push(n.sqrt.index, rootIndexStyle(it.style));
          break;
        case Kind::Matrix: {
          // The span length is the authoritative cell count -- it describes
          // the memory actually reserved. rows*cols is a second source of
          // truth for the same number, and this walk runs before combine()'s
          // bounds check.
          const u16 total = n.matrix.cells.count;
          for (u16 k = 0; k < total; ++k)
            push(_nodes.cellAt(it.atom, k), it.style);
          break;
        }
        default:
          break;
      }
    } else {
      const Handle b = combine(it.atom, n, boxOf, it.size, it.style);
      if (!valid(b)) {
        failed = true;
        break;
      }
      boxOf[it.atom] = b;
    }
  }

  if (failed) {
    return {NO_NODE, false,
            _failure != ParseError::Ok ? _failure : ParseError::OutOfMemory};
  }
  return {boxOf[rootAtom], true, ParseError::Ok};
}

}  // namespace statex
