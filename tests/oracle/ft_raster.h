#ifndef STATEX_ORACLE_FT_RASTER_H
#define STATEX_ORACLE_FT_RASTER_H

// Rasterises TeX font slots with FreeType, using the real Computer Modern
// faces in tests/oracle/fonts/.
//
// This is what makes the reference trustworthy. MicroTeX does not hand us a
// character -- it hands us "slot 0x32 of cmex10 at this size, here". Those
// slots are TeX font positions, and for the growing glyphs (radicals, tall
// delimiters) they are *different glyphs*, not scaled ones: a size-variant
// family plus, past its largest member, separate top/extension/bottom pieces.
//
// The earlier approach drew those with StaTeX's own SDF atlas, which has one
// radical and one of each bracket. Every variant had to be faked by stretching
// the one glyph available, which looked wrong and needed a pile of machinery
// (a learned slot->Unicode map, metric-derived scaling, piece merging) to look
// wrong slightly less. Drawing the actual glyph from the actual font deletes
// all of that and is exact.

#include <string>

#include "stx_image.h"

namespace stxoracle {

/** One glyph MicroTeX asked to be drawn, in its own terms. */
struct OracleGlyph {
  std::string fontPath;  // as MicroTeX names it, e.g. "res/fonts/base/cmex10.ttf"
  unsigned slot = 0;     // TeX font position, NOT Unicode
  float x = 0.0f;        // pen origin, device px
  float baselineY = 0.0f;
  float emPx = 0.0f;
};

/**
 * Loads TeX faces on demand and draws slots into an Image.
 *
 * One instance holds the open faces; construct it once per process. Failures
 * are counted rather than thrown so a missing face degrades the picture
 * visibly instead of taking the suite down.
 */
class FreeTypeRasteriser {
 public:
  FreeTypeRasteriser();
  ~FreeTypeRasteriser();
  FreeTypeRasteriser(const FreeTypeRasteriser&) = delete;
  FreeTypeRasteriser& operator=(const FreeTypeRasteriser&) = delete;

  /** False if FreeType itself could not start; `error()` says why. */
  bool ok() const;
  const std::string& error() const;

  /**
   * Draw one glyph. Returns false if the face or slot could not be rendered,
   * having drawn nothing.
   */
  bool draw(const OracleGlyph& g, stximg::Image& target);

  /** Faces opened so far, for diagnostics. */
  int faceCount() const;

 private:
  struct Impl;
  Impl* _impl;
};

/** Where the TeX faces live. Set at build time; override with STATEX_TEX_FONTS. */
std::string texFontRoot();

}  // namespace stxoracle

#endif  // STATEX_ORACLE_FT_RASTER_H
