// Phase 6 — draw (box tree -> coverage op-list) via recording backend.
// STX-API-03, STX-FNT-02/03.
#include <unity.h>
#include <statex_parser.h>
#include <statex_layout.h>
#include <statex_draw.h>
#include <statex_record.h>

using namespace statex;

static u8 g_buf[65536];
static const float SIZE = 24.0f;
static const float BL = 100.0f;

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

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

static void test_single_glyph_coverage() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"x", 1));
  TEST_ASSERT_EQUAL_INT(1, rec.count);
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[0].kind);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, rec.ops[0].w);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, rec.ops[0].h);
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, rec.ops[0].fill);  // some ink
  TEST_ASSERT_LESS_THAN_FLOAT(1.0f, rec.ops[0].fill);     // not fully solid
}

static void test_row_advances_left_to_right() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"xy", 2));
  TEST_ASSERT_EQUAL_INT(2, rec.count);
  // Second glyph starts to the right of the first.
  TEST_ASSERT_TRUE(rec.ops[1].x > rec.ops[0].x);
}

static void test_superscript_is_higher_and_right() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"x^2", 3));
  TEST_ASSERT_EQUAL_INT(2, rec.count);
  // The sup '2' is drawn higher up (smaller y) and to the right of base 'x'.
  // (Don't compare coverage heights: '2' is intrinsically taller than 'x'.)
  TEST_ASSERT_TRUE(rec.ops[1].y < rec.ops[0].y);
  TEST_ASSERT_TRUE(rec.ops[1].x > rec.ops[0].x);
}

static void test_fraction_emits_rule_between_glyphs() {
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"\\frac{a}{b}", slen(U"\\frac{a}{b}")));
  TEST_ASSERT_EQUAL_INT(3, rec.count);
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[0].kind);  // numerator
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Rule, (int)rec.ops[1].kind);   // bar
  TEST_ASSERT_EQUAL_INT((int)DrawOp::Glyph, (int)rec.ops[2].kind);  // denominator
  // Numerator above the bar, denominator below.
  TEST_ASSERT_TRUE(rec.ops[0].y < rec.ops[1].y);
  TEST_ASSERT_TRUE(rec.ops[2].y > rec.ops[1].y);
}

static void test_missing_glyph_draws_nothing() {
  // A CJK char has no atlas glyph: it occupies space but emits no coverage.
  Arena a(g_buf, sizeof(g_buf));
  RecordingGraphics<512> rec;
  TEST_ASSERT_TRUE(render(a, rec, U"x中x", 3));
  TEST_ASSERT_EQUAL_INT(2, rec.count);  // only the two 'x'
}

static void test_draw_workstack_exhaustion() {
  Arena big(g_buf, sizeof(g_buf));
  NodeStore nodes(big, 1024, 1024);
  Parser p(big, nodes);
  ParseResult pr = p.parse(U"\\frac{a}{b}", slen(U"\\frac{a}{b}"));
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)pr.error);
  BoxStore boxes(big, 512, 512);
  Layout lay(big, nodes, boxes, SIZE);
  LayoutResult lr = lay.run(pr.root);
  TEST_ASSERT_TRUE(lr.ok);

  u8 tiny[8];
  Arena small(tiny, sizeof(tiny));
  RecordingGraphics<512> rec;
  TEST_ASSERT_FALSE(drawTree(small, boxes, lr.rootBox, 0.0f, BL, rec));
  TEST_ASSERT_EQUAL_INT(0, rec.count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_glyph_coverage);
  RUN_TEST(test_row_advances_left_to_right);
  RUN_TEST(test_superscript_is_higher_and_right);
  RUN_TEST(test_fraction_emits_rule_between_glyphs);
  RUN_TEST(test_missing_glyph_draws_nothing);
  RUN_TEST(test_draw_workstack_exhaustion);
  return UNITY_END();
}
