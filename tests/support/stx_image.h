#ifndef STATEX_TEST_IMAGE_H
#define STATEX_TEST_IMAGE_H

// 8-bit grayscale image plus the small amount of I/O and geometry the
// similarity engine needs. Host/test-only: STL is fine here, unlike in
// lib/StaTeX.
//
// Convention: 0 = blank paper, 255 = full ink. That is the natural output of
// StaTeX's coverage blits, so no inversion is needed anywhere on the render
// path.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace stximg {

/** Row-major index, kept in one place so the casts are not repeated. */
inline size_t pixelIndex(int x, int y, int w) {
  return static_cast<size_t>(y) * static_cast<size_t>(w) +
         static_cast<size_t>(x);
}

struct Image {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> px;

  // Ink that fell outside the canvas and was silently dropped. Every writer
  // below clips, which is the right behaviour -- but a render canvas that is
  // one pixel too small would otherwise truncate a glyph and still score as a
  // legitimate mismatch. Callers on a render path assert this stays zero, so
  // the canvas can be sized tightly instead of padded out of superstition.
  long clipped = 0;

  Image() = default;
  Image(int width, int height)
      : w(width),
        h(height),
        px(static_cast<size_t>(width) * static_cast<size_t>(height), 0) {}

  bool empty() const { return w <= 0 || h <= 0; }
  bool inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < w && y < h;
  }

  std::uint8_t at(int x, int y) const {
    if (!inBounds(x, y)) return 0;
    return px[pixelIndex(x, y, w)];
  }
  void set(int x, int y, std::uint8_t v) {
    if (inBounds(x, y)) {
      px[pixelIndex(x, y, w)] = v;
    } else if (v != 0) {
      clipped++;
    }
  }

  /** Blend a coverage bitmap with `max` (ink never erases ink). */
  void blendMax(int x0, int y0, int cw, int ch, const std::uint8_t* cov);
  /** Fill an axis-aligned rect (pixel-center coverage, clipped). */
  void fillRect(float x, float y, float rw, float rh, std::uint8_t v = 255);
  void clear() { std::fill(px.begin(), px.end(), std::uint8_t{0}); }
};

/** Bounding box and moments of the inked pixels (value >= threshold). */
struct InkStats {
  long count = 0;      // inked pixel count
  double mass = 0.0;   // sum of coverage / 255
  double cx = 0.0;     // centroid, coverage-weighted
  double cy = 0.0;
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // inclusive bbox; empty if x1 < x0
  bool any() const { return x1 >= x0; }
  int bw() const { return any() ? x1 - x0 + 1 : 0; }
  int bh() const { return any() ? y1 - y0 + 1 : 0; }
};

InkStats inkStats(const Image& im, std::uint8_t threshold = 32);

/** Tight crop around the ink, with `pad` blank pixels on each side. */
Image cropToInk(const Image& im, int pad = 2, std::uint8_t threshold = 32);

/** Copy `src` into a `w x h` canvas at (dx, dy), clipped. */
Image placeOn(const Image& src, int w, int h, int dx, int dy);

/**
 * Binary PGM (P5) write/read: the raw, chrome-free form, for goldens and for
 * dumping a single render. Failure *pictures* meant for a human go through
 * stx_report.h instead, which writes annotated PNGs.
 */
bool writePgm(const Image& im, const std::string& path);
bool readPgm(Image& out, const std::string& path);

}  // namespace stximg

#endif  // STATEX_TEST_IMAGE_H
