#ifndef STATEX_TFT_H
#define STATEX_TFT_H

// Concrete TFT-backed Graphics2D (Phase 8). Compiled only on the Arduino
// target. Blends StaTeX-supplied glyph coverage (STX-FNT-02/03) and fills
// rules — no SDF sampling here, no heap. Pixels can't be unit-tested on host;
// the coverage it receives is covered by the host draw tests.
#if defined(ARDUINO)

#include <TFT_eSPI.h>

#include "statex_graphics.h"

namespace statex {

class TftGraphics : public Graphics2D {
 public:
  // fg is a 16-bit 565 colour; coverage is alpha-blended over a black ground.
  TftGraphics(TFT_eSPI& tft, uint16_t fg) : _tft(tft), _fg(fg) {}

  void blendCoverage(int x, int y, int w, int h, const u8* cov) override {
    const uint8_t fr = (_fg >> 11) & 0x1F;
    const uint8_t fg6 = (_fg >> 5) & 0x3F;
    const uint8_t fb = _fg & 0x1F;
    for (int j = 0; j < h; ++j) {
      for (int i = 0; i < w; ++i) {
        const uint8_t a = cov[j * w + i];
        if (a == 0) continue;
        // Alpha-blend toward fg over black: c = fg * a/255.
        const uint16_t r = (fr * a) / 255;
        const uint16_t g = (fg6 * a) / 255;
        const uint16_t b = (fb * a) / 255;
        const uint16_t c = (r << 11) | (g << 5) | b;
        _tft.drawPixel(x + i, y + j, c);
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
};

}  // namespace statex

#endif  // ARDUINO
#endif  // STATEX_TFT_H
