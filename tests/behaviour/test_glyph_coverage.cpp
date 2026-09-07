// Was RED (defect GLYPH-COVERAGE), now green and guarding the fix.
//
// Specified behaviour (STX-FNT-05, reinforced by STX-LNG-02 / STX-ERR-03):
//   "Math style commands select the face for their argument; an unavailable
//    (face, glyph) pair refuses."
//
// It used to reserve a fallback advance for a glyph missing from the atlas and
// then draw nothing, so render() returned Ok while the character silently
// disappeared. layout now refuses with ParseError::MissingGlyph.
//
// Why it matters more than the other defects: every other failure mode here is
// visible as a wrong picture. This one reports success while displaying
// something other than the formula that was asked for, so a caller has no way
// to know. The atlas genuinely lacks '?', '%', italic digits and most
// blackboard letters, so it is easy to hit.
//
// Kept verbatim from when it was red: these went green because the code
// changed, not because an assertion was relaxed.

#include <unity.h>

#include <cstdio>

#include "statex_glyphstore.h"
#include "stx_compare.h"
#include "stx_corpus.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stxtest;
using namespace statex;

void setUp() {}
void tearDown() {}

static void test_absent_roman_glyph_refuses() {
  // Establish the premise: the atlas really does not have these.
  TEST_ASSERT_NULL(findGlyphRecord(Face::Roman, U'?'));
  TEST_ASSERT_NULL(findGlyphRecord(Face::Roman, U'%'));

  const RenderOutput q = renderStatex(tex32("?"));
  std::printf("  '?'  -> error=%d ink=%ld\n", static_cast<int>(q.error),
              stximg::inkStats(q.image).count);
  TEST_ASSERT_FALSE(q.ok());

  const RenderOutput p = renderStatex(tex32("%"));
  TEST_ASSERT_FALSE(p.ok());
}

static void test_absent_face_glyph_pair_refuses() {
  // The italic face carries letters only; the blackboard face only R C N Z Q.
  TEST_ASSERT_NULL(findGlyphRecord(Face::Italic, U'5'));
  TEST_ASSERT_NULL(findGlyphRecord(Face::Blackboard, U'A'));

  TEST_ASSERT_FALSE(renderStatex(tex32("\\mathit{5}")).ok());
  TEST_ASSERT_FALSE(renderStatex(tex32("\\mathbb{A}")).ok());
}

static void test_one_absent_glyph_refuses_the_whole_formula() {
  // 'a' and 'b' are present, '?' is not. A partially-correct formula is worse
  // than a refusal: it looks deliberate.
  const RenderOutput r = renderStatex(tex32("a?b"));
  std::printf("  'a?b' -> error=%d blits=%d (expected refusal)\n",
              static_cast<int>(r.error), r.coverageBlits);
  // 'aXb' is the same three-glyph shape with a middle glyph that IS in the
  // atlas, so the diff isolates the hole left where '?' should have been.
  const RenderOutput present = renderStatex(tex32("aXb"));
  writeFailureStack(present.image, r.image, "GLYPH-COVERAGE",
                    "the gap is reserved and left blank, and render() still "
                    "returns Ok",
                    artifactPath("defect_glyph_coverage.png"),
                    "reference: 'aXb', every glyph in the atlas",
                    "statex: 'a?b', question mark absent from the atlas");
  TEST_ASSERT_FALSE(r.ok());
}

static void test_ok_render_never_leaves_a_blank_gap() {
  // The general invariant behind the specific cases: if render() says Ok, the
  // panel shows the formula. Nothing that reports success may put down zero
  // ink, and no coverage blit may be entirely empty.
  for (const Case& c : corpus()) {
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (!r.ok()) continue;
    const stximg::InkStats st = stximg::inkStats(r.image);
    if (!st.any() || r.blankBlits > 0) {
      std::printf("  '%s' (%s): Ok but ink=%ld blankBlits=%d\n", c.id, c.tex,
                  st.count, r.blankBlits);
    }
    TEST_ASSERT_TRUE(st.any());
    TEST_ASSERT_EQUAL_INT(0, r.blankBlits);
  }
}

static void test_fallback_advance_is_not_used_as_a_glyph() {
  // A refused formula has no width. `?` used to lay out at a `fallbackAdvance`
  // of 0.5 em and report that width to the caller as though it had rendered.
  // Both fallback constants are now gone from FontParams -- a missing glyph
  // refuses, so there is nothing to reserve space for.
  const RenderOutput r = renderStatex(tex32("?"));
  if (r.ok()) {
    std::printf("  '?' reported width %.2f for a glyph that was never drawn\n",
                static_cast<double>(r.stats.width));
  }
  TEST_ASSERT_FALSE(r.ok());
}

static void test_every_corpus_glyph_absence_case_refuses() {
  // Selected by id, not by the openDefect tag: the tag was cleared when the
  // defect was fixed, and keying off it would have quietly reduced this to
  // checking nothing.
  int checked = 0;
  for (const Case& c : corpus()) {
    if (std::string(c.id).rfind("glyph_absent", 0) != 0) continue;
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (r.ok()) std::printf("  '%s' (%s) still renders\n", c.id, c.tex);
    TEST_ASSERT_FALSE(r.ok());
    checked++;
  }
  TEST_ASSERT_GREATER_THAN_INT(0, checked);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_absent_roman_glyph_refuses);
  RUN_TEST(test_absent_face_glyph_pair_refuses);
  RUN_TEST(test_one_absent_glyph_refuses_the_whole_formula);
  RUN_TEST(test_ok_render_never_leaves_a_blank_gap);
  RUN_TEST(test_fallback_advance_is_not_used_as_a_glyph);
  RUN_TEST(test_every_corpus_glyph_absence_case_refuses);
  return UNITY_END();
}
