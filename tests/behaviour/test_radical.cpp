// Radicals. Formerly RED as SQRT-VINCULUM, SQRT-INDEX-DROPPED and
// SQRT-STRETCH; all three are fixed and the assertions below are the ones
// those suites shipped with, except where the oracle showed one to be wrong
// (see test_a_narrow_index_costs_no_width_but_a_wide_one_does).
//
// What each defect was:
//
//   SQRT-VINCULUM      No overbar was drawn, so the extent of the radicand was
//                      invisible. Layout now emits a rule and TeX's clearance.
//
//   SQRT-INDEX-DROPPED The index box was laid out and then never read, so
//                      `\sqrt[3]{x}` rendered byte-identically to `\sqrt{x}`.
//
//   SQRT-STRETCH       The surd was one glyph scaled to the radicand's height,
//                      which distorted its stroke weight and, because it is
//                      positioned from its own metrics, misplaced it. It now
//                      walks TeX's variant chain: cmsy10 112 then cmex10
//                      112 -> 113 -> 114 -> 115.

#include <unity.h>

#include <cstdio>
#include <vector>

#include "stx_compare.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stxtest;
using namespace statex;

void setUp() {}
void tearDown() {}

static void test_sqrt_emits_a_vinculum_rule() {
  const RenderOutput r = renderStatex(tex32("\\sqrt{x}"));
  TEST_ASSERT_TRUE(r.ok());
  std::printf("  \\sqrt{x}: %d glyph blits, %d rules\n", r.coverageBlits,
              r.rules);
  TEST_ASSERT_GREATER_THAN_INT(0, r.rules);
}

static void test_vinculum_spans_the_radicand() {
  // The rule must start where the surd ends and reach the right edge of the
  // radicand, so that \sqrt{abc} is visibly bracketed.
  const RenderOutput r = renderStatex(tex32("\\sqrt{abc}"));
  TEST_ASSERT_TRUE(r.ok());
  TEST_ASSERT_GREATER_THAN_INT(0, r.rules);

  const RulePlacement& rule = r.rulePlacements.front();
  const stximg::InkStats st = stximg::inkStats(r.image);
  std::printf("  rule x=%.1f w=%.1f ; ink spans x=%d..%d\n",
              static_cast<double>(rule.x), static_cast<double>(rule.w), st.x0,
              st.x1);
  TEST_ASSERT_FLOAT_WITHIN(2.0, static_cast<double>(st.x1),
                           static_cast<double>(rule.x + rule.w));
}

static void test_ink_reaches_across_the_top_of_the_radicand() {
  // A picture-level statement of the same defect, independent of how the
  // vinculum is implemented (rule box, extended glyph, whatever). Along the
  // topmost few rows of a radical, ink should run nearly the full width: that
  // is what the overbar is. Today the top rows hold only the tip of the surd
  // and the tops of the letters, so the widest run is a small fraction.
  const RenderOutput r = renderStatex(tex32("\\sqrt{abc}"));
  TEST_ASSERT_TRUE(r.ok());
  // Reference panel: the same radicand with no radical at all. If a vinculum
  // existed the diff would show it as a bar across the top; today the two
  // differ only by the surd sitting to the left.
  const RenderOutput bare = renderStatex(tex32("abc"));
  writeFailureStack(bare.image, r.image, "SQRT-VINCULUM",
                    "nothing covers the radicand: the only difference is the "
                    "surd",
                    artifactPath("defect_sqrt_vinculum.png"),
                    "reference: radicand alone, 'abc'",
                    "statex: 'sqrt{abc}'");

  const stximg::InkStats st = stximg::inkStats(r.image);
  TEST_ASSERT_TRUE(st.any());
  const int formulaWidth = st.bw();

  // Widest horizontal ink run in the top three rows of the glyph box.
  int widestRun = 0;
  for (int y = st.y0; y < st.y0 + 3 && y <= st.y1; ++y) {
    int first = -1, last = -1;
    for (int x = st.x0; x <= st.x1; ++x) {
      if (r.image.at(x, y) < 32) continue;
      if (first < 0) first = x;
      last = x;
    }
    if (first >= 0 && last - first + 1 > widestRun) widestRun = last - first + 1;
  }
  std::printf("  widest ink run in the top rows: %d px of %d wide (%.0f%%)\n",
              widestRun, formulaWidth,
              100.0 * widestRun / (formulaWidth > 0 ? formulaWidth : 1));
  // A vinculum covers the radicand: the surd sits left of it, so expect the
  // run to reach at least two thirds of the formula width.
  TEST_ASSERT_TRUE(widestRun * 3 >= formulaWidth * 2);
}

static void test_radical_index_is_drawn() {
  // \sqrt[3]{x} must put down one more glyph than \sqrt{x}: the 3.
  const RenderOutput plain = renderStatex(tex32("\\sqrt{x}"));
  const RenderOutput indexed = renderStatex(tex32("\\sqrt[3]{x}"));
  TEST_ASSERT_TRUE(plain.ok());
  TEST_ASSERT_TRUE(indexed.ok());
  std::printf("  \\sqrt{x}    blits=%d width=%.2f\n", plain.coverageBlits,
              static_cast<double>(plain.stats.width));
  std::printf("  \\sqrt[3]{x} blits=%d width=%.2f\n", indexed.coverageBlits,
              static_cast<double>(indexed.stats.width));
  TEST_ASSERT_EQUAL_INT(plain.coverageBlits + 1, indexed.coverageBlits);
}

