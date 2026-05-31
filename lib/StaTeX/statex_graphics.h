#ifndef STATEX_GRAPHICS_H
#define STATEX_GRAPHICS_H

#include "statex_types.h"

namespace statex {

/**
 * Abstract drawing surface (STX-API-03). The renderer emits primitives in
 * absolute pixel coordinates (y grows downward; `baseline` is the glyph
 * baseline). Concrete backends (a TFT on target, a recorder in tests) are
 * stack/static objects — never heap-allocated — so the destructor is
 * non-virtual and protected (we never delete through this base).
 */
class Graphics2D {
 public:
  /** Draw glyph `ch` with its baseline-left origin at (x, baseline), scaled. */
  virtual void drawGlyph(c32 ch, float x, float baseline, float scale) = 0;
  /** Fill a rectangle with top-left (x, top) and size (w, h). */
  virtual void drawRule(float x, float top, float w, float h) = 0;

 protected:
  ~Graphics2D() = default;
};

}  // namespace statex

#endif  // STATEX_GRAPHICS_H
