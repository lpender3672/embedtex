#ifndef EMBEDTEX_STATEX_A8_H
#define EMBEDTEX_STATEX_A8_H

// StaTeX Graphics2D writing 8-bit coverage into a caller-supplied byte grid.
//
// This is the shape LVGL wants: an A8 buffer, which LV_COLOR_FORMAT_A8 tints
// with style.img_recolor. But nothing here knows about LVGL -- it is a
// `uint8_t*` and a stride -- so it is host-testable with no LVGL in the build,
// and reusable by anything else that wants coverage rather than pixels.
//
// Colour is deliberately absent. Coverage is alpha; whoever composites it
// decides what colour it is (see docs/StaTeX-app-architecture.md).

#include "statex_graphics.h"
#include "statex_types.h"

namespace statex {
namespace backends {

class CoverageGraphics : public Graphics2D {
 public:
  /**
   * @param buf     w*h (at least) bytes, caller-owned, NOT cleared here
   * @param w,h     grid size in pixels
   * @param stride  bytes per row; equals `w` for a dense buffer
   */
  CoverageGraphics(u8* buf, int w, int h, int stride)
      : _buf(buf), _w(w), _h(h), _stride(stride) {}

  /** Set the whole grid to zero. Call before the first render into it. */
  void clear();

  void blendCoverage(int x, int y, int w, int h, const u8* cov) override;
  void drawRule(float x, float top, float w, float h) override;

  /** Pixels the caller's grid was too small to hold. Should be 0. */
  long clipped() const { return _clipped; }

 private:
  u8* _buf;
  int _w;
  int _h;
  int _stride;
  long _clipped = 0;
};

}  // namespace backends
}  // namespace statex

#endif  // EMBEDTEX_STATEX_A8_H
