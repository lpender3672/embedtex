// The A8 coverage backend must produce exactly what the host image path does.
//
// This is the phase that proves the LVGL rendering idea without LVGL and
// without hardware: if CoverageGraphics and ImageGraphics disagree on a single
// byte, the formula LVGL displays is not the formula the oracle validated.
//
// Both max-composite glyph coverage and both round rule edges independently,
// so the correct assertion is byte equality, not similarity.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "statex_a8.h"
#include "statex_render.h"
#include "stx_corpus.h"
#include "stx_image.h"
#include "stx_png.h"
#include "stx_render.h"

using namespace statex;
using namespace stxtest;

void setUp() {}
void tearDown() {}

static u8 g_scratch[256 * 1024];

/** Render `tex` through CoverageGraphics onto a canvas-sized grid. */
static std::vector<u8> renderA8(const std::u32string& tex, const Canvas& c,
                                ParseError* err, long* clipped) {
  std::vector<u8> buf(
      static_cast<size_t>(c.width) * static_cast<size_t>(c.height), 0);
  backends::CoverageGraphics g(buf.data(), c.width, c.height, c.width);
  Renderer r(g_scratch, sizeof(g_scratch));
  *err = r.render(tex.data(), static_cast<int>(tex.size()), c.sizePx,
                  c.originX, c.baseline, g);
  *clipped = g.clipped();
  return buf;
}

static void compareOne(const char* id, const std::u32string& tex, float em) {
  // Size the canvas from the formula rather than using the suite default,
  // which is too small for the larger ems and would clip both paths equally
  // -- hiding, rather than exposing, any disagreement at the edges. This is
  // what Renderer::measure() is for.
  Canvas c;
  c.sizePx = em;
  {
    Renderer m(g_scratch, sizeof(g_scratch));
    RenderStats st{};
    if (m.measure(tex.data(), static_cast<int>(tex.size()), em, &st) ==
        ParseError::Ok) {
      // Generous, and proportional: measure() reports the box tree's extents,
      // but a glyph's ink can overhang them -- italic overhang and side
      // bearings both do. A fixed 8px pad clipped a few corpus cases.
      const int pad = static_cast<int>(em);
      c.originX = static_cast<float>(pad);
      c.baseline = st.height + static_cast<float>(pad);
      c.width = static_cast<int>(st.width) + 2 * pad + 2;
      c.height = static_cast<int>(st.height + st.depth) + 2 * pad + 2;
    }
  }

  const RenderOutput ref = renderStatex(tex, c);
  ParseError e = ParseError::Ok;
  long clipped = 0;
  const std::vector<u8> a8 = renderA8(tex, c, &e, &clipped);

  // Refusals must agree, or the two paths are not running the same pipeline.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ref.error), static_cast<int>(e));
  if (e != ParseError::Ok) return;

  // Both paths share a canvas, so ink falling outside it falls outside for
  // both; requiring zero would just be a statement about the padding. What
  // must not happen is one path losing ink the other kept, and clipping the
  // same amount is the direct check of that. Disagreement *inside* the canvas
  // is caught by the byte comparison below.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ref.image.clipped),
                        static_cast<int>(clipped));

  size_t diff = 0;
  int worst = 0;
  for (size_t i = 0; i < a8.size(); ++i) {
    const int d = static_cast<int>(a8[i]) - static_cast<int>(ref.image.px[i]);
    const int ad = d < 0 ? -d : d;
    if (ad != 0) {
      diff++;
      if (ad > worst) worst = ad;
    }
  }

  if (diff != 0) {
    // Dump both so a human can see which glyph moved, same convention as the
    // oracle's failure pictures.
    stximg::RgbImage out(c.width * 2 + 8, c.height, 0);
    for (int y = 0; y < c.height; ++y) {
      for (int x = 0; x < c.width; ++x) {
        const u8 r = ref.image.at(x, y);
        const u8 a = a8[static_cast<size_t>(y) * static_cast<size_t>(c.width) +
                        static_cast<size_t>(x)];
        out.set(x, y, r, r, r);
        out.set(x + c.width + 8, y, a, a, a);
      }
    }
    stximg::writePng(out, artifactPath(std::string("a8_") + id + ".png"));
    char msg[192];
    std::snprintf(msg, sizeof(msg),
                  "%s @%gpx: %zu bytes differ (worst %d); see artifacts",
                  id, static_cast<double>(em), diff, worst);
    TEST_FAIL_MESSAGE(msg);
  }
}

