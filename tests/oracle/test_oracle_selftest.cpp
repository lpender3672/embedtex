// Green -- the oracle adapter's own sanity checks.
//
// The differential suite is only worth reading if the oracle it compares
// against is actually working. This suite proves MicroTeX comes up, that the
// real TeX faces rasterise, and that the adapter is deterministic. It asserts
// nothing about StaTeX's layout.

#include <unity.h>

#include <cstdio>
#include <string>

#include "microtex_oracle.h"
#include "stx_compare.h"
#include "stx_corpus.h"
#include "stx_render.h"

using namespace stxoracle;
using namespace stxtest;

void setUp() {}
void tearDown() {}

static void test_microtex_and_freetype_come_up() {
  std::string err;
  const bool ready = ensureReady(&err);
  if (!ready) std::printf("  oracle unavailable: %s\n", err.c_str());
  TEST_ASSERT_TRUE(ready);
}

static void test_a_simple_formula_typesets() {
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  const OracleRender r = render("x+1", Canvas{});
  if (!r.ok) std::printf("  oracle error: %s\n", r.error.c_str());
  TEST_ASSERT_TRUE(r.ok);
  std::printf("  'x+1' -> %d glyphs, %zu rules, %dx%d+%d\n", r.glyphCount(),
              r.rules.size(), static_cast<int>(r.width),
              static_cast<int>(r.height), static_cast<int>(r.depth));
  TEST_ASSERT_EQUAL_INT(3, r.glyphCount());
}

static void test_a_fraction_produces_a_rule() {
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  const OracleRender r = render("\\frac{a}{b}", Canvas{});
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_INT(2, r.glyphCount());
  TEST_ASSERT_GREATER_THAN_INT(0, static_cast<int>(r.rules.size()));
}

static void test_every_glyph_the_corpus_needs_rasterises() {
  // The whole point of drawing the reference from the real faces is that
  // nothing has to be approximated. If any slot fails to render, the reference
  // has a hole and every score computed against it is suspect.
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  const Canvas canvas;
  const char* formulas[] = {
      "abc",
      "x^2_i",
      "\\frac{x^2+1}{2}",
      "\\sqrt{x}",
      "\\sqrt{\\frac{a}{b}}",
      "\\sqrt[3]{x}",
      "\\alpha\\leq\\infty",
      "\\sum\\int",
      "\\mathbf{A}\\mathit{x}\\mathbb{R}",
      "\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}",
      "\\mathbf{A}=\\begin{bmatrix}\\frac{x^2+1}{2} & \\sqrt{\\omega} \\\\"
      "\\alpha^2_i & \\sum\\leq\\infty\\end{bmatrix}",
  };
  int total = 0;
  for (const char* f : formulas) {
    const OracleRender r = render(f, canvas);
    TEST_ASSERT_TRUE(r.ok);
    int unrendered = 0;
    const stximg::Image img = rasterise(r, canvas, &unrendered);
    if (unrendered > 0) {
      std::printf("  '%s': %d of %d glyphs would not render\n", f, unrendered,
                  r.glyphCount());
    }
    TEST_ASSERT_EQUAL_INT(0, unrendered);
    TEST_ASSERT_TRUE(stximg::inkStats(img).any());
    total += r.glyphCount();
  }
  std::printf("  %d glyphs across %zu formulas, all rendered from %d TeX faces\n",
              total, sizeof(formulas) / sizeof(formulas[0]), openFaceCount());
}

static void test_no_corpus_case_hits_an_unrecorded_path() {
  // microtex_host counts calls to MicroTeX's text-layout path, which the
  // recorder cannot capture as glyphs. A non-zero count means the reference we
  // are scoring against is INCOMPLETE -- glyphs were drawn that never reached
  // the recording -- and every metric derived from it is quietly wrong rather
  // than visibly broken.
  //
  // The counter has existed since the recorder was written and nothing ever
  // read it. This is the check that makes it mean something.
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  stxtest::Canvas c;
  c.sizePx = 36.0f;
  c.width = 540;
  c.height = 250;
  c.originX = 18.0f;
  c.baseline = 124.0f;

  int offenders = 0;
  for (const stxtest::Case& k : stxtest::corpus()) {
    if (k.expect != stxtest::Expect::Renders) continue;
    const OracleRender o = render(k.tex, c);
    if (!o.ok) continue;
    if (o.textRuns != 0) {
      std::printf("  %-22s took the text-layout path %d time(s): the "
                  "reference is incomplete\n",
                  k.id, o.textRuns);
      offenders++;
    }
  }
  std::printf("  %d case(s) produce an incomplete reference\n", offenders);
  TEST_ASSERT_EQUAL_INT(0, offenders);
}

