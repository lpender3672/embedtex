// Phase 5 — layout (atom tree -> box tree), now metric-driven from the glyph
// store (STX-RES-01). STX-EXE-01 (iterative), STX-DAT-03, STX-MEM-03.
#include <unity.h>
#include <statex_parser.h>
#include <statex_layout.h>
#include <statex_serialize.h>
#include <statex_glyphstore.h>

using namespace statex;

static u8 g_buf[65536];
static const float SIZE = 20.0f;

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

static Handle layoutOf(Arena& a, BoxStore& boxes, const c32* src, int len) {
  NodeStore nodes(a, 1024, 1024);
  Parser p(a, nodes);
  ParseResult pr = p.parse(src, len);
  if (pr.error != ParseError::Ok) return NO_NODE;
  Layout lay(a, nodes, boxes, SIZE);
  LayoutResult lr = lay.run(pr.root);
  return lr.ok ? lr.rootBox : NO_NODE;
}

static void structEq(BoxStore& boxes, Handle root, const char* expect) {
  char out[512];
  int n = serializeBox(boxes, root, out, sizeof(out));
  TEST_ASSERT_TRUE(n >= 0);
  TEST_ASSERT_EQUAL_STRING(expect, out);
}

static void test_char_metrics_from_glyph_store() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"x", 1);
  TEST_ASSERT_TRUE(valid(r));
  const Box& b = boxes.get(r);
  TEST_ASSERT_EQUAL_INT((int)BoxKind::Char, (int)b.kind);
  TEST_ASSERT_EQUAL_INT((int)Face::Roman, (int)b.face);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, SIZE, b.emPx);
  // Width/height come from the glyph record, not a uniform constant.
  const GlyphRecord* g = findGlyphRecord(Face::Roman, 'x');
  TEST_ASSERT_NOT_NULL(g);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, g->advance / 256.0f * SIZE, b.width);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, g->height / 256.0f * SIZE, b.height);
}

static void test_row_width_is_sum() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"xy", 2);
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(H x y)");
  const Box& b = boxes.get(r);
  const GlyphRecord* gx = findGlyphRecord(Face::Roman, 'x');
  const GlyphRecord* gy = findGlyphRecord(Face::Roman, 'y');
  const float expect = (gx->advance + gy->advance) / 256.0f * SIZE;
  TEST_ASSERT_FLOAT_WITHIN(0.2f, expect, b.width);
}

static void test_frac_structure_and_depth() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\frac{a}{b}", slen(U"\\frac{a}{b}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(V a R b)");
  const Box& b = boxes.get(r);
  TEST_ASSERT_EQUAL_INT((int)BoxKind::VList, (int)b.kind);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, b.height);  // numerator above
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, b.depth);   // denominator below
}

static void test_superscript_structure_and_scale() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"x^2", 3);
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(H x 2)");
  // The superscript box renders at a smaller em than the base.
  const Box& root = boxes.get(r);
  const Box& base = boxes.get(boxes.child(r, 0));
  const Box& sup = boxes.get(boxes.child(r, 1));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, SIZE, base.emPx);
  TEST_ASSERT_TRUE(sup.emPx < base.emPx);   // scaled down
  TEST_ASSERT_TRUE(sup.shift > 0.0f);       // raised
  TEST_ASSERT_TRUE(root.width > base.width);
}

static void test_combined_scripts_use_vlist() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"x^2_i", 5);
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(H x (V 2 i))");
}

static void test_sqrt_structure() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\sqrt{x}", slen(U"\\sqrt{x}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(H <U+221A> x)");
}

static void test_nested_frac_structure() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\frac{x^2}{y}", slen(U"\\frac{x^2}{y}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(boxes, r, "(V (H x 2) R y)");
}

static void test_layout_exhaustion_refuses() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 2, 4);
  Handle r = layoutOf(a, boxes, U"\\frac{abc}{def}", slen(U"\\frac{abc}{def}"));
  TEST_ASSERT_FALSE(valid(r));
}

static void test_iterative_deep_nesting_ok() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 512, 512);
  const c32 src[] =
      U"\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{x}}}}}}}}";
  Handle r = layoutOf(a, boxes, src, slen(src));
  TEST_ASSERT_TRUE(valid(r));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_char_metrics_from_glyph_store);
  RUN_TEST(test_row_width_is_sum);
  RUN_TEST(test_frac_structure_and_depth);
  RUN_TEST(test_superscript_structure_and_scale);
  RUN_TEST(test_combined_scripts_use_vlist);
  RUN_TEST(test_sqrt_structure);
  RUN_TEST(test_nested_frac_structure);
  RUN_TEST(test_layout_exhaustion_refuses);
  RUN_TEST(test_iterative_deep_nesting_ok);
  return UNITY_END();
}