// A formula per structural feature, at the sizes the oracle uses.
static void test_a8_matches_image_path() {
  struct Sample { const char* id; const char32_t* tex; };
  static const Sample cases[] = {
      {"char", U"x"},
      {"frac", U"\\frac{x^2+1}{2}"},
      {"sqrt", U"\\sqrt{1+\\sqrt{1+x}}"},
      {"script", U"\\alpha^2_i"},
      {"bigop", U"\\sum_{k=1}^{n}\\phi_{k}"},
      {"matrix", U"\\begin{bmatrix}\\frac{a}{b}&\\sqrt{c}\\\\d&e\\end{bmatrix}"},
      {"showcase",
       U"\\mathbf{A}=\\begin{bmatrix}\\frac{x^2+1}{2}&\\sqrt{\\omega}\\\\"
       U"\\alpha^2_i&\\sum\\leq\\infty\\end{bmatrix}"},
  };
  for (const Sample& k : cases) {
    for (float em : {22.0f, 36.0f, 64.0f}) compareOne(k.id, k.tex, em);
  }
}

// Every corpus case, at the device size. Broader but with one assertion, so a
// regression names the case that broke.
static void test_a8_matches_over_corpus() {
  for (const Case& k : corpus()) {
    // Skip cases whose specified behaviour StaTeX does not yet deliver; the
    // named red suite owns those. Both paths still have to agree on whatever
    // today's behaviour is, but a green suite should not assert it.
    if (!isSettled(k)) continue;
    compareOne(k.id, tex32(k.tex), 22.0f);
  }
}

// Overlapping glyphs must accumulate, not overwrite. Two coverage blits at the
// same spot: the brighter must survive.
static void test_overlap_takes_max() {
  u8 buf[16] = {0};
  backends::CoverageGraphics g(buf, 4, 4, 4);
  const u8 bright[4] = {200, 200, 200, 200};
  const u8 dim[4] = {50, 50, 50, 50};
  g.blendCoverage(0, 0, 4, 1, bright);
  g.blendCoverage(0, 0, 4, 1, dim);  // must not erase the bright ink
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_INT(200, buf[i]);
  g.blendCoverage(0, 1, 4, 1, dim);
  g.blendCoverage(0, 1, 4, 1, bright);  // brighter arriving second also wins
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_INT(200, buf[4 + i]);
}

// A glyph placed partly off-grid is clipped, not written out of bounds.
static void test_clipping_is_counted_not_crashed() {
  u8 buf[16];
  std::memset(buf, 0, sizeof(buf));
  backends::CoverageGraphics g(buf, 4, 4, 4);
  const u8 cov[4] = {255, 255, 255, 255};
  g.blendCoverage(-2, 0, 4, 1, cov);
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(g.clipped()));
  TEST_ASSERT_EQUAL_INT(255, buf[0]);
  TEST_ASSERT_EQUAL_INT(255, buf[1]);
  TEST_ASSERT_EQUAL_INT(0, buf[2]);
}

int main(int, char**) {
  clearArtifacts("a8_");
  UNITY_BEGIN();
  RUN_TEST(test_overlap_takes_max);
  RUN_TEST(test_clipping_is_counted_not_crashed);
  RUN_TEST(test_a8_matches_image_path);
  RUN_TEST(test_a8_matches_over_corpus);
  return UNITY_END();
}
