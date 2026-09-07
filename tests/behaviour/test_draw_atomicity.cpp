// Was RED (defect DRAW-ATOMICITY.
//
// lib/StaTeX/statex_render.h:40-42 states the contract:
//
//     "Any failure (malformed input, or scratch exhaustion) is a defined
//      refusal that resets cleanly (STX-MEM-03 / STX-ERR-03); on refusal
//      nothing is drawn."
//
// The draw phase cannot honour that. lib/StaTeX/statex_draw.cpp walks the box
// tree emitting primitives as it goes, and bails out mid-walk on two paths:
//   :43  an oversized glyph exceeds the kCovCap coverage budget
//   :81  an unexpected BoxKind
// By then earlier glyphs are already on the panel. The caller sees
// OutOfMemory, the user sees half a formula.
//
// The two honest fixes are (a) validate the maximum glyph extent during layout
// so the draw phase cannot fail, or (b) drop the claim from the header. These
// tests assert (a), which is the one that matches STX-MEM-03's "refuse, don't
// fail" framing.
//
// Related capability note: kCovCap is 160x160, so any single glyph over about
// 160px refuses. The scaled radical and matrix delimiters reach that at quite
// ordinary display sizes, which is why this is easy to trigger.

#include <unity.h>

#include <cstdio>

#include "stx_compare.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stxtest;
using namespace statex;

void setUp() {}
void tearDown() {}

namespace {

// A radicand tall enough that the radical glyph is scaled past the coverage
// budget, preceded by an ordinary glyph that draws successfully first.
const char* kOversizeAfterInk =
    "a\\sqrt{\\frac{\\frac{x}{y}}{\\frac{x}{y}}}";

Canvas bigCanvas(float sizePx) {
  Canvas c;
  c.width = 1400;
  c.height = 900;
  c.originX = 20.0f;
  c.baseline = 600.0f;
  c.sizePx = sizePx;
  return c;
}

// Find the smallest em at which `kOversizeAfterInk` actually refuses.
//
// This used to be a hardcoded 70px, chosen when the radical was drawn by
// scaling one glyph up: at that size the scaled surd blew the coverage budget.
// Radicals now pick a purpose-cut variant at natural size, so 70px renders
// fine and the test's PREMISE evaporated -- it started asserting "this
// refuses" about an input that succeeds, which says nothing about atomicity.
//
// Searching for the size keeps the test about the property (a refusal emits
// nothing) instead of about a number that goes stale whenever glyph sizing
// changes. Returns 0 if nothing in range refuses, which the tests report as a
// lost premise rather than passing vacuously.
float smallestRefusingSize() {
  for (float px = 40.0f; px <= 400.0f; px += 5.0f) {
    if (!renderStatex(tex32(kOversizeAfterInk), bigCanvas(px)).ok()) return px;
  }
  return 0.0f;
}

}  // namespace

static void test_refusal_emits_no_primitives() {
  const float px = smallestRefusingSize();
  std::printf("  smallest refusing em: %.0fpx\n", static_cast<double>(px));
  TEST_ASSERT_TRUE(px > 0.0f);  // premise: some size does refuse
  const Canvas c = bigCanvas(px);
  const RenderOutput r = renderStatex(tex32(kOversizeAfterInk), c);
  std::printf("  error=%d, coverage blits emitted=%d, rules=%d\n",
              static_cast<int>(r.error), r.coverageBlits, r.rules);
  TEST_ASSERT_FALSE(r.ok());
  TEST_ASSERT_EQUAL_INT(0, r.coverageBlits);
  TEST_ASSERT_EQUAL_INT(0, r.rules);
}

static void test_refusal_leaves_the_canvas_blank() {
  const float px = smallestRefusingSize();
  TEST_ASSERT_TRUE(px > 0.0f);
  const Canvas c = bigCanvas(px);
  const RenderOutput r = renderStatex(tex32(kOversizeAfterInk), c);
  TEST_ASSERT_FALSE(r.ok());
  const stximg::InkStats st = stximg::inkStats(r.image);
  if (st.any()) {
    // A refused render must leave the panel exactly as it found it, so the
    // reference is a blank canvas. Everything green in the diff is ink that
    // should never have reached the device.
    const stximg::Image blank(r.image.w, r.image.h);
    writeFailureStack(blank, r.image, "DRAW-ATOMICITY",
                      "render() returned a refusal, but this much had already "
                      "been drawn",
                      artifactPath("defect_draw_atomicity.png"),
                      "reference: nothing drawn, per the contract",
                      "statex: what actually reached the device");
    std::printf("  %ld inked pixels survived a refusal (see artifacts/)\n",
                st.count);
  }
  TEST_ASSERT_FALSE(st.any());
}

static void test_refusal_is_detected_before_drawing_starts() {
  // Sweep the size at which the radical outgrows the coverage budget. At every
  // size the render either fully succeeds or emits nothing at all -- there is
  // no size at which it half-draws.
  for (float sizePx = 40.0f; sizePx <= 130.0f; sizePx += 5.0f) {
    const RenderOutput r = renderStatex(tex32(kOversizeAfterInk),
                                        bigCanvas(sizePx));
    if (r.ok()) continue;
    if (r.coverageBlits != 0) {
      std::printf("  size %.0fpx: refused after %d blits\n",
                  static_cast<double>(sizePx), r.coverageBlits);
    }
    TEST_ASSERT_EQUAL_INT(0, r.coverageBlits);
  }
}

static void test_oversized_single_glyph_refuses_cleanly() {
  // The degenerate case already behaves: the very first glyph is oversized, so
  // nothing was drawn before the refusal. Keeps the fix honest -- it must not
  // regress the case that works today.
  Canvas c = bigCanvas(300.0f);
  const RenderOutput r = renderStatex(tex32("abc"), c);
  TEST_ASSERT_FALSE(r.ok());
  TEST_ASSERT_EQUAL_INT(0, r.coverageBlits);
  TEST_ASSERT_FALSE(stximg::inkStats(r.image).any());
}

static void test_parse_and_layout_refusals_draw_nothing() {
  // The phases that already satisfy the contract, asserted so the draw-phase
  // fix is not mistaken for the whole story.
  const char* refusing[] = {"\\nosuchcommand", "{a", "a}", "^2", ""};
  for (const char* tex : refusing) {
    const RenderOutput r = renderStatex(tex32(tex));
    TEST_ASSERT_FALSE(r.ok());
    TEST_ASSERT_EQUAL_INT(0, r.coverageBlits);
    TEST_ASSERT_FALSE(stximg::inkStats(r.image).any());
  }
}

static void test_a_refused_render_does_not_disturb_the_next_one() {
  // STX-API-02 holds even across a partial draw, and must keep holding.
  const RenderOutput before = renderStatex(tex32("x^2"));
  TEST_ASSERT_TRUE(before.ok());
  (void)renderStatex(tex32(kOversizeAfterInk), bigCanvas(70.0f));
  const RenderOutput after = renderStatex(tex32("x^2"));
  TEST_ASSERT_TRUE(after.ok());
  const stximg::Similarity s = stximg::compare(before.image, after.image);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_refusal_emits_no_primitives);
  RUN_TEST(test_refusal_leaves_the_canvas_blank);
  RUN_TEST(test_refusal_is_detected_before_drawing_starts);
  RUN_TEST(test_oversized_single_glyph_refuses_cleanly);
  RUN_TEST(test_parse_and_layout_refusals_draw_nothing);
  RUN_TEST(test_a_refused_render_does_not_disturb_the_next_one);
  return UNITY_END();
}
