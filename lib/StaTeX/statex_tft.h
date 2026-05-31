#ifndef STATEX_TFT_H
#define STATEX_TFT_H

// Concrete TFT-backed Graphics2D (Phase 8). Compiled only on the Arduino
// target. Serves glyphs from the flash atlas (STX-RES-03) — no SD / font-file
// access on the render path. Pixels can't be unit-tested on host; the op-list
// behavior it relies on is covered by the host draw tests.
#if defined(ARDUINO)

#include <TFT_eSPI.h>

#include "statex_atlas.h"
#include "statex_graphics.h"

namespace statex {

class TftGraphics : public Graphics2D {
 public:
  TftGraphics(TFT_eSPI& tft, uint16_t fg, float emPx)
      : _tft(tft), _fg(fg), _emPx(emPx) {}

  void drawGlyph(c32 ch, float x, float baseline, float scale) override {
    const GlyphInfo* g = findGlyph(ch);
    if (g == nullptr) return;
    // Map atlas rows to device pixels at the requested size.
    const float pxPerRow = (scale * _emPx) / static_cast<float>(kAtlasEmPx);
    int cell = static_cast<int>(pxPerRow + 0.5f);
    if (cell < 1) cell = 1;
    const float top = baseline - g->height * pxPerRow;
    for (int gy = 0; gy < g->height; ++gy) {
      for (int gx = 0; gx < g->width; ++gx) {
        if (!glyphPixel(*g, gx, gy)) continue;
        const int px = static_cast<int>(x + gx * pxPerRow);
        const int py = static_cast<int>(top + gy * pxPerRow);
        _tft.fillRect(px, py, cell, cell, _fg);
      }
    }
  }

  void drawRule(float x, float top, float w, float h) override {
    int hh = static_cast<int>(h + 0.5f);
    if (hh < 1) hh = 1;
    _tft.fillRect(static_cast<int>(x), static_cast<int>(top),
                  static_cast<int>(w + 0.5f), hh, _fg);
  }

 private:
  TFT_eSPI& _tft;
  uint16_t _fg;
  float _emPx;
};

}  // namespace statex

#endif  // ARDUINO
#endif  // STATEX_TFT_H
