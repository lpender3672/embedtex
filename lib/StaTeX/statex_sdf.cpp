#include "statex_sdf.h"

namespace statex {
namespace {

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Bilinear sample of the encoded SDF (values 0..255) at texel coords (u,v),
// edge-clamped.
float sampleSdf(const u8* base, int w, int h, float u, float v) {
  if (u < 0) u = 0;
  if (v < 0) v = 0;
  const float fu = u < (w - 1) ? u : static_cast<float>(w - 1);
  const float fv = v < (h - 1) ? v : static_cast<float>(h - 1);
  const int x0 = static_cast<int>(fu);
  const int y0 = static_cast<int>(fv);
  const int x1 = x0 + 1 < w ? x0 + 1 : x0;
  const int y1 = y0 + 1 < h ? y0 + 1 : y0;
  const float ax = fu - x0;
  const float ay = fv - y0;
  const float s00 = base[y0 * w + x0];
  const float s10 = base[y0 * w + x1];
  const float s01 = base[y1 * w + x0];
  const float s11 = base[y1 * w + x1];
  const float top = s00 + (s10 - s00) * ax;
  const float bot = s01 + (s11 - s01) * ax;
  return top + (bot - top) * ay;
}

}  // namespace

bool renderGlyphCoverage(const GlyphRecord& g, const u8* sdfBase, int spread256,
                         int emPx, u8* out, int cap, GlyphCoverage* cov) {
  // Output box size in device pixels = SDF box (em) * emPx.
  const int w = static_cast<int>((g.boxW * emPx) / 256 + 0.5f);
  const int h = static_cast<int>((g.boxH * emPx) / 256 + 0.5f);
  cov->w = w;
  cov->h = h;
  if (w <= 0 || h <= 0) return true;  // nothing to draw
  if (w * h > cap) return false;      // refuse oversized glyph (STX-MEM-03)

  const u8* sdf = sdfBase + g.sdfOffset;
  const float spreadEm = static_cast<float>(spread256) / 256.0f;

  for (int py = 0; py < h; ++py) {
    // Map output pixel center to SDF texel space.
    const float v = (py + 0.5f) / h * g.sdfH - 0.5f;
    for (int px = 0; px < w; ++px) {
      const float u = (px + 0.5f) / w * g.sdfW - 0.5f;
      const float enc = sampleSdf(sdf, g.sdfW, g.sdfH, u, v);
      // Decode to signed distance (em), positive inside.
      const float distEm = (enc - 128.0f) / 127.0f * spreadEm;
      const float distPx = distEm * emPx;
      // ~1px antialiasing ramp about the contour.
      const float c = clamp01(0.5f + distPx);
      out[py * w + px] = static_cast<u8>(c * 255.0f + 0.5f);
    }
  }
  return true;
}

}  // namespace statex
