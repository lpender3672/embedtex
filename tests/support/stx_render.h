#ifndef STATEX_TEST_RENDER_H
#define STATEX_TEST_RENDER_H

// Turns a formula into a picture, two ways:
//
//   renderStatex()        - the real StaTeX pipeline end to end, drawing
//                           through a Graphics2D that blends into an Image.
//                           This is the thing under test.
//
// The oracle side has its own renderer (tests/oracle/ft_raster.h), which draws
// the real TeX faces with FreeType. Both produce the same kind of Image, so
// tests/support/stx_compare.h scores them against each other or against a
// frozen golden.

#include <string>
#include <vector>

#include "statex_graphics.h"
#include "statex_parser.h"
#include "statex_render.h"
#include "statex_types.h"
#include "statex_glyphstore.h"
#include "stx_image.h"

namespace stxtest {

/** A filled rectangle (fraction bar, radical vinculum, ...). */
struct RulePlacement {
  float x = 0.0f;
  float top = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

/** Everything one render produced, in both op and pixel form. */
struct RenderOutput {
  statex::ParseError error = statex::ParseError::Ok;
  bool ok() const { return error == statex::ParseError::Ok; }

  stximg::Image image;
  statex::RenderStats stats{};

  int coverageBlits = 0;  // Graphics2D::blendCoverage calls
  int rules = 0;          // Graphics2D::drawRule calls
  int blankBlits = 0;     // blits whose coverage was entirely zero

  std::vector<RulePlacement> rulePlacements;

  // Where layout put each glyph, in emission order. This is the layout-only
  // view: comparing these against the oracle's pen positions asks whether
  // StaTeX placed the glyphs where TeX would, without the answer being
  // contaminated by the two rasterisers disagreeing about ink.
  std::vector<statex::GlyphPlacement> placements;
  bool placementsTruncated = false;  // formula exceeded the probe's capacity
};

/** Canvas geometry shared by every render in the suite, so images line up. */
struct Canvas {
  int width = 640;
  int height = 320;
  float originX = 24.0f;
  float baseline = 200.0f;
  float sizePx = 22.0f;
};

/**
 * A Graphics2D that paints into an Image. Public because the oracle port and
 * a few targeted tests want to drive it directly.
 */
class ImageGraphics : public statex::Graphics2D {
 public:
  explicit ImageGraphics(stximg::Image& target) : _img(target) {}

  void blendCoverage(int x, int y, int w, int h,
                     const statex::u8* cov) override;
  void drawRule(float x, float top, float w, float h) override;

  int blits = 0;
  int rules = 0;
  int blankBlits = 0;
  std::vector<RulePlacement> rulePlacements;

 private:
  stximg::Image& _img;
};

/** Run the real StaTeX pipeline over `tex` and capture the result. */
RenderOutput renderStatex(const std::u32string& tex,
                          const Canvas& canvas = Canvas{});

/** As above but with explicit renderer capacities (for exhaustion tests). */
RenderOutput renderStatex(const std::u32string& tex, const Canvas& canvas,
                          const statex::RenderCaps& caps);

/** Convenience: UTF-8 (ASCII + backslashes) source text to UTF-32. */
std::u32string tex32(const char* ascii);

/** Where a failing comparison should drop its diagnostic PGMs. */
const std::string& artifactDir();
std::string artifactPath(const std::string& name);

// --- resolving a character the way the parser does -------------------------
//
// Three separate test helpers have now measured the wrong glyph by deciding
// for themselves which face a character belongs to, and each time the suite
// reported confident nonsense rather than failing loudly:
//
//   * `advanceSum` assumed Face::Roman, so when letters became math italic it
//     reported *negative* inter-atom glue for every formula.
//   * the same helper then assumed `-` was the ASCII hyphen, and was 0.445 em
//     out once `-` became U+2212.
//   * `advanceOf` in the matrix suite carries the same assumption and has only
//     escaped so far because it is called with letters.
//
// The rule lives in statex_types.h (`defaultMathFace`, `mathModeGlyph`). These
// two apply it, and nothing in tests/ should be deciding it independently.

/** The glyph record the parser would use for an ASCII character, or nullptr. */
const statex::GlyphRecord* mathGlyphRecord(char ascii);

/** Total advance of `text` at `emPx`, with no inter-atom glue. */
double bareAdvanceSum(const char* ascii, float emPx);

/**
 * Delete artifacts whose filename starts with `prefix`, returning how many
 * went. Call it before a suite writes its pictures, so the folder shows this
 * run's failures rather than the union of every run's. Returns 0 if the
 * directory does not exist.
 */
int clearArtifacts(const std::string& prefix);

}  // namespace stxtest

#endif  // STATEX_TEST_RENDER_H
