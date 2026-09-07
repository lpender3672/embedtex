// RED -- the MicroTeX differential (docs/StaTeX-dev-process.md §4).
//
// StaTeX was rewritten from lib/MicroTeX. For the subset of TeX StaTeX
// supports, the two should lay a formula out the same way. They do not, and
// the dev-process document already predicted why:
//
//   "Same metric source. StaTeX's flash tables must be generated from the very
//    values MicroTeX uses (lib/MicroTeX/res/builtin/*.res.cpp,
//    lib/MicroTeX/res/font/*.def.cpp). Otherwise divergence is just different
//    input numbers, not an algorithm bug."
//
// That precondition is not met today:
//   - the global layout constants in lib/StaTeX/statex_fontparams.h (axis
//     height, rule thickness, script shifts, gaps) are fourteen hand-picked
//     floats, not MicroTeX's tex_param.res.cpp values;
//   - the per-glyph metrics come from Latin Modern via tools/genfont, not from
//     MicroTeX's res/font/*.def.cpp.
//
// So these tests are red for a known and specific reason, and they are the
// measurement that says how far off things are. They go green when the flash
// tables are regenerated from MicroTeX's own numbers.
//
// IMPORTANT, and a limit on what these scores mean.
//
// The reference is rasterised by FreeType from the real Computer Modern faces
// (ft_raster.h); StaTeX's side is sampled from its SDF atlas. That was a
// deliberate change -- it makes the reference genuine TeX output rather than
// StaTeX's atlas pretending to be one, and it is why the pictures are worth
// looking at. But it means these scores measure *position plus rasteriser*,
// where they once measured position alone.
//
// The two rasterisers do not agree and never will: StaTeX's SDF ramp lays
// down 3-7% more ink than FreeType's hinted outlines at the same size and
// position. `style_roman` shows this in isolation -- identical 11x16 ink
// bounding box, alignment (0,0), and it still fails the gate on ink ratio
// 1.16 alone. `char_single`, a bare `x` with a correct advance (within 0.12%
// of the TFM value) and correct position, scores dice 0.83 at 36px.
//
// So dice ~0.85 is roughly the FLOOR for a perfectly laid out formula, not a
// passing mark. Read a case scoring 0.75-0.88 with matching bounding boxes and
// zero alignment offset as "layout agrees, rasterisers differ"; read a case
// scoring below ~0.7, or with a bounding box that disagrees by more than a
// pixel or two, as a real layout defect. Only the second kind is something
// the remaining plan phases can fix.

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "microtex_oracle.h"
#include "stx_compare.h"
#include "stx_corpus.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stxoracle;
using namespace stxtest;

void setUp() {}
void tearDown() {}

namespace {

// Every comparison runs at three em sizes, not one.
//
// A single size cannot distinguish "the layout is wrong" from "the layout is
// right and one size happens to round badly". Agreement that holds at 22, 36
// and 64px is a claim about the algorithm; agreement at 36px alone is a
// coincidence that has not been ruled out. This is not hypothetical -- the
// sweep is what exposed `Face::Italic` being sourced from the wrong font: the
// case scored 0.97 at 22px and 0.83 at 36px, and a uniform error would have
// scored uniformly.
//
// 22px is what the device actually renders at, so it is the size that has to
// be right; 64px shows shape divergence that pixel rounding hides at 22.
constexpr float kOracleSizes[] = {22.0f, 36.0f, 64.0f};
constexpr int kSizeCount = static_cast<int>(sizeof(kOracleSizes) /
                                            sizeof(kOracleSizes[0]));

// Sized from measurement, not from a round number. The widest case in the
// corpus (`showcase`) inks 10.9em right of the origin, 2.7em above the
// baseline and 2.2em below, so these factors leave ~25% headroom in every
// direction at every size.
//
// Keeping the canvas tight matters: every similarity metric sweeps the whole
// canvas several times per case, so a canvas padded "to be safe" is paid for
// on each of 41 cases at each of 3 sizes. The old fixed 1400x900 spent 95% of
// its time scanning blank paper and made this suite take 91 seconds to
// compare 37ms of rendering. The failure pictures are unaffected --
// writeFailureStack crops to the ink either way.
//
// None of that reasoning is safe on its own, so `assertNoClipping` checks the
// sizing actually held rather than trusting it.
Canvas oracleCanvas(float sizePx) {
  Canvas c;
  c.sizePx = sizePx;
  c.originX = 0.5f * sizePx;
  c.baseline = 3.4f * sizePx;
  c.width = static_cast<int>(14.5f * sizePx) + 16;
  c.height = static_cast<int>(6.4f * sizePx) + 16;
  return c;
}

// A canvas one pixel too small truncates a glyph and still produces a
// plausible-looking similarity score, so the tight sizing above has to be
// enforced rather than assumed.
void assertNoClipping(const char* id, float sizePx, const stximg::Image& a,
                      const stximg::Image& b) {
  if (a.clipped != 0 || b.clipped != 0) {
    std::printf("  %-22s @%.0fpx CANVAS TOO SMALL: %ld statex / %ld oracle "
                "ink px fell outside %dx%d\n",
                id, static_cast<double>(sizePx), a.clipped, b.clipped, a.w,
                a.h);
  }
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(a.clipped));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(b.clipped));
}

