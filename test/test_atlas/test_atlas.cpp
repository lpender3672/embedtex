// Glyph atlas in flash (STX-RES-03): lookup + pixel access, no file I/O.
#include <unity.h>
#include <statex_atlas.h>

using namespace statex;

void setUp() {}
void tearDown() {}

static void test_lookup_known() {
  const GlyphInfo* x = findGlyph('x');
  TEST_ASSERT_NOT_NULL(x);
  TEST_ASSERT_EQUAL_UINT(5, x->width);
  TEST_ASSERT_EQUAL_UINT(7, x->height);
  TEST_ASSERT_EQUAL_UINT(6, x->advance);
}

static void test_lookup_unknown() {
  TEST_ASSERT_NULL(findGlyph('z'));
  TEST_ASSERT_NULL(findGlyph(0x03B1));  // alpha not in seed atlas
}

static void test_pixels_of_x() {
  const GlyphInfo* g = findGlyph('x');
  TEST_ASSERT_NOT_NULL(g);
  // 'x' top row 0x11 = 10001 -> corners set, middle clear
  TEST_ASSERT_TRUE(glyphPixel(*g, 0, 0));
  TEST_ASSERT_FALSE(glyphPixel(*g, 1, 0));
  TEST_ASSERT_TRUE(glyphPixel(*g, 4, 0));
  // center row 2 (0x04 = 00100) -> middle set
  TEST_ASSERT_TRUE(glyphPixel(*g, 2, 2));
  TEST_ASSERT_FALSE(glyphPixel(*g, 0, 2));
}

static void test_pixel_out_of_bounds() {
  const GlyphInfo* g = findGlyph('x');
  TEST_ASSERT_FALSE(glyphPixel(*g, -1, 0));
  TEST_ASSERT_FALSE(glyphPixel(*g, 5, 0));
  TEST_ASSERT_FALSE(glyphPixel(*g, 0, 7));
}

static void test_plus_glyph() {
  const GlyphInfo* g = findGlyph('+');
  TEST_ASSERT_NOT_NULL(g);
  // middle row 0x1F = 11111
  for (int x = 0; x < 5; ++x) TEST_ASSERT_TRUE(glyphPixel(*g, x, 3));
  // vertical stem column 2 set on row 1
  TEST_ASSERT_TRUE(glyphPixel(*g, 2, 1));
  TEST_ASSERT_FALSE(glyphPixel(*g, 0, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lookup_known);
  RUN_TEST(test_lookup_unknown);
  RUN_TEST(test_pixels_of_x);
  RUN_TEST(test_pixel_out_of_bounds);
  RUN_TEST(test_plus_glyph);
  return UNITY_END();
}
