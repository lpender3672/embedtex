// Was RED (defect SDF-ROUNDING.
//
// lib/StaTeX/statex_sdf.cpp:35-36 computes the coverage bitmap size as
//
//     const int w = static_cast<int>((g.boxW * emPx) / 256 + 0.5f);
//
// `g.boxW` is i16 and `emPx` is int, so `(g.boxW * emPx) / 256` is integer
// division: it truncates before the `+ 0.5f` is ever applied. The rounding the
// expression is clearly reaching for does not happen, and every glyph comes
// out up to one pixel narrower and shorter than intended.
//
// The fix is to do the division in floating point:
//     static_cast<int>(g.boxW * static_cast<float>(emPx) / 256.0f + 0.5f)
//
// These tests assert the rounded size, so they go green with that change.

#include <unity.h>

#include <cmath>
#include <cstdio>

#include "statex_glyphstore.h"
#include "statex_sdf.h"
#include "stx_render.h"

using namespace statex;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kCovCap = 256 * 256;
u8 g_cov[kCovCap];

int roundedExtent(int box256, int emPx) {
  return static_cast<int>(static_cast<float>(box256) *
                              static_cast<float>(emPx) / 256.0f +
                          0.5f);
}

}  // namespace

static void test_coverage_size_is_rounded_not_truncated() {
  const GlyphRecord* g = findGlyphRecord(Face::Roman, U'x');
  TEST_ASSERT_NOT_NULL(g);

  const int sizes[] = {7, 9, 11, 13, 16, 22, 28, 40, 64};
  for (int emPx : sizes) {
    GlyphCoverage cov{};
    const bool ok = renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(),
                                        emPx, g_cov, kCovCap, &cov);
    TEST_ASSERT_TRUE(ok);
    const int wantW = roundedExtent(g->boxW, emPx);
    const int wantH = roundedExtent(g->boxH, emPx);
    if (cov.w != wantW || cov.h != wantH) {
      std::printf("  emPx=%2d: got %dx%d, rounded would be %dx%d\n", emPx,
                  cov.w, cov.h, wantW, wantH);
    }
    TEST_ASSERT_EQUAL_INT(wantW, cov.w);
    TEST_ASSERT_EQUAL_INT(wantH, cov.h);
  }
}

static void test_rounding_holds_across_every_glyph_in_the_atlas() {
  // One size, every glyph: catches the case where only some box dimensions
  // happen to divide evenly by 256.
  const int emPx = 22;
  int mismatches = 0;
  for (int face = 0; face < 5; ++face) {
    for (c32 cp = 0x20; cp < 0x2300; ++cp) {
      const GlyphRecord* g = findGlyphRecord(static_cast<Face>(face), cp);
      if (g == nullptr) continue;
      GlyphCoverage cov{};
      if (!renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), emPx,
                               g_cov, kCovCap, &cov)) {
        continue;
      }
      if (cov.w != roundedExtent(g->boxW, emPx) ||
          cov.h != roundedExtent(g->boxH, emPx)) {
        mismatches++;
      }
    }
  }
  std::printf("  %d glyphs sized differently from the rounded value at %dpx\n",
              mismatches, emPx);
  TEST_ASSERT_EQUAL_INT(0, mismatches);
}

static void test_coverage_scales_monotonically_with_size() {
  // Not defective today, but it pins the property the fix must preserve: a
  // larger em must never produce a smaller bitmap.
  const GlyphRecord* g = findGlyphRecord(Face::Roman, U'm');
  TEST_ASSERT_NOT_NULL(g);
  int prevW = -1, prevH = -1;
  for (int emPx = 6; emPx <= 80; ++emPx) {
    GlyphCoverage cov{};
    if (!renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), emPx, g_cov,
                             kCovCap, &cov)) {
      break;
    }
    TEST_ASSERT_TRUE(cov.w >= prevW);
    TEST_ASSERT_TRUE(cov.h >= prevH);
    prevW = cov.w;
    prevH = cov.h;
  }
}

static void test_coverage_aspect_tracks_the_glyph_box() {
  // The relative error from truncation is worst at small sizes, which is
  // exactly where scripts live (scriptMinPx is 7). Assert the rendered aspect
  // ratio stays within half a pixel of the box aspect.
  const GlyphRecord* g = findGlyphRecord(Face::Roman, U'x');
  TEST_ASSERT_NOT_NULL(g);
  for (int emPx : {7, 8, 9, 10}) {
    GlyphCoverage cov{};
    TEST_ASSERT_TRUE(renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(),
                                         emPx, g_cov, kCovCap, &cov));
    const double wantW = static_cast<double>(g->boxW) * emPx / 256.0;
    const double wantH = static_cast<double>(g->boxH) * emPx / 256.0;
    if (std::fabs(cov.w - wantW) > 0.5 || std::fabs(cov.h - wantH) > 0.5) {
      std::printf("  emPx=%d: %dx%d vs exact %.2fx%.2f\n", emPx, cov.w, cov.h,
                  wantW, wantH);
    }
    TEST_ASSERT_TRUE(std::fabs(cov.w - wantW) <= 0.5);
    TEST_ASSERT_TRUE(std::fabs(cov.h - wantH) <= 0.5);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_coverage_size_is_rounded_not_truncated);
  RUN_TEST(test_rounding_holds_across_every_glyph_in_the_atlas);
  RUN_TEST(test_coverage_scales_monotonically_with_size);
  RUN_TEST(test_coverage_aspect_tracks_the_glyph_box);
  return UNITY_END();
}
