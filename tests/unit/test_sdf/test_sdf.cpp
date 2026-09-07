// SDF sampler (STX-FNT-02/03): coverage, antialiasing, bounds.
#include <unity.h>
#include <statex_sdf.h>
#include <statex_glyphstore.h>

using namespace statex;

void setUp() {}
void tearDown() {}

static void test_real_glyph_renders_coverage() {
  const GlyphRecord* g = findGlyphRecord(Face::Bold, 'A');
  TEST_ASSERT_NOT_NULL(g);
  static u8 buf[64 * 64];
  GlyphCoverage cov{};
  bool ok = renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), 24, buf,
                                sizeof(buf), &cov);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_GREATER_THAN_INT(0, cov.w);
  TEST_ASSERT_GREATER_THAN_INT(0, cov.h);
  // A real glyph has both fully-covered and empty pixels.
  u8 lo = 255, hi = 0;
  for (int i = 0; i < cov.w * cov.h; ++i) {
    if (buf[i] < lo) lo = buf[i];
    if (buf[i] > hi) hi = buf[i];
  }
  TEST_ASSERT_LESS_THAN_UINT(40, lo);       // some near-empty
  // Interiors must saturate to full coverage (the dimness fix): glyph ink
  // should be as bright as a solid rule, not a washed-out alpha.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT(250, hi);
}

static void test_oversized_refuses() {
  const GlyphRecord* g = findGlyphRecord(Face::Bold, 'A');
  TEST_ASSERT_NOT_NULL(g);
  u8 tiny[16];
  GlyphCoverage cov{};
  // At 200px em the box far exceeds 16 bytes -> refuse (STX-MEM-03).
  bool ok = renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), 200, tiny,
                                sizeof(tiny), &cov);
  TEST_ASSERT_FALSE(ok);
}

static void test_scale_invariance_of_shape() {
  // The same glyph at two sizes should both be non-empty and scale ~2x.
  const GlyphRecord* g = findGlyphRecord(Face::Roman, 'x');
  TEST_ASSERT_NOT_NULL(g);
  static u8 a[64 * 64], b[128 * 128];
  GlyphCoverage ca{}, cb{};
  TEST_ASSERT_TRUE(renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), 20,
                                       a, sizeof(a), &ca));
  TEST_ASSERT_TRUE(renderGlyphCoverage(*g, glyphSdfData(), glyphSdfSpread(), 40,
                                       b, sizeof(b), &cb));
  TEST_ASSERT_GREATER_THAN_INT(0, ca.w);
  // Doubling emPx roughly doubles the pixel box.
  TEST_ASSERT_INT_WITHIN(2, ca.w * 2, cb.w);
  TEST_ASSERT_INT_WITHIN(2, ca.h * 2, cb.h);
}

static void test_synthetic_sdf_center_inside() {
  // A 3x3 SDF: center fully inside (255), edges outside (0).
  static const u8 sdf[9] = {0, 0, 0, 0, 255, 0, 0, 0, 0};
  GlyphRecord g{};
  g.face = 0;
  g.codepoint = '?';
  g.sdfW = 3;
  g.sdfH = 3;
  g.sdfOffset = 0;
  g.boxW = 256;  // 1 em
  g.boxH = 256;
  static u8 out[16 * 16];
  GlyphCoverage cov{};
  TEST_ASSERT_TRUE(renderGlyphCoverage(g, sdf, 64, 12, out, sizeof(out), &cov));
  // Center pixel should be more covered than a corner pixel.
  const int ci = (cov.h / 2) * cov.w + (cov.w / 2);
  TEST_ASSERT_GREATER_THAN_UINT(out[0], out[ci]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_real_glyph_renders_coverage);
  RUN_TEST(test_oversized_refuses);
  RUN_TEST(test_scale_invariance_of_shape);
  RUN_TEST(test_synthetic_sdf_center_inside);
  return UNITY_END();
}
