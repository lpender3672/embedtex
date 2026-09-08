#include "ili9488.h"

namespace drivers {
namespace {

// Command set (ILI9488 datasheet).
constexpr uint8_t CMD_SLPOUT = 0x11;
constexpr uint8_t CMD_DISPON = 0x29;
constexpr uint8_t CMD_CASET = 0x2A;
constexpr uint8_t CMD_PASET = 0x2B;
constexpr uint8_t CMD_RAMWR = 0x2C;
constexpr uint8_t CMD_MADCTL = 0x36;
constexpr uint8_t CMD_PIXFMT = 0x3A;

// Memory Access Control bits.
constexpr uint8_t MAD_MY = 0x80;
constexpr uint8_t MAD_MX = 0x40;
constexpr uint8_t MAD_MV = 0x20;
constexpr uint8_t MAD_BGR = 0x08;

constexpr int PANEL_W = 320;  // native, before rotation
constexpr int PANEL_H = 480;

}  // namespace

void Ili9488::writeCommand(uint8_t c) {
  _bus.setCommandMode(true);
  _bus.write(&c, 1);
  _bus.setCommandMode(false);
}

void Ili9488::writeCommandData(uint8_t c, const uint8_t* data, size_t n) {
  _bus.beginTransaction();
  writeCommand(c);
  if (n != 0) _bus.write(data, n);
  _bus.endTransaction();
}

void Ili9488::begin() {
  _bus.hardReset();

  // Panel bring-up. Transcribed from TFT_eSPI's ILI9488_Init.h, which is the
  // sequence this hardware was known to work with; the gamma and power values
  // are the vendor's reference numbers and are not worth deriving afresh.
  static const uint8_t gammaPos[] = {0x00, 0x03, 0x09, 0x08, 0x16,
                                     0x0A, 0x3F, 0x78, 0x4C, 0x09,
                                     0x0A, 0x08, 0x16, 0x1A, 0x0F};
  static const uint8_t gammaNeg[] = {0x00, 0x16, 0x19, 0x03, 0x0F,
                                     0x05, 0x32, 0x45, 0x46, 0x04,
                                     0x0E, 0x0D, 0x35, 0x37, 0x0F};
  static const uint8_t power1[] = {0x17, 0x15};
  static const uint8_t power2[] = {0x41};
  static const uint8_t vcom[] = {0x00, 0x12, 0x80};
  static const uint8_t ifmode[] = {0x00};
  static const uint8_t framerate[] = {0xA0};
  static const uint8_t invctl[] = {0x02};
  static const uint8_t dispfn[] = {0x02, 0x02, 0x3B};
  static const uint8_t entry[] = {0xC6};
  static const uint8_t adj3[] = {0xA9, 0x51, 0x2C, 0x82};

  writeCommandData(0xE0, gammaPos, sizeof(gammaPos));
  writeCommandData(0xE1, gammaNeg, sizeof(gammaNeg));
  writeCommandData(0xC0, power1, sizeof(power1));
  writeCommandData(0xC1, power2, sizeof(power2));
  writeCommandData(0xC5, vcom, sizeof(vcom));

  const uint8_t madctl = MAD_MX | MAD_BGR;
  writeCommandData(CMD_MADCTL, &madctl, 1);

  // 18-bit colour. The serial interface has no 16-bit mode; see the header.
  const uint8_t pixfmt = 0x66;
  writeCommandData(CMD_PIXFMT, &pixfmt, 1);

  writeCommandData(0xB0, ifmode, sizeof(ifmode));
  writeCommandData(0xB1, framerate, sizeof(framerate));
  writeCommandData(0xB4, invctl, sizeof(invctl));
  writeCommandData(0xB6, dispfn, sizeof(dispfn));
  writeCommandData(0xB7, entry, sizeof(entry));
  writeCommandData(0xF7, adj3, sizeof(adj3));

  writeCommandData(CMD_SLPOUT, nullptr, 0);
  _bus.delayMs(120);
  writeCommandData(CMD_DISPON, nullptr, 0);
  _bus.delayMs(25);

  setRotation(0);
}

void Ili9488::setRotation(uint8_t r) {
  uint8_t m;
  switch (r & 3) {
    case 1:
      m = MAD_MV | MAD_BGR;
      _w = PANEL_H;
      _h = PANEL_W;
      break;
    case 2:
      m = MAD_MY | MAD_BGR;
      _w = PANEL_W;
      _h = PANEL_H;
      break;
    case 3:
      m = MAD_MX | MAD_MY | MAD_MV | MAD_BGR;
      _w = PANEL_H;
      _h = PANEL_W;
      break;
    default:
      m = MAD_MX | MAD_BGR;
      _w = PANEL_W;
      _h = PANEL_H;
      break;
  }
  writeCommandData(CMD_MADCTL, &m, 1);
}

void Ili9488::setAddrWindow(int x, int y, int w, int h) {
  const uint16_t x1 = static_cast<uint16_t>(x);
  const uint16_t x2 = static_cast<uint16_t>(x + w - 1);
  const uint16_t y1 = static_cast<uint16_t>(y);
  const uint16_t y2 = static_cast<uint16_t>(y + h - 1);
  const uint8_t col[4] = {static_cast<uint8_t>(x1 >> 8),
                          static_cast<uint8_t>(x1 & 0xFF),
                          static_cast<uint8_t>(x2 >> 8),
                          static_cast<uint8_t>(x2 & 0xFF)};
  const uint8_t page[4] = {static_cast<uint8_t>(y1 >> 8),
                           static_cast<uint8_t>(y1 & 0xFF),
                           static_cast<uint8_t>(y2 >> 8),
                           static_cast<uint8_t>(y2 & 0xFF)};
  writeCommand(CMD_CASET);
  _bus.write(col, 4);
  writeCommand(CMD_PASET);
  _bus.write(page, 4);
  writeCommand(CMD_RAMWR);
}

void Ili9488::fillRect(int x, int y, int w, int h, uint32_t rgb888) {
  if (w <= 0 || h <= 0) return;
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > _w) x1 = _w;
  if (y1 > _h) y1 = _h;
  if (x0 >= x1 || y0 >= y1) return;
  const int cw = x1 - x0, ch = y1 - y0;

