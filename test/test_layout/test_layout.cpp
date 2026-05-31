// Phase 5 — layout (atom tree -> box tree), metrics + structure.
// STX-EXE-01 (iterative), STX-DAT-03/04, STX-MEM-03.
#include <unity.h>
#include <statex_parser.h>
#include <statex_layout.h>
#include <statex_serialize.h>

using namespace statex;

static u8 g_buf[65536];
static const float SIZE = 20.0f;

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

// Parse + layout into the given BoxStore; returns root box handle (NO_NODE on fail).
static Handle layoutOf(Arena& a, BoxStore& boxes, const c32* src, int len) {
  NodeStore nodes(a, 1024, 1024);
  Parser p(a, nodes);
  ParseResult pr = p.parse(src, len);
  if (pr.error != ParseError::Ok) return NO_NODE;
  Layout lay(a, nodes, boxes, SIZE);
  LayoutResult lr = lay.run(pr.root);
  return lr.ok ? lr.rootBox : NO_NODE;
}

static void structEq(Arena& a, BoxStore& boxes, Handle root, const char* expect) {
  char out[512];
  int n = serializeBox(boxes, root, out, sizeof(out));
  TEST_ASSERT_TRUE(n >= 0);
  TEST_ASSERT_EQUAL_STRING(expect, out);
}

static void test_char_metrics() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"x", 1);
  TEST_ASSERT_TRUE(valid(r));
  const Box& b = boxes.get(r);
  TEST_ASSERT_EQUAL_INT((int)BoxKind::Char, (int)b.kind);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, b.width);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, b.height);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.depth);
}

static void test_row_metrics() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"xy", 2);
  TEST_ASSERT_TRUE(valid(r));
  structEq(a, boxes, r, "(H x y)");
  const Box& b = boxes.get(r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, b.width);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, b.height);
}

static void test_frac_metrics() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\frac{a}{b}", slen(U"\\frac{a}{b}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(a, boxes, r, "(V a R b)");
  const Box& b = boxes.get(r);
  TEST_ASSERT_EQUAL_INT((int)BoxKind::VList, (int)b.kind);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, b.width);
  // numShift(8.4) + num.height(14) = 22.4 ; depth = 12.4
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.4f, b.height);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.4f, b.depth);
}

static void test_superscript_metrics() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"x^2", 3);
  TEST_ASSERT_TRUE(valid(r));
  structEq(a, boxes, r, "(H x 2)");
  const Box& b = boxes.get(r);
  // width = base(10) + sup(10*0.7=7) = 17
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 17.0f, b.width);
  // height = max(14, supShift(9) + supHeight(14*0.7=9.8)) = 18.8
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 18.8f, b.height);
}

static void test_sqrt_structure() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\sqrt{x}", slen(U"\\sqrt{x}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(a, boxes, r, "(H <U+221A> x)");
  const Box& b = boxes.get(r);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 19.0f, b.width);  // 9 + 10
}

static void test_nested_frac_structure() {
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 256, 256);
  Handle r = layoutOf(a, boxes, U"\\frac{x^2}{y}", slen(U"\\frac{x^2}{y}"));
  TEST_ASSERT_TRUE(valid(r));
  structEq(a, boxes, r, "(V (H x 2) R y)");
}

static void test_layout_exhaustion_refuses() {
  // Tiny box store -> layout must fail cleanly, not crash (STX-MEM-03).
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 2, 4);
  Handle r = layoutOf(a, boxes, U"\\frac{abc}{def}", slen(U"\\frac{abc}{def}"));
  TEST_ASSERT_FALSE(valid(r));
}

static void test_iterative_deep_nesting_ok() {
  // Deep nesting must not overflow the C++ stack (work-stack is in the arena).
  Arena a(g_buf, sizeof(g_buf));
  BoxStore boxes(a, 512, 512);
  const c32 src[] =
      U"\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{\\sqrt{x}}}}}}}}";
  Handle r = layoutOf(a, boxes, src, slen(src));
  TEST_ASSERT_TRUE(valid(r));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_char_metrics);
  RUN_TEST(test_row_metrics);
  RUN_TEST(test_frac_metrics);
  RUN_TEST(test_superscript_metrics);
  RUN_TEST(test_sqrt_structure);
  RUN_TEST(test_nested_frac_structure);
  RUN_TEST(test_layout_exhaustion_refuses);
  RUN_TEST(test_iterative_deep_nesting_ok);
  return UNITY_END();
}
