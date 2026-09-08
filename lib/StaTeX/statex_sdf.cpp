#include "statex_sdf.h"

namespace statex {
namespace {

// The decode below relies on `>>` of a negative value being an arithmetic
// (floor) shift. That is guaranteed from C++20 and universal in practice
// before it; assert rather than assume, since the whole rounding argument in
// renderGlyphCoverage rests on it.
static_assert((-1 >> 1) == -1, "arithmetic right shift required");

/**
 * Largest decode slope (see below) the Q16 product can carry without
 * overflowing i32. |encQ8 - 32768| <= 32768, so the product stays inside
 * 2^31 while `slopeQ16` <= 2^31 / 2^15 = 65536. That bound is reached at
 * emPx ~= 1020 for the atlas's 0.125 em spread -- far above any size the
 * coverage budget (kMaxGlyphCoveragePx) admits, so tripping it means the
 * caller asked for an absurd em rather than that the atlas grew.
 */
constexpr i32 kMaxSlopeQ16 = 65536;

}  // namespace

bool renderGlyphCoverage(const GlyphRecord& g, const u8* sdfBase, int spread256,
                         int emPx, u8* out, int cap, GlyphCoverage* cov) {
  // Output box size in device pixels = SDF box (em) * emPx.
  //
  // The division is in floating point on purpose: `g.boxW * emPx / 256` in
  // integers truncates before the rounding term can apply, which cost every
  // glyph up to a pixel of width and height, worst at the small sizes scripts
  // live at. This is once per glyph, not once per pixel, so it is not on the
  // path the rest of this function is written around.
  const float emScale = static_cast<float>(emPx) / 256.0f;
  const int w = static_cast<int>(static_cast<float>(g.boxW) * emScale + 0.5f);
  const int h = static_cast<int>(static_cast<float>(g.boxH) * emScale + 0.5f);
  cov->w = w;
  cov->h = h;
  if (w <= 0 || h <= 0) return true;  // nothing to draw
  if (w * h > cap) return false;      // refuse oversized glyph (STX-MEM-03)

  const u8* sdf = sdfBase + g.sdfOffset;
  const int sw = g.sdfW;
  const int sh = g.sdfH;

  // --- Decode slope -------------------------------------------------------
  //
  // The per-pixel float chain this replaces was
  //
  //     distEm = (enc - 128) / 127 * (spread256 / 256)
  //     out    = round(255 * clamp01(0.5 + distEm * emPx))
  //
  // which is *affine in `enc`*: everything between the subtraction and the
  // clamp is a constant of the glyph and the size, not of the pixel. Folding
  // it into one slope removes both the divide by 127 and the whole float
  // round-trip from the inner loop -- on Cortex-M33 that divide alone is a
  // 14-cycle, non-pipelined vdiv.f32 executed once per output pixel.
  //
  // Writing E for the exact bilinear result in [0,255]:
  //
  //     out = round(127.5 + (E - 128) * slope),  slope = 255*spreadEm*emPx/127
  //
  // and round(127.5 + X) == floor(128 + X), which is what `128 + (prod >> 16)`
  // computes directly given an arithmetic shift. `enc` arrives in Q8, so the
  // stored slope carries an extra factor of 256 to cancel it.
  const float spreadEm = static_cast<float>(spread256) / 256.0f;
  const float slope = 255.0f * spreadEm * static_cast<float>(emPx) / 127.0f;
  const i32 slopeQ16 = static_cast<i32>(slope * 256.0f + 0.5f);
  if (slopeQ16 > kMaxSlopeQ16) return false;  // absurd em: refuse (STX-MEM-03)

  // --- Texel-space DDA ----------------------------------------------------
  //
  // `u` advances by a constant step per output pixel, so the per-pixel
  // `(px + 0.5f) / w` becomes an accumulator. Q16 throughout: the step is
  // computed once in 64-bit, the walk is 32-bit.
  const i32 du = static_cast<i32>((static_cast<std::int64_t>(sw) << 16) / w);
  const i32 dv = static_cast<i32>((static_cast<std::int64_t>(sh) << 16) / h);
  const i32 u0 = (du >> 1) - 32768;  // pixel centre 0.5 mapped in, minus 0.5
  const i32 v0 = (dv >> 1) - 32768;

  i32 v = v0;
  for (int py = 0; py < h; ++py, v += dv) {
    // Edge-clamp in v, then split into texel index and Q16 weight.
    i32 vy = v < 0 ? 0 : v;
    int y0 = vy >> 16;
    if (y0 > sh - 1) {
      y0 = sh - 1;
      vy = y0 << 16;
    }
    const int y1 = (y0 + 1 < sh) ? y0 + 1 : y0;
    const i32 ay = vy & 0xFFFF;
    // Row bases hoisted: the float version recomputed `y0 * w` per pixel.
    const u8* r0 = sdf + y0 * sw;
    const u8* r1 = sdf + y1 * sw;
    u8* dst = out + py * w;

    i32 u = u0;
    for (int px = 0; px < w; ++px, u += du) {
      i32 ux = u < 0 ? 0 : u;
      int x0 = ux >> 16;
      if (x0 > sw - 1) {
        x0 = sw - 1;
        ux = x0 << 16;
      }
      const int x1 = (x0 + 1 < sw) ? x0 + 1 : x0;
      const i32 ax = ux & 0xFFFF;

      const i32 s00 = r0[x0], s10 = r0[x1];
      const i32 s01 = r1[x0], s11 = r1[x1];
      // Lerp in x at Q16, then in y. The y step needs 64 bits: the row
      // difference is up to 255<<16 (25 bits) and the weight is 16, so the
      // product does not fit i32. It is 4 extra instructions on Cortex-M33
      // and buys exactness; a Q4 row cache would remove it if the inner loop
      // ever needs to get tighter.
      const i32 top = (s00 << 16) + (s10 - s00) * ax;
      const i32 bot = (s01 << 16) + (s11 - s01) * ax;
      const i32 encQ8 =
          (top + static_cast<i32>((static_cast<std::int64_t>(bot - top) * ay) >>
                                  16)) >>
          8;

      i32 c = 128 + (((encQ8 - 32768) * slopeQ16) >> 16);
      c = c < 0 ? 0 : (c > 255 ? 255 : c);
      dst[px] = static_cast<u8>(c);
    }
  }
  return true;
}

}  // namespace statex
