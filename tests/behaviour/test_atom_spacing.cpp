// Inter-atom spacing. Formerly RED as defect ATOM-SPACING; the assertions
// below are the ones that suite shipped with, unchanged in value.
//
// The parser has always classified every character into a TeX spacing class
// (lib/StaTeX/statex_parser.cpp `classify`) and stored it on the node
// (Node::atomType), but layout never read it, so `a+b` was set as tightly as
// `ab`. Layout's Row arm now consults it (lib/StaTeX/statex_spacing.h).
//
// TeX's inter-atom spacing table (The TeXbook, Chapter 18) in text/display
// style:
//     Ord-Bin, Bin-Ord   medium space   4mu = 2/9 em
//     Ord-Rel, Rel-Ord   thick space    5mu = 5/18 em
//     Ord-Punct          (none)
//     Punct-Ord          thin space     3mu = 1/6 em

#include <unity.h>

#include <cmath>
#include <cstdio>

#include "statex_glyphstore.h"
#include "stx_corpus.h"
#include "stx_render.h"

using namespace stxtest;
using namespace statex;

void setUp() {}
void tearDown() {}

namespace {

constexpr float kSizePx = 22.0f;
constexpr double kThinMu = 3.0 / 18.0;    // 1/6 em
constexpr double kMediumMu = 4.0 / 18.0;  // 2/9 em
constexpr double kThickMu = 5.0 / 18.0;   // 5/18 em

// Sum of the bare glyph advances, i.e. the width with no inter-atom glue.
// Delegates to the shared resolver so this file cannot disagree with the
// parser about which glyph a character is -- it has done so twice already
// (see the note on mathGlyphRecord in stx_render.h).
double advanceSum(const char* ascii) { return bareAdvanceSum(ascii, kSizePx); }

double renderedWidth(const char* tex) {
  const RenderOutput r = renderStatex(tex32(tex));
  return r.ok() ? static_cast<double>(r.stats.width) : -1.0;
}

}  // namespace

static void test_ordinary_pair_has_no_extra_space() {
  // The control case: two ordinaries get no glue, and this already holds.
  // If it ever breaks, the other assertions in this file mean nothing.
  const double got = renderedWidth("ab");
  const double bare = advanceSum("ab");
  std::printf("  'ab'  width %.2f, bare advances %.2f\n", got, bare);
  TEST_ASSERT_FLOAT_WITHIN(0.5, bare, got);
}

static void test_binary_operator_gets_a_medium_space_either_side() {
  const double got = renderedWidth("a+b");
  const double bare = advanceSum("a+b");
  const double want = bare + 2.0 * kMediumMu * kSizePx;
  std::printf("  'a+b' width %.2f, bare %.2f, TeX would be %.2f (+%.2f)\n", got,
              bare, want, want - bare);
  TEST_ASSERT_FLOAT_WITHIN(1.0, want, got);
}

static void test_relation_gets_a_thick_space_either_side() {
  const double got = renderedWidth("a=b");
  const double bare = advanceSum("a=b");
  const double want = bare + 2.0 * kThickMu * kSizePx;
  std::printf("  'a=b' width %.2f, bare %.2f, TeX would be %.2f (+%.2f)\n", got,
              bare, want, want - bare);
  TEST_ASSERT_FLOAT_WITHIN(1.0, want, got);
}

static void test_punctuation_gets_a_thin_space_after_only() {
  const double got = renderedWidth("a,b");
  const double bare = advanceSum("a,b");
  const double want = bare + kThinMu * kSizePx;
  std::printf("  'a,b' width %.2f, bare %.2f, TeX would be %.2f (+%.2f)\n", got,
              bare, want, want - bare);
  TEST_ASSERT_FLOAT_WITHIN(1.0, want, got);
}

static void test_binary_and_relation_compose() {
  const double got = renderedWidth("a+b=c");
  const double bare = advanceSum("a+b=c");
  const double want =
      bare + 2.0 * kMediumMu * kSizePx + 2.0 * kThickMu * kSizePx;
  std::printf("  'a+b=c' width %.2f, bare %.2f, TeX would be %.2f\n", got, bare,
              want);
  TEST_ASSERT_FLOAT_WITHIN(1.5, want, got);
}

static void test_relation_is_spaced_more_generously_than_a_binary() {
  // A weaker, ordering-only form of the same claim: even without exact muskip
  // values, a relation must be looser than a binary operator, which must be
  // looser than an ordinary pair.
  const double ord = renderedWidth("ab") - advanceSum("ab");
  const double bin = renderedWidth("a+b") - advanceSum("a+b");
  const double rel = renderedWidth("a=b") - advanceSum("a=b");
  std::printf("  extra glue: ord=%.2f bin=%.2f rel=%.2f\n", ord, bin, rel);
  TEST_ASSERT_TRUE(bin > ord + 0.5);
  TEST_ASSERT_TRUE(rel > bin + 0.2);
}

