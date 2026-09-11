#include "statex_a8.h"

namespace statex {
namespace backends {

void CoverageGraphics::clear() {
  for (int y = 0; y < _h; ++y) {
    u8* row = _buf + static_cast<long>(y) * _stride;
    for (int x = 0; x < _w; ++x) row[x] = 0;
  }
  _clipped = 0;
}

void CoverageGraphics::blendCoverage(int x, int y, int w, int h,
                                     const u8* cov) {
  if (w <= 0 || h <= 0 || cov == nullptr) return;

  // Clip, remembering where in the source the visible part starts. Layout may
  // legitimately place a glyph partly outside the grid, exactly as
  // Ili9488::blitCoverage documents.
  int sx = 0, sy = 0;
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (x0 < 0) { sx = -x0; x0 = 0; }
  if (y0 < 0) { sy = -y0; y0 = 0; }
  if (x1 > _w) x1 = _w;
  if (y1 > _h) y1 = _h;
  if (x0 >= x1 || y0 >= y1) {
    for (int i = 0, n = w * h; i < n; ++i) {
      if (cov[i] != 0) _clipped++;
    }
    return;
  }

  for (int j = 0; j < h; ++j) {
    const int dy = y + j;
    const u8* src = cov + static_cast<long>(j) * w;
    if (dy < y0 || dy >= y1) {
      for (int i = 0; i < w; ++i) {
        if (src[i] != 0) _clipped++;
      }
      continue;
    }
    u8* dst = _buf + static_cast<long>(dy) * _stride;
    for (int i = 0; i < w; ++i) {
      const int dx = x + i;
      if (dx < x0 || dx >= x1) {
        if (src[i] != 0) _clipped++;
        continue;
      }
      // MAX, not overwrite and not saturating add.
      //
      // Glyph boxes overlap -- an integral against its limits, italic kerning,
      // a radical's overbar meeting its surd. Writing straight to a panel hid
      // this, because a later blit simply overwrote an earlier one on the
      // wire. Into an accumulator, overwrite would erase ink that a previous
      // glyph put down, and saturating add would darken every crossing into a
      // visible blob. max is idempotent and does neither.
      if (src[i] > dst[dx]) dst[dx] = src[i];
    }
  }
  (void)sx;
  (void)sy;
}

void CoverageGraphics::drawRule(float x, float top, float w, float h) {
  if (w <= 0.0f || h <= 0.0f) return;

  // Round the two edges independently rather than rounding the width. This
  // matches stximg::Image::fillRect, which is what the host suites and the
  // MicroTeX oracle both rasterise rules through, so an A8 render is directly
  // comparable with an ImageGraphics one.
  //
  // It is also simply the correct convention: rounding the width instead makes
  // the drawn extent depend on where the rule starts. The ILI9488 backend
  // still rounds the other way and truncates its origin, which is the
  // half-pixel discrepancy recorded at the end of docs/StaTeX-lvgl-plan.md.
  int ix0 = static_cast<int>(x + 0.5f);
  int iy0 = static_cast<int>(top + 0.5f);
  int ix1 = static_cast<int>(x + w + 0.5f);
  int iy1 = static_cast<int>(top + h + 0.5f);

  // A fraction rule is ~0.04 em thick, well under a pixel at the sizes scripts
  // live at, so it must round up to something visible rather than to nothing.
  if (ix1 <= ix0) ix1 = ix0 + 1;
  if (iy1 <= iy0) iy1 = iy0 + 1;

  for (int yy = iy0; yy < iy1; ++yy) {
    if (yy < 0 || yy >= _h) {
      _clipped += ix1 - ix0;
      continue;
    }
    u8* dst = _buf + static_cast<long>(yy) * _stride;
    for (int xx = ix0; xx < ix1; ++xx) {
      if (xx < 0 || xx >= _w) {
        _clipped++;
        continue;
      }
      dst[xx] = 255;
    }
  }
}

}  // namespace backends
}  // namespace statex
