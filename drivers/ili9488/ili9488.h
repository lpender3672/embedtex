#ifndef EMBEDTEX_ILI9488_H
#define EMBEDTEX_ILI9488_H

// ILI9488 320x480 display controller.
//
// Peripheral-specific, MCU-agnostic: everything here is the controller's own
// protocol, and the machine driving it arrives as a DisplayBus. Nothing in
// this file includes a vendor header.
//
// The one thing that matters and is easy to get wrong: **the ILI9488 has no
// 16-bit mode over its serial interface.** Pixel format is set to 0x66 and
// every pixel goes out as three bytes with the six significant bits
// left-aligned. Sending RGB565 gets you a scrambled panel, not an error.
//
// Blits are streamed: one address window per rectangle, then w*h pixels in a
// single transaction, rather than re-sending CASET/PASET/RAMWR per pixel.

#include <stddef.h>
#include <stdint.h>

#include "display_bus.h"

namespace drivers {

class Ili9488 {
 public:
  explicit Ili9488(DisplayBus& bus) : _bus(bus) {}

  /** Reset, run the panel init sequence, leave the display on, rotation 0. */
  void begin();

  /** 0/2 portrait (320x480), 1/3 landscape (480x320). */
  void setRotation(uint8_t r);

  int width() const { return _w; }
  int height() const { return _h; }

  void fillScreen(uint32_t rgb888);
  void fillRect(int x, int y, int w, int h, uint32_t rgb888);

  /**
   * Blend an 8-bit coverage bitmap over black in `fg`, at (x, y).
   *
   * `cov` is w*h bytes, row-major, 255 = fully inked. Clipped to the panel:
   * a caller may legitimately place a glyph partly off-screen, and an address
   * window outside the panel is undefined behaviour on the controller rather
   * than a harmless no-op.
   */
  void blitCoverage(int x, int y, int w, int h, const uint8_t* cov,
                    uint32_t fg);

  /**
   * Stream a w*h block of packed 24-bit pixels, R,G,B per pixel, at (x, y).
   *
   * The panel keeps the top six bits of each byte and discards the low two, so
   * no packing is needed here. One address window then one bus write -- no
   * per-row loop and no scratch.
   *
   * This exists for a compositor that hands over a finished pixel block, which
   * neither fillRect nor blitCoverage can accept. The contract stays in the
   * panel's own terms (R, G, B); whatever byte order the caller's framebuffer
   * uses is the caller's problem.
   *
   * Unclipped: the caller owns the block's placement. Passing a rectangle that
   * leaves the panel sets an address window the controller does not define.
   */
  void blitRgb888(int x, int y, int w, int h, const uint8_t* rgb);

 private:
  void writeCommand(uint8_t c);
  void writeCommandData(uint8_t c, const uint8_t* data, size_t n);
  void setAddrWindow(int x, int y, int w, int h);

  DisplayBus& _bus;
  int _w = 320;
  int _h = 480;

  // One row of 18-bit pixels at the panel's widest, so fillRect and
  // blitCoverage can share it.
  static constexpr int kMaxRowPx = 480;
  uint8_t _row[kMaxRowPx * 3];
};

}  // namespace drivers

#endif  // EMBEDTEX_ILI9488_H
