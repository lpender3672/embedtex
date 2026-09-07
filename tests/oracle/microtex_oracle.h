#ifndef STATEX_ORACLE_MICROTEX_ORACLE_H
#define STATEX_ORACLE_MICROTEX_ORACLE_H

// The differential oracle: run a formula through vendored MicroTeX and hand
// back both its layout and a picture of it.
//
// MicroTeX does the parsing, the box model and the placement. We supply the
// one layer it leaves to the platform -- turning "slot s of font f at size z,
// here" into pixels -- and that is done by FreeType against the real Computer
// Modern faces in tests/oracle/fonts/. So the reference is genuine TeX
// output, not an approximation of it: StaTeX's own glyph atlas is not involved
// on this side at all.
//
// That layering is the point. The layer StaTeX reimplements (layout) comes
// from MicroTeX untouched; the layer it shares with every backend (glyph
// rasterisation) is provided identically to both. An image difference is
// therefore a layout difference.
//
// What the oracle does NOT cover, per docs/StaTeX-dev-process.md §4: refusal
// behaviour, arena exhaustion and depth bounding. MicroTeX would render, throw
// or grow the heap rather than refuse, so those stay with the hand-written
// suites.

#include <string>
#include <vector>

#include "ft_raster.h"
#include "microtex_host.h"
#include "stx_render.h"

namespace stxoracle {

/** One MicroTeX render, in MicroTeX's own terms. */
struct OracleRender {
  bool ok = false;
  std::string error;

  std::vector<OracleGlyph> glyphs;
  std::vector<stxtest::RulePlacement> rules;

  int textRuns = 0;  // \text{} runs, which the oracle cannot measure

  float width = 0.0f;
  float height = 0.0f;
  float depth = 0.0f;

  int glyphCount() const { return static_cast<int>(glyphs.size()); }
};

/**
 * Bring MicroTeX up (LaTeX::init) and open the rasteriser. Idempotent.
 * Returns false and fills `error` if either fails, so a suite can report that
 * clearly instead of failing on every case.
 */
bool ensureReady(std::string* error);

/** Typeset `tex` with MicroTeX at the given canvas geometry. */
OracleRender render(const std::string& tex, const stxtest::Canvas& canvas);

/**
 * Draw a render with the real TeX faces. `unrendered` receives the number of
 * glyphs FreeType could not produce -- a picture with holes must be reported,
 * not quietly compared.
 */
stximg::Image rasterise(const OracleRender& r, const stxtest::Canvas& canvas,
                        int* unrendered);

/** Number of TeX faces opened so far, for diagnostics. */
int openFaceCount();

}  // namespace stxoracle

#endif  // STATEX_ORACLE_MICROTEX_ORACLE_H
