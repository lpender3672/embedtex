#include "stx_image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace stximg {

void Image::blendMax(int x0, int y0, int cw, int ch, const std::uint8_t* cov) {
  if (cov == nullptr) return;
  for (int j = 0; j < ch; ++j) {
    const int y = y0 + j;
    for (int i = 0; i < cw; ++i) {
      const int x = x0 + i;
      const std::uint8_t v = cov[pixelIndex(i, j, cw)];
      if (y < 0 || y >= h || x < 0 || x >= w) {
        if (v != 0) clipped++;
        continue;
      }
      std::uint8_t& dst = px[pixelIndex(x, y, w)];
      if (v > dst) dst = v;
    }
  }
}

void Image::fillRect(float x, float y, float rw, float rh, std::uint8_t v) {
  if (rw <= 0.0f || rh <= 0.0f) return;
  // Cover any pixel whose centre falls inside the rect, but never drop a
  // sub-pixel-tall rule entirely: a fraction bar is often < 1px high.
  int ix0 = static_cast<int>(std::floor(x + 0.5f));
  int iy0 = static_cast<int>(std::floor(y + 0.5f));
  int ix1 = static_cast<int>(std::floor(x + rw + 0.5f));
  int iy1 = static_cast<int>(std::floor(y + rh + 0.5f));
  if (ix1 <= ix0) ix1 = ix0 + 1;
  if (iy1 <= iy0) iy1 = iy0 + 1;
  for (int yy = iy0; yy < iy1; ++yy) {
    for (int xx = ix0; xx < ix1; ++xx) {
      if (yy < 0 || yy >= h || xx < 0 || xx >= w) {
        if (v != 0) clipped++;
        continue;
      }
      std::uint8_t& dst = px[pixelIndex(xx, yy, w)];
      if (v > dst) dst = v;
    }
  }
}

InkStats inkStats(const Image& im, std::uint8_t threshold) {
  InkStats s;
  s.x0 = im.w;
  s.y0 = im.h;
  s.x1 = -1;
  s.y1 = -1;
  double sx = 0.0, sy = 0.0;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      const std::uint8_t v = im.px[pixelIndex(x, y, im.w)];
      if (v < threshold) continue;
      const double m = v / 255.0;
      s.count++;
      s.mass += m;
      sx += m * x;
      sy += m * y;
      if (x < s.x0) s.x0 = x;
      if (y < s.y0) s.y0 = y;
      if (x > s.x1) s.x1 = x;
      if (y > s.y1) s.y1 = y;
    }
  }
  if (s.mass > 0.0) {
    s.cx = sx / s.mass;
    s.cy = sy / s.mass;
  }
  if (s.x1 < s.x0) {  // no ink
    s.x0 = s.y0 = 0;
    s.x1 = s.y1 = -1;
  }
  return s;
}

Image cropToInk(const Image& im, int pad, std::uint8_t threshold) {
  const InkStats s = inkStats(im, threshold);
  if (!s.any()) return Image(1, 1);
  const int x0 = std::max(0, s.x0 - pad);
  const int y0 = std::max(0, s.y0 - pad);
  const int x1 = std::min(im.w - 1, s.x1 + pad);
  const int y1 = std::min(im.h - 1, s.y1 + pad);
  Image out(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = 0; y < out.h; ++y) {
    for (int x = 0; x < out.w; ++x) {
      out.px[pixelIndex(x, y, out.w)] = im.at(x0 + x, y0 + y);
    }
  }
  return out;
}

Image placeOn(const Image& src, int w, int h, int dx, int dy) {
  Image out(w, h);
  for (int y = 0; y < src.h; ++y) {
    const int ty = y + dy;
    if (ty < 0 || ty >= h) continue;
    for (int x = 0; x < src.w; ++x) {
      const int tx = x + dx;
      if (tx < 0 || tx >= w) continue;
      out.px[pixelIndex(tx, ty, w)] = src.px[pixelIndex(x, y, src.w)];
    }
  }
  return out;
}


bool writePgm(const Image& im, const std::string& path) {
  if (im.empty()) return false;
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P5\n%d %d\n255\n", im.w, im.h);
  const size_t n = im.px.size();
  const bool ok = std::fwrite(im.px.data(), 1, n, f) == n;
  std::fclose(f);
  return ok;
}

namespace {

// Read one PGM header token, skipping whitespace and '#' comments.
bool pgmToken(std::FILE* f, std::string& tok) {
  tok.clear();
  int c;
  for (;;) {
    c = std::fgetc(f);
    if (c == EOF) return false;
    if (c == '#') {
      while (c != '\n' && c != EOF) c = std::fgetc(f);
      continue;
    }
    if (!std::isspace(c)) break;
  }
  while (c != EOF && !std::isspace(c)) {
    tok.push_back(static_cast<char>(c));
    c = std::fgetc(f);
  }
  return !tok.empty();
}

}  // namespace

bool readPgm(Image& out, const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::string magic, sw, sh, smax;
  const bool hdr = pgmToken(f, magic) && pgmToken(f, sw) && pgmToken(f, sh) &&
                   pgmToken(f, smax);
  if (!hdr || magic != "P5") {
    std::fclose(f);
    return false;
  }
  const int w = std::atoi(sw.c_str());
  const int h = std::atoi(sh.c_str());
  if (w <= 0 || h <= 0 || std::atoi(smax.c_str()) != 255) {
    std::fclose(f);
    return false;
  }
  out = Image(w, h);
  const size_t n = out.px.size();
  const bool ok = std::fread(out.px.data(), 1, n, f) == n;
  std::fclose(f);
  return ok;
}

}  // namespace stximg
