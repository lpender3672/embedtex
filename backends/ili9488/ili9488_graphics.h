#ifndef EMBEDTEX_BACKEND_ILI9488_GRAPHICS_H
#define EMBEDTEX_BACKEND_ILI9488_GRAPHICS_H

// StaTeX Graphics2D over an ILI9488 (STX-API-03).
//
// A backend is the *only* kind of thing allowed to know both layers: it
// includes lib/StaTeX for the interface and drivers/ for the hardware. Neither
// of those may include this.
//
// It used to live in drivers/ili9488, which made the driver depend upward on
// the application core and declare a class inside `namespace statex` -- a
// namespace it does not own. A driver has no business knowing what StaTeX is.

#include "ili9488.h"
#include "statex_graphics.h"

namespace statex {
namespace backends {

class Ili9488Graphics : public Graphics2D {
 public:
  // `fg` is 24-bit RGB888; the driver reduces it to the panel's 18 bits.
  Ili9488Graphics(drivers::Ili9488& panel, uint32_t fg)
      : _panel(panel), _fg(fg) {}

  void blendCoverage(int x, int y, int w, int h, const u8* cov) override {
    _panel.blitCoverage(x, y, w, h, cov, _fg);
  }

  void drawRule(float x, float top, float w, float h) override {
    // A fraction rule is ~0.04 em thick, which is well under a pixel at the
    // sizes scripts live at, so it must round up to something visible rather
    // than to nothing. Width is clamped alongside it for symmetry; no real
    // rule is sub-pixel wide.
    int hh = static_cast<int>(h + 0.5f);
    int ww = static_cast<int>(w + 0.5f);
    if (hh < 1) hh = 1;
    if (ww < 1) ww = 1;
    _panel.fillRect(static_cast<int>(x), static_cast<int>(top), ww, hh, _fg);
  }

 private:
  drivers::Ili9488& _panel;
  uint32_t _fg;
};

}  // namespace backends
}  // namespace statex

#endif  // EMBEDTEX_BACKEND_ILI9488_GRAPHICS_H
