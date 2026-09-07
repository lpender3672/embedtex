#ifndef STATEX_TEST_COMPARE_H
#define STATEX_TEST_COMPARE_H

// Numeric image-similarity metrics for asserting that two rendered formulas
// "look the same" without demanding bit-identical pixels.
//
// Why not exact pixel equality: the two things we compare (StaTeX today vs a
// frozen golden, or StaTeX vs the MicroTeX oracle) legitimately differ by
// sub-pixel rounding, antialiasing ramp, and origin conventions. Exact
// equality would be permanently red for uninteresting reasons; a similarity
// score with a stated threshold fails only when the *shape* moved.
//
// The metric set, cheapest first:
//   inkRatio  - total coverage mass ratio; catches "a glyph vanished"
//   dice/iou  - thresholded overlap; catches gross misplacement
//   ncc       - zero-mean normalised cross-correlation; brightness-invariant
//   ssim      - structural similarity over 8x8 uniform windows
//   rmse      - raw residual, for reporting
//
// All of them are computed AFTER an optional integer-translation alignment
// search, because a constant offset between two renders is a different (and
// usually less interesting) defect than a distorted layout. The recovered
// offset is reported separately so a test can assert on it directly.

#include <cstdint>
#include <string>

#include "stx_image.h"

namespace stximg {

struct Similarity {
  // Alignment recovered before scoring (b shifted by (dx, dy) onto a).
  int dx = 0;
  int dy = 0;

  double dice = 0.0;      // 1.0 = identical ink sets
  double iou = 0.0;       // 1.0 = identical ink sets
  double ncc = 0.0;       // 1.0 = perfectly correlated
  double ssim = 0.0;      // 1.0 = structurally identical
  double rmse = 0.0;      // 0.0 = identical (0..255 scale)
  double inkRatio = 0.0;  // mass(b) / mass(a); 1.0 = same amount of ink

  // Ink bounding boxes, pre-alignment, for diagnosis.
  InkStats statsA;
  InkStats statsB;

  std::string describe() const;
};

struct SimilarityOptions {
  std::uint8_t threshold = 32;  // ink threshold for dice/iou
  int maxShift = 0;             // +/- pixels searched during alignment
  bool alignOnCentroid = true;  // seed the search from the centroid offset
  int ssimWindow = 8;
};

/**
 * Compare two images. They need not be the same size: both are composited
 * onto a common canvas large enough for either, anchored at (0,0), before
 * alignment and scoring.
 */
Similarity compare(const Image& a, const Image& b,
                   const SimilarityOptions& opt = SimilarityOptions{});

/**
 * Thresholds a similarity result must clear to count as "the same picture".
 * Defaults are deliberately loose enough to tolerate antialiasing and
 * sub-pixel rounding, and tight enough to catch a moved or missing glyph.
 */
struct SimilarityGate {
  double minDice = 0.90;
  double minSsim = 0.85;
  double minNcc = 0.95;
  double minInkRatio = 0.90;
  double maxInkRatio = 1.10;
  int maxShift = 1;  // residual offset allowed after alignment

  /** Empty string if the result passes; otherwise why it did not. */
  std::string check(const Similarity& s) const;
};

// --- individual metrics, exposed for the engine's own self-tests -----------
double diceCoefficient(const Image& a, const Image& b, std::uint8_t threshold);
double iouCoefficient(const Image& a, const Image& b, std::uint8_t threshold);
double normalisedCrossCorrelation(const Image& a, const Image& b);
double structuralSimilarity(const Image& a, const Image& b, int window);
double rootMeanSquareError(const Image& a, const Image& b);

}  // namespace stximg

#endif  // STATEX_TEST_COMPARE_H