static void test_size_variants_really_are_bigger() {
  // A tall radicand makes TeX switch to a larger radical, and past the largest
  // variant it builds the delimiter from pieces. Both must come out visibly
  // bigger than the base glyph -- that is exactly what the old atlas-based
  // reference could not do.
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  Canvas canvas;
  canvas.width = 900;
  canvas.height = 700;
  canvas.baseline = 400.0f;
  canvas.sizePx = 36.0f;

  const char* growing[] = {"\\sqrt{x}", "\\sqrt{\\frac{a}{b}}",
                           "\\sqrt{\\frac{\\frac{a}{b}}{\\frac{c}{d}}}"};
  int lastHeight = 0;
  for (const char* f : growing) {
    const OracleRender r = render(f, canvas);
    TEST_ASSERT_TRUE(r.ok);
    int unrendered = 0;
    const stximg::Image img = rasterise(r, canvas, &unrendered);
    TEST_ASSERT_EQUAL_INT(0, unrendered);
    const stximg::InkStats st = stximg::inkStats(img);
    std::printf("  %-40s ink %dx%d\n", f, st.bw(), st.bh());
    TEST_ASSERT_GREATER_THAN_INT(lastHeight, st.bh());
    lastHeight = st.bh();
  }
}

static void test_oracle_is_deterministic() {
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  const Canvas canvas;
  const OracleRender a = render("\\frac{x^2+1}{2}", canvas);
  const OracleRender b = render("\\frac{x^2+1}{2}", canvas);
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_TRUE(b.ok);
  TEST_ASSERT_EQUAL_INT(a.glyphCount(), b.glyphCount());
  for (size_t i = 0; i < a.glyphs.size(); ++i) {
    TEST_ASSERT_FLOAT_WITHIN(1e-4, a.glyphs[i].x, b.glyphs[i].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, a.glyphs[i].baselineY, b.glyphs[i].baselineY);
  }
  int ua = 0, ub = 0;
  const stximg::Similarity s = stximg::compare(rasterise(a, canvas, &ua),
                                               rasterise(b, canvas, &ub));
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
}

static void test_reference_picture_is_written() {
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  Canvas canvas;
  canvas.width = 700;
  canvas.height = 400;
  canvas.baseline = 250.0f;
  canvas.sizePx = 36.0f;
  const OracleRender r = render("\\sqrt{\\frac{a}{b}}", canvas);
  TEST_ASSERT_TRUE(r.ok);
  int unrendered = 0;
  const stximg::Image img = rasterise(r, canvas, &unrendered);
  TEST_ASSERT_EQUAL_INT(0, unrendered);
  TEST_ASSERT_TRUE(stximg::writePgm(stximg::cropToInk(img, 4),
                                    artifactPath("oracle_reference.pgm")));
}

static void test_malformed_input_does_not_crash_the_oracle() {
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  // MicroTeX throws on these; the adapter must contain that and report.
  for (const char* bad : {"\\nosuchcommand", "{a", "\\frac{a}", "^2"}) {
    const OracleRender r = render(bad, Canvas{});
    std::printf("  '%s' -> ok=%d %s\n", bad, r.ok ? 1 : 0, r.error.c_str());
  }
  TEST_ASSERT_TRUE(render("x", Canvas{}).ok);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_microtex_and_freetype_come_up);
  RUN_TEST(test_a_simple_formula_typesets);
  RUN_TEST(test_a_fraction_produces_a_rule);
  RUN_TEST(test_every_glyph_the_corpus_needs_rasterises);
  RUN_TEST(test_no_corpus_case_hits_an_unrecorded_path);
  RUN_TEST(test_size_variants_really_are_bigger);
  RUN_TEST(test_oracle_is_deterministic);
  RUN_TEST(test_reference_picture_is_written);
  RUN_TEST(test_malformed_input_does_not_crash_the_oracle);
  return UNITY_END();
}
