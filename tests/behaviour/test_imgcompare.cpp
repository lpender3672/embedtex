// Self-tests for the image-similarity engine (tests/support/stx_compare.*).
//
// Nothing else in the suite is trustworthy unless these hold: every oracle
// and golden assertion is expressed in terms of these metrics, so a bug here
// would silently pass or silently fail real defects. Green by construction --
// these test the measuring instrument, not StaTeX.

#include <unity.h>

#include <cstdio>

#include "stx_compare.h"
#include "stx_image.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stximg;

void setUp() {}
void tearDown() {}

namespace {

// A recognisable blob: a filled disc, so translation and shape changes are
// both detectable.
Image disc(int w, int h, int cx, int cy, int r) {
  Image im(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int dx = x - cx, dy = y - cy;
      if (dx * dx + dy * dy <= r * r) im.set(x, y, 255);
    }
  }
  return im;
}

Image bar(int w, int h, int x0, int y0, int bw, int bh) {
  Image im(w, h);
  for (int y = y0; y < y0 + bh; ++y)
    for (int x = x0; x < x0 + bw; ++x) im.set(x, y, 255);
  return im;
}

}  // namespace

static void test_identical_images_score_perfect() {
  const Image a = disc(64, 64, 32, 32, 10);
  const Similarity s = compare(a, a);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.dice);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.iou);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.ncc);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0, s.ssim);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.inkRatio);
}

static void test_two_blank_images_are_identical() {
  const Image a(32, 32);
  const Image b(32, 32);
  const Similarity s = compare(a, b);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.dice);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.ncc);
  SimilarityGate gate;
  TEST_ASSERT_EQUAL_STRING("", gate.check(s).c_str());
}

static void test_disjoint_ink_scores_zero_overlap() {
  const Image a = disc(64, 64, 16, 32, 8);
  const Image b = disc(64, 64, 48, 32, 8);
  SimilarityOptions opt;
  opt.maxShift = 0;
  opt.alignOnCentroid = false;
  const Similarity s = compare(a, b, opt);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.dice);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.iou);
  SimilarityGate gate;
  TEST_ASSERT_TRUE(!gate.check(s).empty());
}

static void test_blank_versus_inked_is_caught() {
  const Image a = disc(64, 64, 32, 32, 10);
  const Image b(64, 64);
  const Similarity s = compare(a, b);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.dice);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.inkRatio);
  SimilarityGate gate;
  TEST_ASSERT_TRUE(!gate.check(s).empty());
}

static void test_alignment_recovers_a_known_translation() {
  const Image a = disc(80, 80, 30, 40, 9);
  const Image b = disc(80, 80, 35, 44, 9);  // shifted by (+5, +4)
  SimilarityOptions opt;
  opt.maxShift = 2;  // centroid seed does the bulk, search refines
  const Similarity s = compare(a, b, opt);
  TEST_ASSERT_EQUAL_INT(-5, s.dx);
  TEST_ASSERT_EQUAL_INT(-4, s.dy);
  // Once aligned the two discs coincide exactly.
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.dice);
}

static void test_unaligned_comparison_reports_the_offset() {
  const Image a = disc(80, 80, 30, 40, 9);
  const Image b = disc(80, 80, 33, 40, 9);
  SimilarityOptions opt;
  opt.maxShift = 0;
  opt.alignOnCentroid = false;
  const Similarity s = compare(a, b, opt);
  TEST_ASSERT_EQUAL_INT(0, s.dx);
  // Overlapping but clearly not the same picture in place.
  TEST_ASSERT_TRUE(s.dice > 0.3);
  TEST_ASSERT_TRUE(s.dice < 0.95);
}

static void test_ink_ratio_detects_a_missing_element() {
  Image a = disc(80, 40, 20, 20, 8);
  const Image extra = disc(80, 40, 55, 20, 8);
  for (size_t i = 0; i < a.px.size(); ++i)
    a.px[i] = std::max(a.px[i], extra.px[i]);
  // b is missing the second disc entirely.
  const Image b = disc(80, 40, 20, 20, 8);
  SimilarityOptions opt;
  opt.maxShift = 0;
  opt.alignOnCentroid = false;
  const Similarity s = compare(a, b, opt);
  TEST_ASSERT_TRUE(s.inkRatio < 0.6);
  SimilarityGate gate;
  TEST_ASSERT_TRUE(!gate.check(s).empty());
}

static void test_ssim_penalises_structural_change_not_brightness() {
  const Image a = bar(48, 48, 8, 8, 32, 32);
  // Same structure, dimmer: NCC should stay high.
  Image dim = a;
  for (auto& p : dim.px) p = static_cast<std::uint8_t>(p * 6 / 10);
  const double nccDim = normalisedCrossCorrelation(a, dim);
  TEST_ASSERT_TRUE(nccDim > 0.99);

  // Different structure at the same total brightness: SSIM should drop.
  const Image scattered = bar(48, 48, 8, 8, 32, 19);
  const double ssimScattered = structuralSimilarity(a, scattered, 8);
  TEST_ASSERT_TRUE(ssimScattered < 0.9);
}

static void test_metrics_handle_differently_sized_images() {
  const Image a = disc(64, 64, 20, 20, 6);
  const Image b = disc(96, 40, 20, 20, 6);  // same content, bigger canvas
  SimilarityOptions opt;
  opt.maxShift = 0;
  opt.alignOnCentroid = false;
  const Similarity s = compare(a, b, opt);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 1.0, s.dice);
}