// Cases the oracle cannot speak to: StaTeX-only refusal behaviour, and
// environments MicroTeX renders differently enough that the comparison is
// about grammar rather than metrics.
bool comparable(const Case& c) {
  return c.expect == Expect::Renders && isSettled(c);
}

}  // namespace

static void test_oracle_is_available() {
  std::string err;
  const bool ready = ensureReady(&err);
  if (!ready) std::printf("  oracle unavailable: %s\n", err.c_str());
  TEST_ASSERT_TRUE(ready);
}

static void test_glyph_counts_agree() {
  TEST_DEFECT("ORACLE-METRICS", "StaTeX and MicroTeX disagree on the glyph set");
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  int compared = 0, disagreed = 0;
  for (const float px : kOracleSizes) {
    const Canvas canvas = oracleCanvas(px);
    for (const Case& c : corpus()) {
      if (!comparable(c)) continue;
      const RenderOutput s = renderStatex(tex32(c.tex), canvas);
      const OracleRender o = render(c.tex, canvas);
      if (!s.ok() || !o.ok) continue;
      compared++;
      if (s.coverageBlits != o.glyphCount()) {
        std::printf("  %-22s @%.0fpx StaTeX %2d glyphs, MicroTeX %2d\n", c.id,
                    static_cast<double>(px), s.coverageBlits, o.glyphCount());
        disagreed++;
      }
    }
  }
  std::printf("  %d case/size pairs compared, %d disagree on glyph count\n",
              compared, disagreed);
  TEST_ASSERT_GREATER_THAN_INT(0, compared);
  TEST_ASSERT_EQUAL_INT(0, disagreed);
}

static void test_overall_width_agrees() {
  TEST_DEFECT("ORACLE-METRICS", "hand-picked font parameters give different advances");
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  // Seeded at 1.0 (perfect agreement) so any deviation registers. Seeding at
  // 0.0 meant a case had to be more than 100% out before it counted as worst,
  // which silently reported "worst 0.000" once the outliers were fixed.
  double worst = 1.0;
  const char* worstId = "";
  float worstSize = 0.0f;
  int compared = 0, outside = 0;
  for (const float px : kOracleSizes) {
    const Canvas canvas = oracleCanvas(px);
    int outsideHere = 0;
    for (const Case& c : corpus()) {
      if (!comparable(c)) continue;
      const RenderOutput s = renderStatex(tex32(c.tex), canvas);
      const OracleRender o = render(c.tex, canvas);
      if (!s.ok() || !o.ok || o.width <= 0.0f) continue;
      compared++;
      const double ratio = static_cast<double>(s.stats.width) / o.width;
      if (std::fabs(ratio - 1.0) > std::fabs(worst - 1.0)) {
        worst = ratio;
        worstId = c.id;
        worstSize = px;
      }
      if (ratio < 0.95 || ratio > 1.05) {
        std::printf("  %-22s @%.0fpx StaTeX %7.2f  MicroTeX %7.2f  ratio %.3f\n",
                    c.id, static_cast<double>(px),
                    static_cast<double>(s.stats.width), o.width, ratio);
        outside++;
        outsideHere++;
      }
    }
    std::printf("  -- %.0fpx: %d outside +/-5%%\n", static_cast<double>(px),
                outsideHere);
  }
  std::printf("  %d case/size pairs compared, %d outside +/-5%%, "
              "worst %.3f on '%s' @%.0fpx\n",
              compared, outside, worst, worstId,
              static_cast<double>(worstSize));
  TEST_ASSERT_EQUAL_INT(0, outside);
}

