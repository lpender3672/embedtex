#ifndef STATEX_ATLAS_H
#define STATEX_ATLAS_H

#include "statex_types.h"

namespace statex {

/**
 * A pre-rasterized glyph in the flash atlas (STX-RES-03). Bitmaps and metrics
 * are `constexpr` data linked into flash; there is no runtime font parsing or
 * file I/O on the render path (replaces the SD/OpenFontRender path in
 * graphic_tft.cpp). A real build would generate this table offline from a font.
 */
struct GlyphInfo {
  c32 codepoint;
  u8 width;       // bitmap columns
  u8 height;      // bitmap rows
  u8 advance;     // horizontal advance in px at the reference size
  u16 rowOffset;  // index of the first row byte in the shared bitmap array
};

/** Reference EM pixel size the atlas bitmaps were rasterized at. */
constexpr int kAtlasEmPx = 7;

/** Find a glyph by codepoint, or nullptr if not in the atlas. */
const GlyphInfo* findGlyph(c32 cp);

/** Test whether pixel (x,y) of glyph `g` is set (0 <= x < width, 0 <= y < height). */
bool glyphPixel(const GlyphInfo& g, int x, int y);

int glyphCount();

}  // namespace statex

#endif  // STATEX_ATLAS_H
