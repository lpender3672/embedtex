// Verifies the generated glyph store (STX-FNT-01/04): the constexpr tables
// compile, lookup works across faces, and the SDF data is well-formed.
#include <unity.h>
#include <statex_glyphstore.h>

using namespace statex;

void setUp() {}
void tearDown() {}

static void test_store_nonempty_and_params() {
  TEST_ASSERT_GREATER_THAN_INT(0, glyphRecordCount());
  TEST_ASSERT_EQUAL_INT(256, glyphEmPx());
  TEST_ASSERT_GREATER_THAN_INT(0, glyphSdfSpread());
  TEST_ASSERT_NOT_NULL(glyphSdfData());
}

static void test_lookup_across_faces() {
  const GlyphRecord* x = findGlyphRecord(Face::Roman, 'x');
  TEST_ASSERT_NOT_NULL(x);
  TEST_ASSERT_EQUAL_UINT('x', x->codepoint);
  TEST_ASSERT_EQUAL_UINT((unsigned)Face::Roman, x->face);
  TEST_ASSERT_GREATER_THAN_INT(0, x->advance);   // positive advance
  TEST_ASSERT_GREATER_THAN_INT(0, x->height);    // ink above baseline
  TEST_ASSERT_GREATER_THAN_INT(0, x->sdfW);
  TEST_ASSERT_GREATER_THAN_INT(0, x->sdfH);

  TEST_ASSERT_NOT_NULL(findGlyphRecord(Face::Italic, 'x'));
  TEST_ASSERT_NOT_NULL(findGlyphRecord(Face::Bold, 'A'));
  TEST_ASSERT_NOT_NULL(findGlyphRecord(Face::Symbol, 0x03B1));  // alpha
  TEST_ASSERT_NOT_NULL(findGlyphRecord(Face::Symbol, 0x221A));  // sqrt sign
}

static void test_absent_returns_null() {
  TEST_ASSERT_NULL(findGlyphRecord(Face::Roman, 0x4E2D));   // a CJK char
  TEST_ASSERT_NULL(findGlyphRecord(Face::Blackboard, 'x')); // not in bb set
}

static void test_face_isolation() {
  // Same codepoint in different faces resolves to distinct records.
  const GlyphRecord* r = findGlyphRecord(Face::Roman, 'A');
  const GlyphRecord* i = findGlyphRecord(Face::Italic, 'A');
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_NOT_NULL(i);
  TEST_ASSERT_TRUE(r != i);
  TEST_ASSERT_EQUAL_UINT((unsigned)Face::Roman, r->face);
  TEST_ASSERT_EQUAL_UINT((unsigned)Face::Italic, i->face);
}

static void test_sdf_has_contour() {
  // The SDF for a real glyph must contain both inside (>128) and outside
  // (<128) texels — i.e. the contour is captured with padding around it.
  const GlyphRecord* g = findGlyphRecord(Face::Bold, 'A');
  TEST_ASSERT_NOT_NULL(g);
  const u8* data = glyphSdfData() + g->sdfOffset;
  const int n = g->sdfW * g->sdfH;
  u8 lo = 255, hi = 0;
  for (int k = 0; k < n; ++k) {
    if (data[k] < lo) lo = data[k];
    if (data[k] > hi) hi = data[k];
  }
  TEST_ASSERT_LESS_THAN_UINT(128, lo);      // some outside
  TEST_ASSERT_GREATER_THAN_UINT(128, hi);   // some inside
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_store_nonempty_and_params);
  RUN_TEST(test_lookup_across_faces);
  RUN_TEST(test_absent_returns_null);
  RUN_TEST(test_face_isolation);
  RUN_TEST(test_sdf_has_contour);
  return UNITY_END();
}
