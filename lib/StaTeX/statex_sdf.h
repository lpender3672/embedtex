#ifndef STATEX_SDF_H
#define STATEX_SDF_H

#include "statex_glyphstore.h"
#include "statex_types.h"

namespace statex {

/** Pixel dimensions of a rendered glyph coverage bitmap. */
struct GlyphCoverage {
  int w;
  int h;
};

/**
 * Render a glyph's SDF (STX-FNT-02/03) to an 8-bit coverage bitmap at the given
 * em pixel size. Bounded and heap-free: the output is `w*h` bytes written into
 * `out`; if that exceeds `cap` the call refuses (returns false — STX-MEM-03).
 *
 * Coverage comes from bilinear sampling of the SDF and a ~1px antialiasing ramp
 * about the contour (STX-FNT-03): 255 = fully inside, 0 = fully outside.
 *
 * @param g        glyph record (from findGlyphRecord)
 * @param sdfBase  glyphSdfData() (the shared blob; g.sdfOffset indexes into it)
 * @param spread256 glyphSdfSpread() — em/256 spanned per 127 encoded levels
 * @param emPx     target em size in pixels
 * @param out      caller buffer of at least `cap` bytes
 * @param cap      capacity of `out`
 * @param cov      receives the output dimensions
 */
bool renderGlyphCoverage(const GlyphRecord& g, const u8* sdfBase, int spread256,
                         int emPx, u8* out, int cap, GlyphCoverage* cov);

}  // namespace statex

#endif  // STATEX_SDF_H
