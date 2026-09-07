#ifndef STATEX_STYLE_H
#define STATEX_STYLE_H

// TeX's math style.
//
// Every subformula is set in one of eight styles, and the style decides four
// things that StaTeX previously hard-coded:
//
//   * which font parameter applies -- TeX has num1/num2/num3, denom1/denom2,
//     sup1/sup2/sup3 and sub1/sub2 where StaTeX had one constant each;
//   * what size the glyphs are set at -- three discrete sizes, not a factor
//     applied once per nesting level;
//   * whether a big operator takes its display cut;
//   * whether limits go above and below an operator or beside it.
//
// The eight values are four size classes each in an uncramped and a cramped
// form. Cramped means "there is something above me" -- under a radical, in a
// denominator -- and it lowers superscripts. The numbering is load-bearing:
// even is uncramped, odd is cramped, and `index / 2` is the size class, which
// is why every derivation below is integer arithmetic on the index.
//
// Mirrors lib/MicroTeX/utils/enums.h:76 and the Environment::*Style() family
// at lib/MicroTeX/core/core.cpp:215-259.

#include "statex_spacing.h"
#include "statex_types.h"

namespace statex {

enum class TexStyle : u8 {
  Display = 0,
  DisplayCramped,
  Text,
  TextCramped,
  Script,
  ScriptCramped,
  ScriptScript,
  ScriptScriptCramped,
};

constexpr u8 styleIndex(TexStyle s) { return static_cast<u8>(s); }
constexpr TexStyle styleFromIndex(int i) {
  return static_cast<TexStyle>(i < 0 ? 0 : (i > 7 ? 7 : i));
}

/** Cramped: `s % 2 == 1 ? s : s + 1`. */
constexpr TexStyle crampStyle(TexStyle s) {
  const int i = styleIndex(s);
  return styleFromIndex((i % 2 == 1) ? i : i + 1);
}

/** Numerator: `s + 2 - 2 * (s / 6)`. One size down, cramping preserved. */
constexpr TexStyle numeratorStyle(TexStyle s) {
  const int i = styleIndex(s);
  return styleFromIndex(i + 2 - 2 * (i / 6));
}

/** Denominator: `2 * (s / 2) + 1 + 2 - 2 * (s / 6)`. One size down, cramped. */
constexpr TexStyle denominatorStyle(TexStyle s) {
  const int i = styleIndex(s);
  return styleFromIndex(2 * (i / 2) + 1 + 2 - 2 * (i / 6));
}

/** Superscript: `2 * (s / 4) + 4 + (s % 2)`. Script size, cramping preserved. */
constexpr TexStyle superscriptStyle(TexStyle s) {
  const int i = styleIndex(s);
  return styleFromIndex(2 * (i / 4) + 4 + (i % 2));
}

/** Subscript: `2 * (s / 4) + 5`. Script size, always cramped. */
constexpr TexStyle subscriptStyle(TexStyle s) {
  const int i = styleIndex(s);
  return styleFromIndex(2 * (i / 4) + 5);
}

/** A root index is always scriptscript, however deep it already was. */
constexpr TexStyle rootIndexStyle(TexStyle) { return TexStyle::ScriptScript; }

/**
 * Glyph size relative to the formula's base em: display and text both 1.0,
 * script 0.7, scriptscript 0.5.
 *
 * Note this is a *lookup*, not a repeated multiplication. StaTeX used to
 * multiply by 0.7 at every nesting level with a floor in pixels, so
 * `x^{y^{z^{w}}}` at 64px ran 64 -> 44.8 -> 31.4 -> 22 where TeX runs
 * 64 -> 44.8 -> 32 -> 32: scriptscript is the floor as a *style*, and no
 * amount of further nesting goes below it.
 */
constexpr float styleSizeFactor(TexStyle s) {
  const int c = styleIndex(s) / 2;
  return c < 2 ? 1.0f : (c == 2 ? 0.7f : 0.5f);
}

/** Display style proper -- TeX writes this test as `style < text`. */
constexpr bool isDisplayStyle(TexStyle s) { return styleIndex(s) < 2; }

constexpr bool isCrampedStyle(TexStyle s) { return (styleIndex(s) % 2) == 1; }

/** The size class, which is what the inter-atom spacing table is indexed by. */
constexpr StyleClass spacingClassOf(TexStyle s) {
  return static_cast<StyleClass>(styleIndex(s) / 2);
}

}  // namespace statex

#endif  // STATEX_STYLE_H
