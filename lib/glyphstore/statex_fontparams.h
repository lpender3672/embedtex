#ifndef STATEX_FONTPARAMS_H
#define STATEX_FONTPARAMS_H

namespace statex {

/**
 * TeX's font dimension parameters, em-relative, flash-resident (STX-RES-01).
 *
 * Every value here is transcribed from Computer Modern's own tables rather
 * than chosen by eye -- `lib/MicroTeX/res/builtin/tex_param.res.cpp` for the
 * sigma/xi parameters, the `res/font/*.def.cpp` headers for the per-font ones.
 * Layout multiplies them by the current em, which already carries the style's
 * size factor, so `param * size` is exactly MicroTeX's
 * `styleParam(name, style)` (fonts.h:114).
 *
 * Where TeX has a family selected by style -- num1/num2, denom1/denom2,
 * sup1/sup2/sup3, sub1/sub2 -- all of them live here and statex_layout.cpp
 * picks between them. The single hand-picked `supShift` (0.45), `subShift`
 * (0.20) and `fracGap` (0.12) these replaced were not any of the values they
 * stood in for.
 */
struct FontParams {
  // --- sigma/xi parameters -------------------------------------------------
  float axisHeight;      // sigma22: the math axis, above the baseline
  float ruleThickness;   // xi8: default rule thickness
  float xHeight;         // sigma5: every CM face declares 0.430555

  float num1;            // numerator shift, display
  float num2;            // numerator shift, text and below, with a rule
  float denom1;          // denominator shift, display
  float denom2;          // denominator shift, text and below

  float sup1;            // superscript shift, display
  float sup2;            // superscript shift, text and below, uncramped
  float sup3;            // superscript shift, cramped
  float sub1;            // subscript shift, subscript alone
  float sub2;            // subscript shift, with a superscript too

  float supDrop;         // sigma18: superscript baseline below the base's top
  float subDrop;         // sigma19: subscript baseline below the base's bottom

  float bigOpSpacing1;   // xi9:  minimum gap above a display operator's limit
  float bigOpSpacing2;   // xi10: minimum gap below
  float bigOpSpacing3;   // xi11: minimum clearance above
  float bigOpSpacing4;   // xi12: minimum clearance below
  float bigOpSpacing5;   // xi13: padding at the very top and bottom

  // --- lengths that are not font dimensions --------------------------------
  // \scriptspace, the air after a script. NOT em-relative, unlike everything
  // else here: TeX's point is an absolute length, and MicroTeX resolves it as
  // `PIXELS_PER_POINT / size` (unit_conversion.cpp:46) with PIXELS_PER_POINT
  // defaulting to 1, so 0.5pt is half a device pixel whatever the em. Treating
  // it as 0.05 em made it 3.2px at a 64px em instead of 0.5px, which showed up
  // as every scripted subformula being too wide.
  float scriptSpacePx;
  float nullDelimiter;   // 1.2pt padding either side of a fraction
  float matrixColGap;    // atom_matrix.cpp:12, _hsep = 1 em
  float matrixRowGap;    // atom_matrix.cpp:14, _vsep_in = 1 ex
};

constexpr FontParams kMathFont = {
    /*axisHeight*/ 0.25f,
    /*ruleThickness*/ 0.039999f,
    /*xHeight*/ 0.430555f,

    /*num1*/ 0.676508f,
    /*num2*/ 0.393732f,
    /*denom1*/ 0.685951f,
    /*denom2*/ 0.344841f,

    /*sup1*/ 0.412892f,
    /*sup2*/ 0.362892f,
    /*sup3*/ 0.288889f,
    /*sub1*/ 0.15f,
    /*sub2*/ 0.247217f,

    /*supDrop*/ 0.386108f,
    /*subDrop*/ 0.05f,

    /*bigOpSpacing1*/ 0.111112f,
    /*bigOpSpacing2*/ 0.166667f,
    /*bigOpSpacing3*/ 0.2f,
    /*bigOpSpacing4*/ 0.6f,
    /*bigOpSpacing5*/ 0.1f,

    /*scriptSpacePx*/ 0.5f,
    // 1.2pt at a 10pt design size.
    /*nullDelimiter*/ 0.12f,
    /*matrixColGap*/ 1.0f,
    /*matrixRowGap*/ 0.430555f,
};

// A canary for the one way this table can go wrong silently.
//
// The initialiser above is positional, and C++17 has no designated
// initialisers to pin it. Removing a struct field without removing its
// initialiser (or the reverse) shifts every value after it by one, and the
// compiler says nothing: a short list just value-initialises the tail to zero.
// That happened once while deleting `num3`, and the only symptom was a matrix
// test failing for no visible reason.
//
// Asserting the LAST field is non-zero catches exactly that: any shift leaves
// it at zero. It is not a proof of alignment, but it turns the silent failure
// mode into a build error, which is the part that matters.
static_assert(kMathFont.matrixRowGap > 0.0f,
              "FontParams initialiser is out of step with the struct: a field "
              "was added or removed on only one side, so every value after it "
              "has shifted.");

inline const FontParams& fontParams() { return kMathFont; }

}  // namespace statex

#endif  // STATEX_FONTPARAMS_H