static void test_named_symbols_carry_their_spacing_class_too() {
  // \cdot is AtomType::BinaryOp and \leq is AtomType::Relation in
  // lib/StaTeX/statex_symbols.cpp, so they must be spaced like '+' and '='.
  const RenderOutput dot = renderStatex(tex32("a\\cdot b"));
  const RenderOutput leq = renderStatex(tex32("a\\leq b"));
  TEST_ASSERT_TRUE(dot.ok());
  TEST_ASSERT_TRUE(leq.ok());

  const GlyphRecord* gd = findGlyphRecord(Face::Symbol, 0x22C5);
  const GlyphRecord* gl = findGlyphRecord(Face::Symbol, 0x2264);
  TEST_ASSERT_NOT_NULL(gd);
  TEST_ASSERT_NOT_NULL(gl);

  const double bareDot = advanceSum("ab") + emUnits(gd->advance, kSizePx);
  const double bareLeq = advanceSum("ab") + emUnits(gl->advance, kSizePx);
  const double wantDot = bareDot + 2.0 * kMediumMu * kSizePx;
  const double wantLeq = bareLeq + 2.0 * kThickMu * kSizePx;
  std::printf("  '\\cdot' width %.2f want %.2f;  '\\leq' width %.2f want %.2f\n",
              static_cast<double>(dot.stats.width), wantDot,
              static_cast<double>(leq.stats.width), wantLeq);
  TEST_ASSERT_FLOAT_WITHIN(1.0, wantDot, static_cast<double>(dot.stats.width));
  TEST_ASSERT_FLOAT_WITHIN(1.0, wantLeq, static_cast<double>(leq.stats.width));
}

static void test_atom_type_survives_into_the_box_tree() {
  // The structural half of the defect: whatever mechanism carries spacing must
  // make a Row of three atoms wider than the sum of its three boxes.
  const RenderOutput r = renderStatex(tex32("a+b"));
  TEST_ASSERT_TRUE(r.ok());
  const double bare = advanceSum("a+b");
  std::printf("  row width %.2f vs sum of child advances %.2f\n",
              static_cast<double>(r.stats.width), bare);
  TEST_ASSERT_TRUE(static_cast<double>(r.stats.width) > bare + 0.5);
}

// --- BIN -> ORD demotion (TeXbook p.170) -----------------------------------
//
// A binary operator is only binary when it has an operand on both sides.
// Everywhere else TeX reclassifies it as Ordinary, which removes the medium
// space -- this is what makes the minus in `-x` a sign rather than a
// subtraction. Getting this wrong is not subtle: without it, every unary sign
// in the corpus is set 2*4mu too wide.

static void test_leading_operator_is_unary_and_takes_no_space() {
  const double got = renderedWidth("-x");
  const double bare = advanceSum("-x");
  std::printf("  '-x'  width %.2f, bare %.2f (must be equal: unary)\n", got,
              bare);
  TEST_ASSERT_FLOAT_WITHIN(0.5, bare, got);
}

static void test_trailing_operator_is_demoted_too() {
  // Nothing follows the `+`, so it cannot be binary.
  const double got = renderedWidth("x+");
  const double bare = advanceSum("x+");
  std::printf("  'x+'  width %.2f, bare %.2f (must be equal: nothing to add)\n",
              got, bare);
  TEST_ASSERT_FLOAT_WITHIN(0.5, bare, got);
}

static void test_operator_after_a_relation_is_a_sign_not_an_operator() {
  // `x=-y`: the `-` follows a relation, so it is a sign. Only the relation
  // contributes glue -- two thick spaces, and nothing around the minus.
  const double got = renderedWidth("x=-y");
  const double bare = advanceSum("x=-y");
  const double want = bare + 2.0 * kThickMu * kSizePx;
  std::printf("  'x=-y' width %.2f, bare %.2f, want %.2f (thick only)\n", got,
              bare, want);
  TEST_ASSERT_FLOAT_WITHIN(1.0, want, got);
}

static void test_a_demoted_operator_does_not_demote_its_successor() {
  // `-x+y`: the leading `-` is demoted to Ordinary, and *because* it is now
  // Ordinary the following `+` keeps an operand on its left and stays binary.
  // If demotion were applied against the original types instead of the
  // running ones, the `+` would be demoted too and this would come out 2*4mu
  // narrow.
  const double got = renderedWidth("-x+y");
  const double bare = advanceSum("-x+y");
  const double want = bare + 2.0 * kMediumMu * kSizePx;
  std::printf("  '-x+y' width %.2f, bare %.2f, want %.2f (medium only)\n", got,
              bare, want);
  TEST_ASSERT_FLOAT_WITHIN(1.0, want, got);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ordinary_pair_has_no_extra_space);
  RUN_TEST(test_binary_operator_gets_a_medium_space_either_side);
  RUN_TEST(test_relation_gets_a_thick_space_either_side);
  RUN_TEST(test_punctuation_gets_a_thin_space_after_only);
  RUN_TEST(test_binary_and_relation_compose);
  RUN_TEST(test_relation_is_spaced_more_generously_than_a_binary);
  RUN_TEST(test_named_symbols_carry_their_spacing_class_too);
  RUN_TEST(test_atom_type_survives_into_the_box_tree);
  RUN_TEST(test_leading_operator_is_unary_and_takes_no_space);
  RUN_TEST(test_trailing_operator_is_demoted_too);
  RUN_TEST(test_operator_after_a_relation_is_a_sign_not_an_operator);
  RUN_TEST(test_a_demoted_operator_does_not_demote_its_successor);
  return UNITY_END();
}
