#include "statex_rgb565.h"

namespace statex {

void Rgb565Canvas::fill(u16 colour) {
  for (int row = 0; row < _h; ++row) {
    u16* dst = _buf + row * _stride;
    for (int col = 0; col < _w; ++col) {
      dst[col] = colour;
    }
  }
}

void Rgb565Canvas::blendCoverage(int x, int y, int w, int h, const u8* cov) {
  for (int row = 0; row < h; ++row) {
    const int dy = y + row;
    if (dy < 0 || dy >= _h) {
      continue;
    }
    const u8* src = cov + row * w;
    u16* dst = _buf + dy * _stride;
    for (int col = 0; col < w; ++col) {
      const int dx = x + col;
      if (dx < 0 || dx >= _w) {
        continue;
      }
      if (src[col] >= _threshold) {
        dst[dx] = _fg;
      }
    }
  }
}

void Rgb565Canvas::drawRule(float x, float top, float w, float h) {
  // Rules are exact rectangles; round to cover the requested span and
  // never let a thin fraction bar round down to nothing.
  int x0 = static_cast<int>(x + 0.5f);
  int y0 = static_cast<int>(top + 0.5f);
  int x1 = static_cast<int>(x + w + 0.5f);
  int y1 = static_cast<int>(top + h + 0.5f);
  if (x1 <= x0) {
    x1 = x0 + 1;
  }
  if (y1 <= y0) {
    y1 = y0 + 1;
  }

  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > _w) x1 = _w;
  if (y1 > _h) y1 = _h;

  for (int dy = y0; dy < y1; ++dy) {
    u16* dst = _buf + dy * _stride;
    for (int dx = x0; dx < x1; ++dx) {
      dst[dx] = _fg;
    }
  }
}

}  // namespace statex
