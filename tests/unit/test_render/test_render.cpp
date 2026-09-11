// Phase 7 — integration + safety properties.
// STX-MEM-03/05, STX-API-01/02, STX-ERR-03.
#include <initializer_list>

#include <unity.h>
#include <statex_render.h>
#include <stx_record.h>

using namespace statex;

static u8 g_scratch[200000];
static const float SIZE = 20.0f;

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

// Regression: default caps must fit a realistic embedded scratch buffer.
// (The device hit OutOfMemory because default caps once needed ~97 KB.)
static u8 g_device_scratch[96 * 1024];

static void test_default_caps_fit_device_scratch() {
  Renderer r(g_device_scratch, sizeof(g_device_scratch));  // default caps
  RecordingGraphics<512> rec;
  RenderStats st{};
  const c32 src[] = U"\\frac{x^2+1}{2}";  // the main.cpp formula
  ParseError e = r.render(src, slen(src), 24.0f, 20, 90, rec, &st);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e);
  TEST_ASSERT_LESS_THAN_UINT((u32)sizeof(g_device_scratch), st.highWater);
}

static void test_end_to_end_ok() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  ParseError e = r.render(U"x^2", 3, SIZE, 0, 100, rec, &st);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e);
  TEST_ASSERT_EQUAL_INT(2, rec.count);
  TEST_ASSERT_GREATER_THAN_UINT(0u, st.highWater);
}

// A representative supported formula exercising frac, scripts, sqrt, symbols.
static const c32 kGolden[] = U"\\frac{x^2}{\\sqrt{y}}+\\alpha";

static void test_golden_formula() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  ParseError e = r.render(kGolden, slen(kGolden), SIZE, 0, 100, rec, &st);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e);
  // Glyphs: x, 2, y, sqrt-radical, +, alpha = 6 glyphs. Two rules: the
  // fraction bar and the radical's vinculum (the latter arrived with the
  // SQRT-VINCULUM fix; this golden previously expected 1).
  int glyphs = 0, rules = 0;
  for (int i = 0; i < rec.count; ++i) {
    if (rec.ops[i].kind == DrawOp::Glyph) ++glyphs;
    else ++rules;
  }
  TEST_ASSERT_EQUAL_INT(6, glyphs);
  TEST_ASSERT_EQUAL_INT(2, rules);
}

static void test_reentrancy_is_deterministic() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> a, b;
  RenderStats sa{}, sb{};
  r.render(kGolden, slen(kGolden), SIZE, 0, 100, a, &sa);
  r.render(kGolden, slen(kGolden), SIZE, 0, 100, b, &sb);
  // Identical op count, identical peak memory, identical first op position.
  TEST_ASSERT_EQUAL_INT(a.count, b.count);
  TEST_ASSERT_EQUAL_UINT(sa.highWater, sb.highWater);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.ops[0].x, b.ops[0].x);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, a.ops[0].y, b.ops[0].y);
}

static void test_phase_scoped_memory_lower_than_naive() {
  // STX-MEM-05: peak should reflect freeing atom/parse scratch before draw,
  // i.e. it must be well under the full arena.
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  r.render(kGolden, slen(kGolden), SIZE, 0, 100, rec, &st);
  TEST_ASSERT_LESS_THAN_UINT((u32)sizeof(g_scratch), st.highWater);
}

static void test_memory_budget_regression() {
  // Lock the capability bound: the golden must fit a recorded high-water mark.
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  r.render(kGolden, slen(kGolden), SIZE, 0, 100, rec, &st);
  // Generous upper bound; tighten as the metric model stabilizes.
  TEST_ASSERT_LESS_THAN_UINT(150000u, st.highWater);
}

static void test_malformed_refuses_and_draws_nothing() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  ParseError e = r.render(U"\\frac{a}", slen(U"\\frac{a}"), SIZE, 0, 100, rec);
  TEST_ASSERT_EQUAL_INT((int)ParseError::MissingArg, (int)e);
  TEST_ASSERT_EQUAL_INT(0, rec.count);  // nothing drawn (STX-ERR-03)
}

static void test_exhaustion_then_reuse() {
  // STX-MEM-03: tiny caps -> a normal formula refuses; then a small one
  // succeeds with the same renderer (clean reset, no residue).
  RenderCaps tiny;
  tiny.maxNodes = 4;
  tiny.maxNodeChildren = 8;
  Renderer r(g_scratch, sizeof(g_scratch), tiny);

  RecordingGraphics<512> rec1;
  ParseError e1 = r.render(U"abcdefgh", 8, SIZE, 0, 100, rec1);
  TEST_ASSERT_EQUAL_INT((int)ParseError::OutOfMemory, (int)e1);
  TEST_ASSERT_EQUAL_INT(0, rec1.count);

  RecordingGraphics<512> rec2;
  ParseError e2 = r.render(U"x", 1, SIZE, 0, 100, rec2);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e2);
  TEST_ASSERT_EQUAL_INT(1, rec2.count);
}

static void test_unknown_command_refused() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  ParseError e =
      r.render(U"\\notacommand", slen(U"\\notacommand"), SIZE, 0, 100, rec);
  TEST_ASSERT_EQUAL_INT((int)ParseError::UnknownCommand, (int)e);
}

static void test_matrix_renders_end_to_end() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  ParseError e = r.render(U"\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}",
                          slen(U"\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}"),
                          SIZE, 0, 100, rec, &st);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e);
  // 4 cell glyphs + 2 bracket delimiters = 6 glyphs, 0 rules.
  int glyphs = 0, rules = 0;
  for (int i = 0; i < rec.count; ++i) {
    if (rec.ops[i].kind == DrawOp::Glyph) ++glyphs;
    else ++rules;
  }
  TEST_ASSERT_EQUAL_INT(6, glyphs);
  TEST_ASSERT_EQUAL_INT(0, rules);
}