  const uint8_t r = static_cast<uint8_t>((rgb888 >> 16) & 0xFF);
  const uint8_t g = static_cast<uint8_t>((rgb888 >> 8) & 0xFF);
  const uint8_t b = static_cast<uint8_t>(rgb888 & 0xFF);
  for (int i = 0; i < cw; ++i) {
    _row[i * 3 + 0] = r;
    _row[i * 3 + 1] = g;
    _row[i * 3 + 2] = b;
  }

  _bus.beginTransaction();
  setAddrWindow(x0, y0, cw, ch);
  for (int j = 0; j < ch; ++j) {
    _bus.write(_row, static_cast<size_t>(cw) * 3);
  }
  _bus.endTransaction();
}

void Ili9488::fillScreen(uint32_t rgb888) { fillRect(0, 0, _w, _h, rgb888); }

void Ili9488::blitCoverage(int x, int y, int w, int h, const uint8_t* cov,
                           uint32_t fg) {
  if (w <= 0 || h <= 0 || cov == nullptr) return;

  // Clip, remembering where in the source the visible part starts.
  int sx = 0, sy = 0;
  int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  if (x0 < 0) { sx = -x0; x0 = 0; }
  if (y0 < 0) { sy = -y0; y0 = 0; }
  if (x1 > _w) x1 = _w;
  if (y1 > _h) y1 = _h;
  if (x0 >= x1 || y0 >= y1) return;
  const int cw = x1 - x0, ch = y1 - y0;

  const uint32_t fr = (fg >> 16) & 0xFF;
  const uint32_t fgc = (fg >> 8) & 0xFF;
  const uint32_t fb = fg & 0xFF;

  _bus.beginTransaction();
  setAddrWindow(x0, y0, cw, ch);
  for (int j = 0; j < ch; ++j) {
    const uint8_t* src = cov + static_cast<size_t>(sy + j) * w + sx;
    for (int i = 0; i < cw; ++i) {
      // Coverage is alpha over a black ground: c = fg * a / 255.
      const uint32_t a = src[i];
      _row[i * 3 + 0] = static_cast<uint8_t>((fr * a) / 255);
      _row[i * 3 + 1] = static_cast<uint8_t>((fgc * a) / 255);
      _row[i * 3 + 2] = static_cast<uint8_t>((fb * a) / 255);
    }
    _bus.write(_row, static_cast<size_t>(cw) * 3);
  }
  _bus.endTransaction();
}

}  // namespace drivers
