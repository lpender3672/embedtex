#ifndef STATEX_GLYPHSTORE_H
#define STATEX_GLYPHSTORE_H

#include "statex_types.h"

// Place large generated glyph tables in flash, not RAM. The Teensy XIP linker
// script routes the `.progmem` section to FLASH but copies plain `.rodata`
// into DTCM; without this the ~400 KB SDF blob would overflow RAM. Flash is
// memory-mapped, so the data is read directly. No-op on the host.
#if defined(ARDUINO)
#define STATEX_FLASH __attribute__((section(".progmem")))
#else
#define STATEX_FLASH
#endif

namespace statex {

// Face is defined in statex_types.h.

/**
 * A flash glyph record (STX-FNT-01). Generated offline (STX-FNT-04) by
 * tools/genfont. All metrics/placement are i16 fixed-point in 1/256 em.
 *
 * The SDF is an sdfW x sdfH array of 8-bit distances at `sdfOffset` in
 * glyphSdfData(): value 128 = on the contour, >128 inside, <128 outside, with
 * `glyphSdfSpread()` (1/256 em) of distance spanned per 127 encoded levels.
 * The SDF box occupies em-space rectangle (boxX, boxY)..(boxX+boxW, boxY-boxH)
 * relative to the pen origin (boxY measured up from the baseline). The sampler
 * (STX-FNT-02) maps that box to the target pixel size and thresholds for
 * coverage (STX-FNT-03).
 */
struct GlyphRecord {
  u8 face;
  u8 sdfW;
  u8 sdfH;
  // Size variant: 0 is the text-size glyph, 1 and up are successively larger
  // purpose-cut designs from cmex10. TeX enlarges a symbol by walking this
  // chain, not by scaling, because a scaled 10pt design has the wrong stroke
  // weight AND the wrong metrics -- and a big operator is positioned from its
  // own height and depth, so the metrics error becomes a placement error.
  // Occupies what was padding, so GlyphRecord is still 32 bytes.
  u8 variant;
  c32 codepoint;
  u32 sdfOffset;
  i16 advance;   // horizontal advance
  i16 bearingX;  // ink left edge, from pen
  i16 height;    // ink top, above baseline
  i16 depth;     // ink bottom, below baseline (+down)
  i16 italic;    // italic correction
  i16 boxX;      // SDF box left, from pen (+right)
  i16 boxY;      // SDF box top, above baseline (+up)
  i16 boxW;      // SDF box width
  i16 boxH;      // SDF box height
};

/**
 * Convert one of GlyphRecord's fixed-point fields to pixels.
 *
 * Every metric above is stored in 1/256 of an em, and that divisor was being
 * written out by hand at fifteen call sites across layout, the draw walk and
 * the test suites. It is the record's storage format, not a tunable, so it
 * belongs with the record -- and a format that is restated in fifteen places
 * is a format that will one day be changed in fourteen.
 */
inline constexpr float emUnits(i16 v, float emPx) {
  return static_cast<float>(v) / 256.0f * emPx;
}

/** Device-pixel size of a glyph's coverage bitmap. */
struct GlyphBox {
  int w;
  int h;
};

/**
 * The size the sampler will produce for this glyph at `emPx`, without
 * sampling anything.
 *
 * Three callers need this and must agree exactly: the sampler itself, layout's
 * pre-flight budget check (an oversized glyph must be refused before the draw
 * walk has put anything on the panel), and any font backend that has to report
 * a bitmap box before rendering -- which is how every glyph engine works, from
 * FreeType's FT_Load_Glyph to stb_truetype's GetGlyphBitmapBox. It lives here
 * for the same reason emUnits does: it is a property of the record's storage
 * format, and a formula restated in three places is one that will one day be
 * changed in two.
 *
 * The arithmetic is in float on purpose. `boxW * emPx / 256` in integers
 * truncates before the rounding term can apply, which cost every glyph up to a
 * pixel of width and height, worst at the small sizes scripts live at. This
 * runs once per glyph, never per pixel.
 */
inline GlyphBox glyphCoverageSize(const GlyphRecord& g, float emPx) {
  const float scale = emPx / 256.0f;
  return GlyphBox{
      static_cast<int>(static_cast<float>(g.boxW) * scale + 0.5f),
      static_cast<int>(static_cast<float>(g.boxH) * scale + 0.5f)};
}

/**
 * Variants at or above this are not size steps: they are the pieces of an
 * extensible recipe (top, repeat, bottom), which TeX stacks to build a
 * delimiter taller than any single glyph. They must be excluded from any
 * "smallest that fits" walk -- a repeat tile is 0.6 em and would be chosen in
 * preference to the 2.4 em bracket it is meant to extend.
 */
constexpr u8 kFirstPieceVariant = 8;
constexpr u8 kPieceTop = 8;
constexpr u8 kPieceRepeat = 9;
constexpr u8 kPieceBottom = 10;

/** Look up the text-size glyph by face+codepoint, or nullptr (STX-LNG-02). */
const GlyphRecord* findGlyphRecord(Face face, c32 cp);

/**
 * One exact size variant, or nullptr if the chain has no such entry.
 *
 * The draw walk must fetch the variant LAYOUT chose, not "the biggest one".
 * Getting that wrong is invisible to a position comparison -- the pen position
 * is computed from the measured record and stays right -- but draws a
 * different glyph, which is how a 3em surd ended up under a 1.2em radicand.
 */
const GlyphRecord* findGlyphVariant(Face face, c32 cp, u8 variant);

/**
 * The largest size variant of a glyph, or the text-size one when the atlas
 * carries no variants for it. TeX uses the larger design for a big operator in
 * display style; falling back to the base is what should happen for a symbol
 * that has no display cut, and is not an error.
 */
const GlyphRecord* findLargestGlyphVariant(Face face, c32 cp);

/**
 * The smallest size variant whose height+depth reaches `minTotalEm` (in 1/256
 * em), or the largest available when none does.
 *
 * This is TeX's rule for growing a radical or a delimiter: walk the chain of
 * purpose-cut designs and take the first that is big enough, rather than
 * scaling one design up. Returning the largest on overflow is deliberate --
 * TeX would assemble an extensible recipe at that point, which StaTeX does not
 * do yet, and the biggest real glyph is a better answer than a distorted one.
 */
const GlyphRecord* findGlyphVariantAtLeast(Face face, c32 cp, i16 minTotalEm);

/** The shared SDF byte blob; index with GlyphRecord::sdfOffset. */
const u8* glyphSdfData();

/** Distance (1/256 em) spanned per 127 encoded SDF levels. */
int glyphSdfSpread();

/** Em pixel size the SDFs were authored at (for reference/QA). */
int glyphEmPx();

int glyphRecordCount();

}  // namespace statex

#endif  // STATEX_GLYPHSTORE_H