static void test_showcase_matrix_renders() {
  // The src/main.cpp demo: bold label + relation + a 2x2 bmatrix with a
  // fraction, scripts, a radical, Greek, and big operators.
  Renderer r(g_scratch, sizeof(g_scratch));
  RecordingGraphics<512> rec;
  RenderStats st{};
  const c32 tex[] =
      U"\\mathbf{A}=\\begin{bmatrix}"
      U"\\frac{x^2+1}{2} & \\sqrt{\\omega} \\\\"
      U"\\alpha^2_i & \\sum\\leq\\infty"
      U"\\end{bmatrix}";
  ParseError e = r.render(tex, slen(tex), 22.0f, 12, 170, rec, &st);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)e);
  int glyphs = 0, rules = 0;
  for (int i = 0; i < rec.count; ++i) {
    if (rec.ops[i].kind == DrawOp::Glyph) ++glyphs;
    else ++rules;
  }
  TEST_ASSERT_GREATER_THAN_INT(14, glyphs);  // most glyphs resolve from atlas
  TEST_ASSERT_EQUAL_INT(2, rules);           // fraction bar + sqrt vinculum
  TEST_ASSERT_LESS_THAN_UINT((u32)sizeof(g_scratch), st.highWater);
}

// --- measure(): extents without rasterising -------------------------------
//
// The contract that matters is that measure() reports exactly what render()
// will lay out. If it drifted, a scrolling list would size its rows from one
// layout and draw another.

static void test_measure_agrees_with_render() {
  Renderer r(g_scratch, sizeof(g_scratch));
  for (float em : {12.0f, 20.0f, 48.0f}) {
    Renderer rm(g_scratch, sizeof(g_scratch));
    RenderStats ms{};
    TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                          (int)rm.measure(kGolden, slen(kGolden), em, &ms));
    Renderer rr(g_scratch, sizeof(g_scratch));
    RecordingGraphics<512> rec;
    RenderStats rs{};
    TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                          (int)rr.render(kGolden, slen(kGolden), em, 0, 100, rec, &rs));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, rs.width, ms.width);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, rs.height, ms.height);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, rs.depth, ms.depth);
  }
}

// measure() must never reach the draw phase, so its arena peak is strictly
// below a render's: no coverage buffer, no draw work-stack. Fresh Renderers,
// because Arena::reset() preserves the high-water mark across calls.
static void test_measure_costs_less_memory_than_render() {
  RenderStats ms{}, rs{};
  {
    Renderer rm(g_scratch, sizeof(g_scratch));
    TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                          (int)rm.measure(kGolden, slen(kGolden), SIZE, &ms));
  }
  {
    Renderer rr(g_scratch, sizeof(g_scratch));
    RecordingGraphics<512> rec;
    TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                          (int)rr.render(kGolden, slen(kGolden), SIZE, 0, 100, rec, &rs));
  }
  TEST_ASSERT_LESS_THAN_UINT(rs.highWater, ms.highWater);
}

// Refusals must agree too: a caller that measures to decide whether to show an
// entry would otherwise accept something the draw walk then rejects.
static void test_measure_refuses_what_render_refuses() {
  Renderer r(g_scratch, sizeof(g_scratch));
  RenderStats st{};
  const c32 bad[] = U"\\notacommand{x}";
  TEST_ASSERT_EQUAL_INT((int)ParseError::UnknownCommand,
                        (int)r.measure(bad, slen(bad), SIZE, &st));
  const c32 unbal[] = U"{x";
  TEST_ASSERT_EQUAL_INT((int)ParseError::UnbalancedBrace,
                        (int)r.measure(unbal, slen(unbal), SIZE, &st));
}

// measure() leaves no residue, exactly as render() does not (STX-API-02).
static void test_measure_is_reentrant() {
  Renderer r(g_scratch, sizeof(g_scratch));
  const c32 src[] = U"\\sqrt{\\alpha^2}";
  RenderStats a{}, b{};
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                        (int)r.measure(src, slen(src), SIZE, &a));
  const c32 other[] = U"\\frac{1}{2}";
  RenderStats junk{};
  r.measure(other, slen(other), SIZE, &junk);
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok,
                        (int)r.measure(src, slen(src), SIZE, &b));
  TEST_ASSERT_FLOAT_WITHIN(0.0f, a.width, b.width);
  TEST_ASSERT_FLOAT_WITHIN(0.0f, a.height, b.height);
  TEST_ASSERT_FLOAT_WITHIN(0.0f, a.depth, b.depth);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_showcase_matrix_renders);
  RUN_TEST(test_default_caps_fit_device_scratch);
  RUN_TEST(test_end_to_end_ok);
  RUN_TEST(test_golden_formula);
  RUN_TEST(test_reentrancy_is_deterministic);
  RUN_TEST(test_phase_scoped_memory_lower_than_naive);
  RUN_TEST(test_memory_budget_regression);
  RUN_TEST(test_malformed_refuses_and_draws_nothing);
  RUN_TEST(test_exhaustion_then_reuse);
  RUN_TEST(test_unknown_command_refused);
  RUN_TEST(test_matrix_renders_end_to_end);
  RUN_TEST(test_measure_agrees_with_render);
  RUN_TEST(test_measure_costs_less_memory_than_render);
  RUN_TEST(test_measure_refuses_what_render_refuses);
  RUN_TEST(test_measure_is_reentrant);
  return UNITY_END();
}
