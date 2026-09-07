#include "stx_compare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace stximg {
namespace {

// Grow both images onto one canvas so every metric sees the same geometry.
void unify(const Image& a, const Image& b, Image& ua, Image& ub) {
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  ua = placeOn(a, w, h, 0, 0);
  ub = placeOn(b, w, h, 0, 0);
}

}  // namespace

double diceCoefficient(const Image& a, const Image& b, std::uint8_t threshold) {
  long inter = 0, na = 0, nb = 0;
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool ia = a.at(x, y) >= threshold;
      const bool ib = b.at(x, y) >= threshold;
      if (ia) na++;
      if (ib) nb++;
      if (ia && ib) inter++;
    }
  }
  if (na + nb == 0) return 1.0;  // two blank images are identical
  return 2.0 * inter / static_cast<double>(na + nb);
}

double iouCoefficient(const Image& a, const Image& b, std::uint8_t threshold) {
  long inter = 0, uni = 0;
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const bool ia = a.at(x, y) >= threshold;
      const bool ib = b.at(x, y) >= threshold;
      if (ia && ib) inter++;
      if (ia || ib) uni++;
    }
  }
  if (uni == 0) return 1.0;
  return static_cast<double>(inter) / static_cast<double>(uni);
}

double normalisedCrossCorrelation(const Image& a, const Image& b) {
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  const double n = static_cast<double>(w) * h;
  if (n <= 0.0) return 1.0;
  double sa = 0.0, sb = 0.0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      sa += a.at(x, y);
      sb += b.at(x, y);
    }
  }
  const double ma = sa / n, mb = sb / n;
  double num = 0.0, da = 0.0, db = 0.0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double va = a.at(x, y) - ma;
      const double vb = b.at(x, y) - mb;
      num += va * vb;
      da += va * va;
      db += vb * vb;
    }
  }
  if (da <= 0.0 && db <= 0.0) return 1.0;   // both flat
  if (da <= 0.0 || db <= 0.0) return 0.0;   // one flat, one not
  return num / std::sqrt(da * db);
}

double structuralSimilarity(const Image& a, const Image& b, int window) {
  if (window < 2) window = 8;
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  if (w <= 0 || h <= 0) return 1.0;
  // Standard SSIM constants for an 8-bit dynamic range.
  const double L = 255.0;
  const double C1 = (0.01 * L) * (0.01 * L);
  const double C2 = (0.03 * L) * (0.03 * L);

  double total = 0.0;
  long windows = 0;
  // Uniform (box) windows rather than Gaussian: cheaper, and for the sparse
  // high-contrast content we render the difference is immaterial.
  for (int wy = 0; wy < h; wy += window) {
    for (int wx = 0; wx < w; wx += window) {
      const int x1 = std::min(wx + window, w);
      const int y1 = std::min(wy + window, h);
      const double n = static_cast<double>(x1 - wx) * (y1 - wy);
      if (n <= 1.0) continue;
      double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
      for (int y = wy; y < y1; ++y) {
        for (int x = wx; x < x1; ++x) {
          const double va = a.at(x, y);
          const double vb = b.at(x, y);
          sa += va;
          sb += vb;
          saa += va * va;
          sbb += vb * vb;
          sab += va * vb;
        }
      }
      const double ma = sa / n;
      const double mb = sb / n;
      const double va = (saa - n * ma * ma) / (n - 1.0);
      const double vb = (sbb - n * mb * mb) / (n - 1.0);
      const double cab = (sab - n * ma * mb) / (n - 1.0);
      const double s = ((2 * ma * mb + C1) * (2 * cab + C2)) /
                       ((ma * ma + mb * mb + C1) * (va + vb + C2));
      total += s;
      windows++;
    }
  }
  return windows > 0 ? total / windows : 1.0;
}

double rootMeanSquareError(const Image& a, const Image& b) {
  const int w = std::max(a.w, b.w);
  const int h = std::max(a.h, b.h);
  const double n = static_cast<double>(w) * h;
  if (n <= 0.0) return 0.0;
  double acc = 0.0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double d = static_cast<double>(a.at(x, y)) - b.at(x, y);
      acc += d * d;
    }
  }
  return std::sqrt(acc / n);
}

