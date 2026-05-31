#include "statex_atlas.h"

namespace statex {
namespace {

// 5x7 bitmaps, one byte per row (bit (width-1) = leftmost column). This is a
// hand-authored seed set demonstrating the flash-atlas mechanism; a production
// atlas is generated offline from the TeX fonts. Rows are concatenated in
// codepoint order to match kGlyphs[].rowOffset.
constexpr u8 kRows[] = {
    // '+' (0x2B) rowOffset 0
    0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00,
    // '1' (0x31) rowOffset 7
    0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E,
    // '2' (0x32) rowOffset 14
    0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F,
    // 'x' (0x78) rowOffset 21
    0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11,
    // 'y' (0x79) rowOffset 28
    0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04,
};

// Sorted by codepoint for binary search.
constexpr GlyphInfo kGlyphs[] = {
    {0x2B, 5, 7, 6, 0},
    {0x31, 5, 7, 6, 7},
    {0x32, 5, 7, 6, 14},
    {0x78, 5, 7, 6, 21},
    {0x79, 5, 7, 6, 28},
};

constexpr int kCount = static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));

constexpr bool sortedByCp(const GlyphInfo* a, int n) {
  for (int i = 1; i < n; ++i) {
    if (a[i - 1].codepoint >= a[i].codepoint) return false;
  }
  return true;
}
static_assert(sortedByCp(kGlyphs, kCount),
              "kGlyphs must be sorted by codepoint");

}  // namespace

const GlyphInfo* findGlyph(c32 cp) {
  int lo = 0;
  int hi = kCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const c32 m = kGlyphs[mid].codepoint;
    if (m == cp) return &kGlyphs[mid];
    if (m < cp) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

bool glyphPixel(const GlyphInfo& g, int x, int y) {
  if (x < 0 || y < 0 || x >= g.width || y >= g.height) return false;
  const u8 rowBits = kRows[g.rowOffset + y];
  return ((rowBits >> (g.width - 1 - x)) & 0x1u) != 0;
}

int glyphCount() { return kCount; }

}  // namespace statex