static void test_rendered_pictures_match() {
  TEST_DEFECT("ORACLE-METRICS", "layout positions diverge from the reference");
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  stximg::SimilarityOptions opt;
  opt.maxShift = 3;  // absorb a constant origin difference, not a layout one
  stximg::SimilarityGate gate;
  gate.minDice = 0.85;
  gate.minSsim = 0.80;
  gate.minNcc = 0.90;
  gate.maxShift = 3;

  const int stale = clearArtifacts("oracle_");
  if (stale > 0) std::printf("  cleared %d picture(s) from a previous run\n", stale);

  int compared = 0, failed = 0, pictures = 0;
  int failedAt[kSizeCount] = {0};
  // A case is only "converged" when it clears the gate at every size. Counting
  // per size as well as overall keeps a case that passes at 22px and fails at
  // 64px from reading as half-fixed.
  int casesFailingSomewhere = 0;

  for (const Case& c : corpus()) {
    if (!comparable(c)) continue;

    // Score the case at every size before writing anything, so the picture can
    // be written for the size that actually looks worst rather than whichever
    // size happens to be iterated last.
    double worstDice = 2.0;
    int worstIdx = -1;
    std::string worstWhy;
    stximg::Similarity worstSim;
    bool anyFailure = false;

    for (int k = 0; k < kSizeCount; ++k) {
      const float px = kOracleSizes[k];
      const Canvas canvas = oracleCanvas(px);
      const RenderOutput s = renderStatex(tex32(c.tex), canvas);
      const OracleRender o = render(c.tex, canvas);
      if (!s.ok() || !o.ok) continue;

      int unrendered = 0;
      const stximg::Image ref = rasterise(o, canvas, &unrendered);
      assertNoClipping(c.id, px, s.image, ref);

      // A glyph FreeType could not draw leaves a hole in the reference, so the
      // scores would be meaningless -- but the picture is still worth having,
      // with the caveat written into it.
      if (unrendered > 0) {
        if (k == 0) {
          std::printf("  %-22s not scored: %d glyph(s) would not render\n",
                      c.id, unrendered);
          char note[192];
          std::snprintf(note, sizeof(note),
                        "%s   NOT SCORED: %d oracle glyph(s) failed to render",
                        c.tex, unrendered);
          if (writeFailureStack(ref, s.image, c.id, note,
                                artifactPath(artifactName(
                                    std::string("oracle_") + c.id, ".png")))) {
            pictures++;
          }
        }
        continue;
      }

      compared++;
      const stximg::Similarity sim = stximg::compare(s.image, ref, opt);
      const std::string why = gate.check(sim);
      if (!why.empty()) {
        failed++;
        failedAt[k]++;
        anyFailure = true;
      }
      if (sim.dice < worstDice) {
        worstDice = sim.dice;
        worstIdx = k;
        worstWhy = why;
        worstSim = sim;
      }
    }

    if (!anyFailure || worstIdx < 0) continue;
    casesFailingSomewhere++;

    // Re-render at the worst size purely to draw it.
    const float px = kOracleSizes[worstIdx];
    const Canvas canvas = oracleCanvas(px);
    const RenderOutput s = renderStatex(tex32(c.tex), canvas);
    const OracleRender o = render(c.tex, canvas);
    int unrendered = 0;
    const stximg::Image ref = rasterise(o, canvas, &unrendered);

    std::printf("  %-22s worst @%.0fpx: %s  (%s)\n", c.id,
                static_cast<double>(px),
                worstWhy.empty() ? "passes here" : worstWhy.c_str(),
                worstSim.describe().c_str());
    char note[360];
    std::snprintf(note, sizeof(note), "%s   @%.0fpx (worst of %d sizes)   %s   %s",
                  c.tex, static_cast<double>(px), kSizeCount,
                  worstWhy.empty() ? "" : worstWhy.c_str(),
                  worstSim.describe().c_str());
    char stem[128];
    std::snprintf(stem, sizeof(stem), "oracle_%s_%.0fpx", c.id,
                  static_cast<double>(px));
    if (writeFailureStack(ref, s.image, c.id, note,
                          artifactPath(artifactName(stem, ".png")))) {
      pictures++;
    }
  }

  for (int k = 0; k < kSizeCount; ++k) {
    std::printf("  -- %.0fpx: %d diverge\n",
                static_cast<double>(kOracleSizes[k]), failedAt[k]);
  }
  std::printf("  %d cases diverge at one or more size\n", casesFailingSomewhere);
  std::printf("  %d case/size pairs compared against the oracle, %d diverge\n",
              compared, failed);
  std::printf("  %d stacked PNGs written to %s\n", pictures,
              artifactDir().c_str());
  TEST_ASSERT_GREATER_THAN_INT(0, compared);
  TEST_ASSERT_EQUAL_INT(0, failed);
}