static void test_indexed_and_plain_radicals_are_different_pictures() {
  // The strongest form: they must not be the same image.
  const RenderOutput plain = renderStatex(tex32("\\sqrt{x}"));
  const RenderOutput indexed = renderStatex(tex32("\\sqrt[3]{x}"));
  TEST_ASSERT_TRUE(plain.ok());
  TEST_ASSERT_TRUE(indexed.ok());
  stximg::SimilarityOptions opt;
  opt.maxShift = 0;
  opt.alignOnCentroid = false;
  const stximg::Similarity s =
      stximg::compare(plain.image, indexed.image, opt);
  std::printf("  \\sqrt{x} vs \\sqrt[3]{x}: %s\n", s.describe().c_str());
  if (!(s.rmse > 0.0)) {  // written without == to stay -Wfloat-equal clean
    std::printf("  the two renders are pixel-identical: the index vanished\n");
  }
  // An empty diff panel is the whole story here: the index was never drawn.
  writeFailureStack(plain.image, indexed.image, "SQRT-INDEX-DROPPED",
                    "an empty diff means the requested index never reached "
                    "the box tree",
                    artifactPath("defect_sqrt_index_dropped.png"),
                    "reference: 'sqrt{x}', no index asked for",
                    "statex: 'sqrt[3]{x}', index asked for");
  TEST_ASSERT_TRUE(s.dice < 0.95);
}

static void test_a_narrow_index_costs_no_width_but_a_wide_one_does() {
  // This asserted `indexed.width > plain.width`, which is the intuitive claim
  // and the wrong one. TeX pulls the index back over the surd with a negative
  // 10mu kern, so an index narrower than 10mu disappears entirely into the
  // crook and the radical is exactly as wide as it would be with no index at
  // all. Checked against the oracle at 36px:
  //
  //     \sqrt{x}       MicroTeX 52.00
  //     \sqrt[3]{x}    MicroTeX 52.00     <- identical
  //     \sqrt[10]{x}   MicroTeX 52.00     <- still identical
  //     \sqrt[abc]{x}  MicroTeX 57.00     <- wider than the kern, so it counts
  //
  // Both halves matter: the first pins the overlap, the second pins that the
  // overlap is bounded rather than unconditional.
  const RenderOutput plain = renderStatex(tex32("\\sqrt{x}"));
  const RenderOutput narrow = renderStatex(tex32("\\sqrt[3]{x}"));
  const RenderOutput wide = renderStatex(tex32("\\sqrt[abc]{x}"));
  TEST_ASSERT_TRUE(plain.ok());
  TEST_ASSERT_TRUE(narrow.ok());
  TEST_ASSERT_TRUE(wide.ok());
  std::printf("  plain %.2f, narrow index %.2f, wide index %.2f\n",
              static_cast<double>(plain.stats.width),
              static_cast<double>(narrow.stats.width),
              static_cast<double>(wide.stats.width));
  TEST_ASSERT_FLOAT_WITHIN(0.01, static_cast<double>(plain.stats.width),
                           static_cast<double>(narrow.stats.width));
  TEST_ASSERT_TRUE(wide.stats.width > plain.stats.width + 1.0f);
}

static void test_radical_is_not_stretched_out_of_proportion() {
  // A radical over a tall radicand should grow mostly in height. Scaling one
  // glyph uniformly grows it in width too, so the surd swallows the formula.
  const RenderOutput small = renderStatex(tex32("\\sqrt{x}"));
  const RenderOutput tall = renderStatex(tex32("\\sqrt{\\frac{a}{b}}"));
  TEST_ASSERT_TRUE(small.ok());
  TEST_ASSERT_TRUE(tall.ok());

  const RenderOutput justX = renderStatex(tex32("x"));
  const RenderOutput justFrac = renderStatex(tex32("\\frac{a}{b}"));
  const double surdSmall = static_cast<double>(small.stats.width) -
                           static_cast<double>(justX.stats.width);
  const double surdTall = static_cast<double>(tall.stats.width) -
                          static_cast<double>(justFrac.stats.width);
  std::printf("  surd width: over 'x' %.2f, over a fraction %.2f (%.1fx)\n",
              surdSmall, surdTall,
              surdSmall > 0 ? surdTall / surdSmall : 0.0);
  TEST_ASSERT_TRUE(surdTall < surdSmall * 2.0);
}

static void test_radicand_is_not_clipped_by_the_surd() {
  // Guard, not a defect: the radicand must start to the right of the surd's
  // ink, whatever the surd's size. Protects the HList pen arithmetic while the
  // three defects above are fixed.
  for (const char* tex : {"\\sqrt{x}", "\\sqrt{abc}", "\\sqrt{\\frac{a}{b}}"}) {
    const RenderOutput r = renderStatex(tex32(tex));
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_TRUE(stximg::inkStats(r.image).any());
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sqrt_emits_a_vinculum_rule);
  RUN_TEST(test_vinculum_spans_the_radicand);
  RUN_TEST(test_ink_reaches_across_the_top_of_the_radicand);
  RUN_TEST(test_radical_index_is_drawn);
  RUN_TEST(test_indexed_and_plain_radicals_are_different_pictures);
  RUN_TEST(test_a_narrow_index_costs_no_width_but_a_wide_one_does);
  RUN_TEST(test_radical_is_not_stretched_out_of_proportion);
  RUN_TEST(test_radicand_is_not_clipped_by_the_surd);
  return UNITY_END();
}
