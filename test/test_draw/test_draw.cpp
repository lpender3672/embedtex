// Phase 6 — draw (box tree -> op-list) via recording backend. STX-API-03.
#include <unity.h>
#include <statex_parser.h>
#include <statex_layout.h>
#include <statex_draw.h>
#include <statex_record.h>

using namespace statex;

static u8 g_buf[65536];
static const float SIZE = 20.0f;
static const float BL = 100.0f;

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

// Parse + layout + draw `src`; fill `rec`. Returns true on full success.
template <int Cap>
static bool render(Arena& a, RecordingGraphics<Cap>& rec, const c32* src,
                   int len) {
  NodeStore nodes(a, 1024, 1024);
  Parser p(a, nodes);
  ParseResult pr = p.parse(src, len);
  if (pr.error != ParseError::Ok) return false;
  BoxStore boxes(a, 512, 512);
  Layout lay(a, nodes, boxes, SIZE);
  LayoutResult lr = lay.run(pr.root);
  if (!lr.ok) return false;
  return drawTree(a, boxes, lr.rootBox, 0.0f, BL, rec);
}

static void test_single_glyph() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"x", 1));
  TEST_ASSERT_EQUAL_INT(1, rec.count);
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[0].kind);
  TEST_ASSERT_EQUAL_UINT('x', rec.ops[0].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, rec.ops[0].x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, BL, rec.ops[0].y);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, rec.ops[0].scale);
}

static void test_row_advances() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"xy", 2));
  TEST_ASSERT_EQUAL_INT(2, rec.count);
  TEST_ASSERT_EQUAL_UINT('x', rec.ops[0].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, rec.ops[0].x);
  TEST_ASSERT_EQUAL_UINT('y', rec.ops[1].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, rec.ops[1].x);  // advanced by width
}

static void test_superscript_position_and_scale() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"x^2", 3));
  TEST_ASSERT_EQUAL_INT(2, rec.count);
  // base x at baseline, full scale
  TEST_ASSERT_EQUAL_UINT('x', rec.ops[0].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, BL, rec.ops[0].y);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, rec.ops[0].scale);
  // sup '2' raised (smaller y), scaled 0.7, x advanced by base width 10
  TEST_ASSERT_EQUAL_UINT('2', rec.ops[1].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, rec.ops[1].x);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, BL - 9.0f, rec.ops[1].y);  // SUP_SHIFT*20=9
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.7f, rec.ops[1].scale);
}

static void test_fraction_op_list() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"\\frac{a}{b}", slen(U"\\frac{a}{b}")));
  // num glyph, rule, den glyph (pre-order)
  TEST_ASSERT_EQUAL_INT(3, rec.count);
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[0].kind);
  TEST_ASSERT_EQUAL_UINT('a', rec.ops[0].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, BL - 8.4f, rec.ops[0].y);  // numShift

  TEST_ASSERT_EQUAL_INT((int)DrawOp::Rule, (int)rec.ops[1].kind);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, rec.ops[1].w);     // rule width
  TEST_ASSERT_FLOAT_WITHIN(0.05f, BL - 5.0f - 0.4f, rec.ops[1].y);  // axis top

  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[2].kind);
  TEST_ASSERT_EQUAL_UINT('b', rec.ops[2].ch);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, BL + 12.4f, rec.ops[2].y);  // den below baseline
}

static void test_draw_workstack_exhaustion() {
  // Force the draw work-stack allocation to fail with a tiny arena slice.
  Arena big(g_buf, sizeof(g_buf));
  NodeStore nodes(big, 1024, 1024);
  Parser p(big, nodes);
  ParseResult pr = p.parse(U"\\frac{a}{b}", slen(U"\\frac{a}{b}"));
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)pr.error);
  BoxStore boxes(big, 512, 512);
  Layout lay(big, nodes, boxes, SIZE);
  LayoutResult lr = lay.run(pr.root);
  TEST_ASSERT_TRUE(lr.ok);

  // A separate, full arena -> drawTree can't get its work-stack.
  u8 tiny[8];
  Arena small(tiny, sizeof(tiny));
  RecordingGraphics<512> rec;
  TEST_ASSERT_FALSE(drawTree(small, boxes, lr.rootBox, 0.0f, BL, rec));
  TEST_ASSERT_EQUAL_INT(0, rec.count);  // nothing drawn on refusal
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_glyph);
  RUN_TEST(test_row_advances);
  RUN_TEST(test_superscript_position_and_scale);
  RUN_TEST(test_fraction_op_list);
  RUN_TEST(test_draw_workstack_exhaustion);
  return UNITY_END();
}
