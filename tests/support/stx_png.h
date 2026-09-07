#ifndef STATEX_TEST_PNG_H
#define STATEX_TEST_PNG_H

// A self-contained PNG writer, so failure pictures open in anything.
//
// PGM was the wrong format for artefacts a human is meant to look at: most
// viewers will not open it. This writes 8-bit RGB PNG with no external
// dependency by using deflate *stored* blocks -- the output is uncompressed
// and therefore larger than a real encoder would produce, but these are small
// diagnostic images and the alternative is linking zlib into the test tree.

#include <cstdint>
#include <string>
#include <vector>

namespace stximg {

/** Byte offset of pixel (x, y) in a 3-bytes-per-pixel buffer. */
inline size_t rgbIndex(int x, int y, int w) {
  return (static_cast<size_t>(y) * static_cast<size_t>(w) +
          static_cast<size_t>(x)) *
         3;
}

/** 8-bit RGB image, 3 bytes per pixel, row-major. */
struct RgbImage {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> px;

  RgbImage() = default;
  RgbImage(int width, int height, std::uint8_t fill = 0)
      : w(width),
        h(height),
        px(static_cast<size_t>(width) * static_cast<size_t>(height) * 3,
           fill) {}

  void set(int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const size_t i = rgbIndex(x, y, w);
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
  }
  void fillRow(int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (int x = 0; x < w; ++x) set(x, y, r, g, b);
  }
};

/** Write `im` as an 8-bit RGB PNG. Returns false on I/O failure. */
bool writePng(const RgbImage& im, const std::string& path);

}  // namespace stximg

#endif  // STATEX_TEST_PNG_H
