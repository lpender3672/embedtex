#ifndef STATEX_RGB565_H
#define STATEX_RGB565_H

#include "statex_graphics.h"

namespace statex {

/**
 * Graphics2D over a caller-owned RGB565 (native-endian) pixel buffer.
 *
 * Coverage is thresholded to on/off rather than alpha-blended: this
 * backend targets palette-constrained displays (memory-in-pixel panels)
 * where anti-aliased greys do not exist. Pixels at or above `threshold`
 * become the foreground colour; everything else is left untouched, so
 * the caller controls the background by pre-filling the buffer.
 *
 * Knows nothing about any UI toolkit; it is a byte grid like statex_a8,
 * just RGB565-shaped.
 */
class Rgb565Canvas : public Graphics2D {
 public:
  Rgb565Canvas(u16* buf, int w, int h, int stridePx, u16 fg,
               u8 threshold = 128)
      : _buf(buf),
        _w(w),
        _h(h),
        _stride(stridePx),
        _fg(fg),
        _threshold(threshold) {}

  /** Fill the whole buffer (background). */
  void fill(u16 colour);

  void blendCoverage(int x, int y, int w, int h, const u8* cov) override;
  void drawRule(float x, float top, float w, float h) override;

  /** 0xRRGGBB -> RGB565. Lossless for palette colours. */
  static constexpr u16 fromRgb888(u32 rgb) {
    return static_cast<u16>((((rgb >> 16) & 0xFF) >> 3) << 11 |
                            (((rgb >> 8) & 0xFF) >> 2) << 5 |
                            ((rgb & 0xFF) >> 3));
  }

 private:
  u16* _buf;
  int _w;
  int _h;
  int _stride;
  u16 _fg;
  u8 _threshold;
};

}  // namespace statex

#endif  // STATEX_RGB565_H
