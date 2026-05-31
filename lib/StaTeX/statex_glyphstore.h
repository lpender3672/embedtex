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

/** Look up a glyph by face+codepoint, or nullptr if absent (STX-LNG-02). */
const GlyphRecord* findGlyphRecord(Face face, c32 cp);

/** The shared SDF byte blob; index with GlyphRecord::sdfOffset. */
const u8* glyphSdfData();

/** Distance (1/256 em) spanned per 127 encoded SDF levels. */
int glyphSdfSpread();

/** Em pixel size the SDFs were authored at (for reference/QA). */
int glyphEmPx();

int glyphRecordCount();

}  // namespace statex

#endif  // STATEX_GLYPHSTORE_H
