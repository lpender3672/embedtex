#ifndef STATEX_RECORD_H
#define STATEX_RECORD_H

#include "statex_graphics.h"

namespace statex {

/** One recorded draw primitive (test/oracle fixture). */
struct DrawOp {
  enum Kind : u8 { Glyph, Rule } kind;
  float x;
  float y;     // top-left (coverage) or rule top
  float w;
  float h;
  float fill;  // Glyph: average coverage fraction 0..1 (0 for Rule)
};

/**
 * A Graphics2D backend that records draw calls into a fixed buffer instead of
 * pushing pixels (dev-process §3). It is the assertion mechanism for draw tests
 * and the comparison surface for the MicroTeX oracle (§4).
 */
template <int Capacity = 512>
class RecordingGraphics : public Graphics2D {
 public:
  DrawOp ops[Capacity];
  int count = 0;
  bool overflow = false;

  void blendCoverage(int x, int y, int w, int h, const u8* cov) override {
    if (count >= Capacity) {
      overflow = true;
      return;
    }
    unsigned long sum = 0;
    const int n = w * h;
    for (int i = 0; i < n; ++i) sum += cov[i];
    const float fill = n > 0 ? static_cast<float>(sum) / (n * 255.0f) : 0.0f;
    ops[count++] = DrawOp{DrawOp::Glyph, static_cast<float>(x),
                          static_cast<float>(y), static_cast<float>(w),
                          static_cast<float>(h), fill};
  }
  void drawRule(float x, float top, float w, float h) override {
    if (count < Capacity) {
      ops[count++] = DrawOp{DrawOp::Rule, x, top, w, h, 0.0f};
    } else {
      overflow = true;
    }
  }
};

}  // namespace statex

#endif  // STATEX_RECORD_H