Similarity compare(const Image& a, const Image& b,
                   const SimilarityOptions& opt) {
  Similarity r;
  r.statsA = inkStats(a, opt.threshold);
  r.statsB = inkStats(b, opt.threshold);

  Image ua, ub;
  unify(a, b, ua, ub);

  // Alignment: seed from the centroid difference (rounded), then hill-search
  // a (2*maxShift+1)^2 neighbourhood maximising Dice. Dice is the right
  // objective here -- it is the metric most sensitive to translation and it
  // saturates cleanly, unlike NCC on sparse content.
  int seedX = 0, seedY = 0;
  if (opt.alignOnCentroid && r.statsA.any() && r.statsB.any()) {
    seedX = static_cast<int>(std::lround(r.statsA.cx - r.statsB.cx));
    seedY = static_cast<int>(std::lround(r.statsA.cy - r.statsB.cy));
  }
  int bestDx = 0, bestDy = 0;
  double bestScore = -1.0;
  const int s = std::max(0, opt.maxShift);
  for (int dy = seedY - s; dy <= seedY + s; ++dy) {
    for (int dx = seedX - s; dx <= seedX + s; ++dx) {
      const Image shifted = placeOn(ub, ua.w, ua.h, dx, dy);
      const double score = diceCoefficient(ua, shifted, opt.threshold);
      if (score > bestScore) {
        bestScore = score;
        bestDx = dx;
        bestDy = dy;
      }
    }
  }
  // With maxShift == 0 and no centroid seeding this degenerates to (0,0),
  // which is what golden-image regression wants.
  r.dx = bestDx;
  r.dy = bestDy;

  const Image aligned = placeOn(ub, ua.w, ua.h, bestDx, bestDy);
  r.dice = diceCoefficient(ua, aligned, opt.threshold);
  r.iou = iouCoefficient(ua, aligned, opt.threshold);
  r.ncc = normalisedCrossCorrelation(ua, aligned);
  r.ssim = structuralSimilarity(ua, aligned, opt.ssimWindow);
  r.rmse = rootMeanSquareError(ua, aligned);
  r.inkRatio = r.statsA.mass > 0.0
                   ? r.statsB.mass / r.statsA.mass
                   : (r.statsB.mass > 0.0 ? 1e9 : 1.0);
  return r;
}

std::string Similarity::describe() const {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "dice=%.4f iou=%.4f ncc=%.4f ssim=%.4f rmse=%.2f ink=%.4f "
                "align=(%+d,%+d) bboxA=%dx%d bboxB=%dx%d",
                dice, iou, ncc, ssim, rmse, inkRatio, dx, dy, statsA.bw(),
                statsA.bh(), statsB.bw(), statsB.bh());
  return std::string(buf);
}

std::string SimilarityGate::check(const Similarity& s) const {
  char buf[256];
  if (s.dice < minDice) {
    std::snprintf(buf, sizeof(buf), "dice %.4f < %.4f", s.dice, minDice);
    return buf;
  }
  if (s.ssim < minSsim) {
    std::snprintf(buf, sizeof(buf), "ssim %.4f < %.4f", s.ssim, minSsim);
    return buf;
  }
  if (s.ncc < minNcc) {
    std::snprintf(buf, sizeof(buf), "ncc %.4f < %.4f", s.ncc, minNcc);
    return buf;
  }
  if (s.inkRatio < minInkRatio || s.inkRatio > maxInkRatio) {
    std::snprintf(buf, sizeof(buf), "ink ratio %.4f outside [%.2f, %.2f]",
                  s.inkRatio, minInkRatio, maxInkRatio);
    return buf;
  }
  if (std::abs(s.dx) > maxShift || std::abs(s.dy) > maxShift) {
    std::snprintf(buf, sizeof(buf), "residual offset (%+d,%+d) exceeds %d",
                  s.dx, s.dy, maxShift);
    return buf;
  }
  return std::string();
}

}  // namespace stximg