// How far a glyph may sit from where MicroTeX put it, in device pixels.
//
// Two terms, each with a reason:
//
//   0.75px  A constant sub-pixel offset, not a layout error. Simple cases show
//           a baseline delta of 0.0215em at 22px, 0.0126em at 36px and 0.0081em
//           at 64px -- all almost exactly half a pixel. Constant in pixels
//           rather than in em means it is pixel-grid rounding, and no layout
//           change will remove it.
//
//   0.01em  Accumulated metric quantisation. GlyphRecord stores advances as
//           1/256 of an em, so a row of n glyphs can drift by n/256 em against
//           MicroTeX's exact TFM values purely from storage precision.
//
// Deliberately NOT a tolerance on ink, shape or coverage: this test exists
// because the image comparison cannot separate "placed wrongly" from "inked
// differently", and it must not reintroduce the confusion it was built to
// avoid.
double positionTolerancePx(float emPx) {
  return 0.75 + 0.01 * static_cast<double>(emPx);
}

static void test_glyph_positions_agree() {
  TEST_DEFECT("ORACLE-METRICS", "glyphs are placed differently from TeX");
  // The layout-only differential, and the one that actually measures the thing
  // the remaining plan phases are meant to fix. `test_rendered_pictures_match`
  // above compares pixels, which conflates placement with rasterisation and
  // bottoms out around dice 0.85 even for a perfectly placed formula. This
  // compares pen positions, so a passing case means "StaTeX put every glyph
  // where TeX would", full stop.
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));

  int converged = 0, diverged = 0, unmatched = 0;
  double worstEm = 0.0;
  const char* worstId = "";

  for (const Case& c : corpus()) {
    if (!comparable(c)) continue;

    double worstDx = 0.0, worstDy = 0.0;  // in em, for reporting
    double worstOverPx = 0.0;             // px beyond tolerance, for the verdict
    float atSize = 0.0f;
    int countMismatch = 0;
    bool measured = false;

    for (const float px : kOracleSizes) {
      const Canvas canvas = oracleCanvas(px);
      const RenderOutput s = renderStatex(tex32(c.tex), canvas);
      const OracleRender o = render(c.tex, canvas);
      if (!s.ok() || !o.ok) continue;

      // A truncated probe would compare only a prefix and look like agreement.
      TEST_ASSERT_FALSE(s.placementsTruncated);

      if (s.placements.size() != o.glyphs.size()) {
        countMismatch = static_cast<int>(s.placements.size()) -
                        static_cast<int>(o.glyphs.size());
        continue;
      }
      measured = true;
      const double tol = positionTolerancePx(px);
      for (size_t i = 0; i < s.placements.size(); ++i) {
        const double dxPx = s.placements[i].penX - o.glyphs[i].x;
        const double dyPx = s.placements[i].baseline - o.glyphs[i].baselineY;
        const double overX = std::fabs(dxPx) - tol;
        const double overY = std::fabs(dyPx) - tol;
        const double over = overX > overY ? overX : overY;
        if (over > worstOverPx) {
          worstOverPx = over;
          worstDx = dxPx / px;
          worstDy = dyPx / px;
          atSize = px;
        }
      }
    }

    if (countMismatch != 0) {
      std::printf("  %-22s UNMATCHED: %+d glyphs vs the oracle\n", c.id,
                  countMismatch);
      unmatched++;
      continue;
    }
    if (!measured) continue;

    const double worstCase =
        std::fabs(worstDx) > std::fabs(worstDy) ? std::fabs(worstDx)
                                                : std::fabs(worstDy);
    if (worstOverPx > 0.0) {
      diverged++;
      std::printf("  %-22s worst dx=%+.4f dy=%+.4f em @%.0fpx (%.2fpx over)\n",
                  c.id, worstDx, worstDy, static_cast<double>(atSize),
                  worstOverPx);
      if (worstCase > worstEm) {
        worstEm = worstCase;
        worstId = c.id;
      }
    } else {
      converged++;
    }
  }

  std::printf("  %d cases place every glyph within tolerance, %d do not, "
              "%d unmatched\n",
              converged, diverged, unmatched);
  std::printf("  worst offset %.3f em on '%s'\n", worstEm, worstId);
  TEST_ASSERT_GREATER_THAN_INT(0, converged);
  TEST_ASSERT_EQUAL_INT(0, diverged);
  TEST_ASSERT_EQUAL_INT(0, unmatched);
}

