#ifndef STATEX_RECORD_H
#define STATEX_RECORD_H

#include "statex_graphics.h"

namespace statex {

/** One recorded draw primitive (test/oracle fixture). */
struct DrawOp {
  enum Kind : u8 { Glyph, Rule } kind;
  c32 ch;
  float x;
  float y;  // glyph baseline, or rule top
  float w;
  float h;
  float scale;
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

  void drawGlyph(c32 ch, float x, float baseline, float scale) override {
    if (count < Capacity) {
      ops[count++] = DrawOp{DrawOp::Glyph, ch, x, baseline, 0, 0, scale};
    } else {
      overflow = true;
    }
  }
  void drawRule(float x, float top, float w, float h) override {
    if (count < Capacity) {
      ops[count++] = DrawOp{DrawOp::Rule, 0, x, top, w, h, 1.0f};
    } else {
      overflow = true;
    }
  }
};

}  // namespace statex

#endif  // STATEX_RECORD_H
