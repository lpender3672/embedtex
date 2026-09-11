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

// --- size variants ---------------------------------------------------------

static void test_base_lookup_never_returns_a_variant() {
  // findGlyphRecord must keep meaning "the text-size glyph". If it started
  // returning whichever record the binary search landed on, every ordinary
  // use of a symbol that happens to have a display cut would silently get the
  // large one.
  const GlyphRecord* sum = findGlyphRecord(Face::Symbol, 0x2211);
  TEST_ASSERT_NOT_NULL(sum);
  TEST_ASSERT_EQUAL_UINT(0, sum->variant);
}

static void test_largest_variant_is_the_display_cut() {
  // U+2211 carries a variant sourced from cmex10 slot 88, whose TFM metrics
  // are height 0.100 em and depth 1.500 em -- a total of 1.6 em against the
  // text sigma's ~0.75. The point of the chain is that this is a different
  // design, not the same one scaled, so it must be both larger and distinct.
  const GlyphRecord* base = findGlyphRecord(Face::Symbol, 0x2211);
  const GlyphRecord* big = findLargestGlyphVariant(Face::Symbol, 0x2211);
  TEST_ASSERT_NOT_NULL(base);
  TEST_ASSERT_NOT_NULL(big);
  TEST_ASSERT_TRUE(big != base);
  TEST_ASSERT_EQUAL_UINT(1, big->variant);
  TEST_ASSERT_TRUE(big->height + big->depth > base->height + base->depth);
  // TFM values, not ink: 0.100 and 1.500 em in 1/256ths.
  TEST_ASSERT_INT_WITHIN(2, 26, big->height);
  TEST_ASSERT_INT_WITHIN(2, 384, big->depth);
}

static void test_exact_variant_lookup_returns_that_variant() {
  // The draw walk must fetch the variant layout chose. Asking for "the
  // largest" instead is invisible to a pen-position comparison -- the position
  // is computed from the measured record and stays correct -- while drawing a
  // completely different glyph. That is exactly what happened once radicals
  // grew a four-entry chain: measured with variant 1, drawn with variant 4.
  for (u8 v = 0; v <= 4; ++v) {
    const GlyphRecord* g = findGlyphVariant(Face::Symbol, 0x221A, v);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT(v, g->variant);
  }
  // Past the end of the chain there is no record, and the caller must be told
  // rather than handed the nearest thing. The size chain is 0..4, so 5 is the
  // first value past it -- NOT some larger number, because the variant space
  // is partitioned and everything at kFirstPieceVariant and above is an
  // extensible piece rather than a size step.
  TEST_ASSERT_NULL(findGlyphVariant(Face::Symbol, 0x221A, 5));
  // cmex10 does give the radical an extensible recipe (slots 118/117/116), so
  // the piece slots are populated. They must stay invisible to the size walk:
  // a 0.6 em repeat tile chosen in place of a 3 em radical would be a
  // spectacular layout bug.
  TEST_ASSERT_NOT_NULL(findGlyphVariant(Face::Symbol, 0x221A, kPieceRepeat));
  TEST_ASSERT_TRUE(kPieceRepeat >= kFirstPieceVariant);
  // A glyph with no chain has variant 0 and nothing else.
  TEST_ASSERT_NOT_NULL(findGlyphVariant(Face::Italic, 'x', 0));
  TEST_ASSERT_NULL(findGlyphVariant(Face::Italic, 'x', 1));
}

static void test_radical_chain_grows_monotonically() {
  // Each step must be strictly taller than the last, or "smallest variant that
  // fits" would pick an arbitrary one of two equal entries.
  i16 prev = -1;
  for (u8 v = 0; v <= 4; ++v) {
    const GlyphRecord* g = findGlyphVariant(Face::Symbol, 0x221A, v);
    TEST_ASSERT_NOT_NULL(g);
    const i16 total = static_cast<i16>(g->height + g->depth);
    TEST_ASSERT_TRUE(total > prev);
    prev = total;
  }
}

static void test_variant_at_least_picks_the_smallest_that_fits() {
  // 1.0 em is exactly the base surd's height+depth, so the base must satisfy
  // it; a hair more must step to the next design rather than scaling.
  const GlyphRecord* base = findGlyphVariantAtLeast(Face::Symbol, 0x221A, 256);
  TEST_ASSERT_NOT_NULL(base);
  TEST_ASSERT_EQUAL_UINT(0, base->variant);
  const GlyphRecord* next = findGlyphVariantAtLeast(Face::Symbol, 0x221A, 257);
  TEST_ASSERT_NOT_NULL(next);
  TEST_ASSERT_EQUAL_UINT(1, next->variant);
  // Beyond the chain, the largest available -- TeX would assemble an
  // extensible recipe here, which StaTeX does not do yet.
  const GlyphRecord* biggest =
      findGlyphVariantAtLeast(Face::Symbol, 0x221A, 32000);
  TEST_ASSERT_NOT_NULL(biggest);
  TEST_ASSERT_EQUAL_UINT(4, biggest->variant);
}

static void test_variant_falls_back_to_the_base_when_there_is_no_chain() {
  // A symbol with no display cut must return its text-size record rather than
  // nullptr -- callers ask for "the largest" unconditionally.
  const GlyphRecord* base = findGlyphRecord(Face::Italic, 'x');
  const GlyphRecord* big = findLargestGlyphVariant(Face::Italic, 'x');
  TEST_ASSERT_NOT_NULL(base);
  TEST_ASSERT_EQUAL_PTR(base, big);
}

static void test_variant_sdf_reaches_its_declared_depth() {
  // The display sigma's SDF was originally built on a raster canvas that ended
  // 0.75 em below the baseline, so the bottom half of the glyph was missing --
  // while every metric still read correctly, because the metrics come from the
  // TFM and only the *shape* was truncated. It drew as half a sigma.
  //
  // The real guard is in genfont, which now refuses to emit a glyph whose ink
  // touches the canvas edge. This is the cheap standing check that the emitted
  // atlas actually contains the whole glyph.
  //
  // Not an equality: boxH is the ink bounding box plus SDF padding, while
  // height+depth is TeX's design box, and ink is legitimately a little smaller
  // than the design box. Clipped, this glyph reached 56% of its declared
  // depth; intact it reaches 93%.
  const GlyphRecord* big = findLargestGlyphVariant(Face::Symbol, 0x2211);
  TEST_ASSERT_NOT_NULL(big);
  const int belowBaseline = big->boxH - big->boxY;  // SDF box bottom, +down
  TEST_ASSERT_TRUE(belowBaseline * 100 >= big->depth * 80);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_base_lookup_never_returns_a_variant);
  RUN_TEST(test_largest_variant_is_the_display_cut);
  RUN_TEST(test_exact_variant_lookup_returns_that_variant);
  RUN_TEST(test_radical_chain_grows_monotonically);
  RUN_TEST(test_variant_at_least_picks_the_smallest_that_fits);
  RUN_TEST(test_variant_falls_back_to_the_base_when_there_is_no_chain);
  RUN_TEST(test_variant_sdf_reaches_its_declared_depth);
  RUN_TEST(test_store_nonempty_and_params);
  RUN_TEST(test_lookup_across_faces);
  RUN_TEST(test_absent_returns_null);
  RUN_TEST(test_face_isolation);
  RUN_TEST(test_sdf_has_contour);
  return UNITY_END();
}