// `test_glyph_pen_positions_agree` lived here: it rendered "abcdef" and
// compared the two ink bounding-box WIDTHS within a couple of pixels. That was
// a proxy for advance accumulation, invented before pen positions could be
// compared directly. `test_glyph_positions_agree` above now checks every glyph
// of every corpus case at three sizes against the oracle's own pen positions,
// which is both stricter and free of rasterisation noise, so the proxy was
// only able to produce a second opinion nobody would act on.

static void test_fraction_rule_agrees() {
  TEST_DEFECT("ORACLE-METRICS", "rule thickness and gaps are hand-picked");
  std::string err;
  TEST_ASSERT_TRUE(ensureReady(&err));
  // Tolerances are em-relative for the same reason as the row test above.
  for (const float px : kOracleSizes) {
    const Canvas canvas = oracleCanvas(px);
    const RenderOutput s = renderStatex(tex32("\\frac{a}{b}"), canvas);
    const OracleRender o = render("\\frac{a}{b}", canvas);
    TEST_ASSERT_TRUE(s.ok());
    TEST_ASSERT_TRUE(o.ok);
    TEST_ASSERT_GREATER_THAN_INT(0, static_cast<int>(s.rulePlacements.size()));
    TEST_ASSERT_GREATER_THAN_INT(0, static_cast<int>(o.rules.size()));

    const RulePlacement& sr = s.rulePlacements.front();
    const RulePlacement& orr = o.rules.front();
    std::printf("  @%.0fpx bar  StaTeX w=%.2f h=%.2f top=%.2f | "
                "MicroTeX w=%.2f h=%.2f top=%.2f\n",
                static_cast<double>(px), static_cast<double>(sr.w),
                static_cast<double>(sr.h), static_cast<double>(sr.top),
                static_cast<double>(orr.w), static_cast<double>(orr.h),
                static_cast<double>(orr.top));
    TEST_ASSERT_FLOAT_WITHIN(px / 22.0, static_cast<double>(orr.w),
                             static_cast<double>(sr.w));
    TEST_ASSERT_FLOAT_WITHIN(px / 44.0, static_cast<double>(orr.h),
                             static_cast<double>(sr.h));
  }
}

static void test_math_variables_are_set_in_italic() {
  // A typeface question rather than a metric one, kept separate so it does not
  // contaminate the image scores above. TeX sets math variables in italic,
  // which is why the oracle emits a cmmi10 slot for a bare `x`. StaTeX used to
  // default every character to Face::Roman -- defect MATH-ITALIC -- so `x` and
  // `\mathit{x}` rendered as different glyphs. They must now be the same one.
  for (const float px : kOracleSizes) {
    const Canvas canvas = oracleCanvas(px);
    const RenderOutput bare = renderStatex(tex32("x"), canvas);
    const RenderOutput italic = renderStatex(tex32("\\mathit{x}"), canvas);
    TEST_ASSERT_TRUE(bare.ok());
    TEST_ASSERT_TRUE(italic.ok());
    stximg::SimilarityOptions opt;
    opt.maxShift = 1;
    const stximg::Similarity sim =
        stximg::compare(bare.image, italic.image, opt);
    std::printf("  @%.0fpx 'x' vs '\\mathit{x}': dice=%.4f align=(%+d,%+d)\n",
                static_cast<double>(px), sim.dice, sim.dx, sim.dy);
    TEST_ASSERT_TRUE(sim.dice > 0.95);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_oracle_is_available);
  RUN_TEST(test_glyph_counts_agree);
  RUN_TEST(test_overall_width_agrees);
  RUN_TEST(test_rendered_pictures_match);
  RUN_TEST(test_glyph_positions_agree);
  RUN_TEST(test_fraction_rule_agrees);
  RUN_TEST(test_math_variables_are_set_in_italic);
  return UNITY_END();
}