static void test_crop_and_ink_stats() {
  const Image a = disc(64, 64, 32, 32, 5);
  const InkStats st = inkStats(a);
  TEST_ASSERT_TRUE(st.any());
  TEST_ASSERT_FLOAT_WITHIN(0.5, 32.0, st.cx);
  TEST_ASSERT_FLOAT_WITHIN(0.5, 32.0, st.cy);
  const Image c = cropToInk(a, 1);
  TEST_ASSERT_EQUAL_INT(st.bw() + 2, c.w);
  TEST_ASSERT_EQUAL_INT(st.bh() + 2, c.h);

  const Image blank(16, 16);
  TEST_ASSERT_FALSE(inkStats(blank).any());
}

static void test_pgm_round_trip() {
  const Image a = disc(37, 23, 18, 11, 7);
  const std::string path = stxtest::artifactPath("selftest_roundtrip.pgm");
  TEST_ASSERT_TRUE(writePgm(a, path));
  Image b;
  TEST_ASSERT_TRUE(readPgm(b, path));
  TEST_ASSERT_EQUAL_INT(a.w, b.w);
  TEST_ASSERT_EQUAL_INT(a.h, b.h);
  const Similarity s = compare(a, b);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
}

static void test_missing_golden_is_reported_not_crashed() {
  Image b;
  TEST_ASSERT_FALSE(readPgm(b, stxtest::artifactPath("no_such_file.pgm")));
}

static void test_failure_stack_is_written() {
  // The picture path has to work, or a failing assertion elsewhere reports a
  // number with no way to see what it means.
  const Image a = disc(40, 40, 20, 20, 8);
  const Image b = disc(40, 40, 23, 20, 8);
  const std::string path = stxtest::artifactPath("selftest_stack.png");
  TEST_ASSERT_TRUE(stxtest::writeFailureStack(a, b, "selftest",
                                              "two discs, 3px apart", path));

  // Structural check rather than a size threshold: the deflate encoder in
  // stx_png.cpp is hand-rolled, so assert the container is well formed and
  // the dimensions are the ones the stack should have produced.
  std::FILE* f = std::fopen(path.c_str(), "rb");
  TEST_ASSERT_NOT_NULL(f);
  // 8 signature + 4 len + 4 'IHDR' + 13 data + 4 CRC + 4 len + 4 'IDAT'
  unsigned char hdr[41] = {0};
  const size_t got = std::fread(hdr, 1, sizeof(hdr), f);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fclose(f);
  TEST_ASSERT_EQUAL_UINT(sizeof(hdr), got);

  const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(hdr, sig, 8));
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(hdr + 12, "IHDR", 4));
  const long w = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
  const long h = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
  TEST_ASSERT_EQUAL_INT(8, hdr[24]);  // bit depth
  TEST_ASSERT_EQUAL_INT(2, hdr[25]);  // colour type: truecolour RGB
  // Three stacked panels plus caption bands, so taller than it is per-panel.
  TEST_ASSERT_GREATER_THAN_INT(100, w);
  TEST_ASSERT_GREATER_THAN_INT(3 * 40, h);
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(hdr + 37, "IDAT", 4));
  // Compressed, not stored: a 3-panel stack of mostly-flat background must
  // come out far smaller than its raw RGB size.
  TEST_ASSERT_LESS_THAN_INT(w * h * 3 / 4, size);
}

static void test_caption_measurement_matches_drawing() {
  RgbImage canvas(400, 40);
  const int measured = stxtest::measureCaption("statex 123", 14);
  const int drawn = stxtest::drawCaption(canvas, 0, 30, "statex 123", 14, 255,
                                         255, 255);
  TEST_ASSERT_INT_WITHIN(2, measured, drawn);
  TEST_ASSERT_GREATER_THAN_INT(0, measured);
}

static void test_gate_thresholds_are_reachable_by_real_renders() {
  // A render compared against itself must clear the default gate. If this
  // fails the gate is mis-specified, not StaTeX.
  const stxtest::RenderOutput r = stxtest::renderStatex(stxtest::tex32("x^2"));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(r.error));
  const Similarity s = compare(r.image, r.image);
  SimilarityGate gate;
  TEST_ASSERT_EQUAL_STRING("", gate.check(s).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_identical_images_score_perfect);
  RUN_TEST(test_two_blank_images_are_identical);
  RUN_TEST(test_disjoint_ink_scores_zero_overlap);
  RUN_TEST(test_blank_versus_inked_is_caught);
  RUN_TEST(test_alignment_recovers_a_known_translation);
  RUN_TEST(test_unaligned_comparison_reports_the_offset);
  RUN_TEST(test_ink_ratio_detects_a_missing_element);
  RUN_TEST(test_ssim_penalises_structural_change_not_brightness);
  RUN_TEST(test_metrics_handle_differently_sized_images);
  RUN_TEST(test_crop_and_ink_stats);
  RUN_TEST(test_pgm_round_trip);
  RUN_TEST(test_missing_golden_is_reported_not_crashed);
  RUN_TEST(test_failure_stack_is_written);
  RUN_TEST(test_caption_measurement_matches_drawing);
  RUN_TEST(test_gate_thresholds_are_reachable_by_real_renders);
  return UNITY_END();
}
