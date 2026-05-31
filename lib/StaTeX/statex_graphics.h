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
  /**
   * Blend an 8-bit coverage bitmap (w*h, row-major; 255 = fully inked) at
   * device pixel top-left (x, y) using the backend's current colour. StaTeX
   * produces the coverage by sampling the glyph SDF (STX-FNT-02); the backend
   * only blends — it stays heap-free and portable (STX-API-03).
   */
  virtual void blendCoverage(int x, int y, int w, int h, const u8* cov) = 0;
  /** Fill a rectangle with top-left (x, top) and size (w, h). */
  virtual void drawRule(float x, float top, float w, float h) = 0;

 protected:
  ~Graphics2D() = default;
};

}  // namespace statex

#endif  // STATEX_GRAPHICS_H
