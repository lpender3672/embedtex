#ifndef STATEX_FONTPARAMS_H
#define STATEX_FONTPARAMS_H

namespace statex {

/**
 * Global font parameters (em-relative), the flash-resident layout constants
 * required by STX-RES-01 (the analogue of TeX's font dimension parameters).
 * Layout multiplies these by the current em pixel size.
 */
struct FontParams {
  float axisHeight;       // math axis above baseline
  float ruleThickness;    // fraction bar thickness
  float fracGap;          // gap between numerator/denominator and the bar
  float supShift;         // superscript baseline raise
  float subShift;         // subscript baseline drop
  float scriptScale;      // size multiplier for scripts
  float scriptMinPx;      // floor on script em pixel size
  float sqrtPad;          // vertical pad above the radicand
  float radicalWidth;     // width of the radical sign
  float matrixColGap;     // horizontal gap between matrix columns
  float matrixRowGap;     // vertical gap between matrix rows
  float delimWidth;       // matrix bracket/paren width
  float fallbackAdvance;  // advance for a glyph missing from the atlas
  float fallbackHeight;   // height for a missing glyph
};

constexpr FontParams kMathFont = {
    /*axisHeight*/ 0.25f,
    /*ruleThickness*/ 0.04f,
    /*fracGap*/ 0.12f,
    /*supShift*/ 0.45f,
    /*subShift*/ 0.20f,
    /*scriptScale*/ 0.70f,
    /*scriptMinPx*/ 7.0f,
    /*sqrtPad*/ 0.10f,
    /*radicalWidth*/ 0.55f,
    /*matrixColGap*/ 0.60f,
    /*matrixRowGap*/ 0.30f,
    /*delimWidth*/ 0.30f,
    /*fallbackAdvance*/ 0.50f,
    /*fallbackHeight*/ 0.70f,
};

inline const FontParams& fontParams() { return kMathFont; }

}  // namespace statex

#endif  // STATEX_FONTPARAMS_H
