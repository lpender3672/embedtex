// Every symbol in the table must actually draw something.
//
// Specified behaviour (STX-LNG-02, STX-FNT-04, STX-RES-01):
//   "A name in the closed command set resolves to a glyph and renders it."
//
// Why this suite exists as its own entry rather than as cases in the corpus:
// the corpus exercises fourteen symbol names, which were the whole command set
// when it was written. The suite now has six hundred, and the other 586 had no
// rendered coverage at all -- test_symbols proves each name finds *a* record,
// which is not the same as proving the record holds the right picture, and
// test_oracle_diff is red for unrelated layout reasons so anything added there
// would be invisible.
//
// What a failure here means, in order of likelihood:
//
//   refused        the symbol table and the atlas disagree. They are generated
//                  from one file (tools/genfont/symbols.tsv) precisely so this
//                  cannot happen, so it means a half-applied regeneration.
//   drew nothing   the record exists and points at a blank raster. Either the
//                  slot is empty in the source font, or the glyph was drawn
//                  through a path that dropped it -- HarfBuzz silently drops
//                  chr(173), U+00AD SOFT HYPHEN, which is a real cmex10 slot.
//   drew clipped   the glyph is larger than its declared box, so the sampler
//                  ran out of coverage buffer.
//
// This is a breadth check, not a fidelity one. It cannot tell a correct glyph
// from a wrong-but-inked one; genfont's advance cross-check is what guards
// that, by refusing any slot whose TFM width disagrees with its raster width
// by more than 0.20 em.

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "statex_glyphstore.h"
#include "statex_symbols.h"
#include "stx_image.h"
#include "stx_png.h"
#include "stx_render.h"
#include "stx_report.h"

using namespace stxtest;
using namespace statex;

void setUp() {}
void tearDown() {}

namespace {

// The corpus's two working sizes. Small enough that the SDF is sampled well
// below its stored resolution, large enough that a thin stroke survives -- the
// two ends where a coverage bug shows up.
const float kSizes[] = {22.0f, 36.0f};

struct Failure {
  std::string name;
  float size;
  const char* why;
  int code;
};

std::u32string commandFor(const char* name) {
  std::u32string out;
  out.push_back(U'\\');
  for (const char* p = name; *p != '\0'; ++p) {
    out.push_back(static_cast<char32_t>(static_cast<unsigned char>(*p)));
  }
  return out;
}

}  // namespace

static void test_every_symbol_renders_ink() {
  std::vector<Failure> failures;
  int rendered = 0;

  for (int i = 0; i < symbolCount(); ++i) {
    const SymbolEntry* e = symbolAt(i);
    const std::u32string tex = commandFor(e->name);

    for (float size : kSizes) {
      Canvas canvas;
      canvas.sizePx = size;
      const RenderOutput out = renderStatex(tex, canvas);

      if (!out.ok()) {
        failures.push_back({e->name, size, "refused", (int)out.error});
        continue;
      }
      if (out.coverageBlits == 0) {
        failures.push_back({e->name, size, "drew nothing", 0});
        continue;
      }
      if (out.blankBlits != 0) {
        // A blit whose coverage was entirely zero. The record exists, the
        // sampler ran, and no pixel came out -- which is what a blank source
        // slot looks like from here.
        failures.push_back({e->name, size, "blank coverage", out.blankBlits});
        continue;
      }
      ++rendered;
    }
  }

  if (!failures.empty()) {
    std::printf("  %d of %d (symbol, size) pairs failed to draw:\n",
                (int)failures.size(), symbolCount() * 2);
    int shown = 0;
    for (const Failure& f : failures) {
      if (shown++ >= 20) {
        std::printf("    ... and %d more\n", (int)failures.size() - 20);
        break;
      }
      std::printf("    %-22s @%.0fpx  %s (%d)\n", f.name.c_str(),
                  (double)f.size, f.why, f.code);
    }
  } else {
    std::printf("  %d symbols drew ink at %d sizes each\n", symbolCount(),
                (int)(sizeof(kSizes) / sizeof(kSizes[0])));
  }

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)failures.size(),
                                "symbols that did not render");
  TEST_ASSERT_EQUAL_INT(symbolCount() * 2, rendered);
}

// --- contact sheets --------------------------------------------------------
//
// The ink assertion above is a machine check: it proves something was drawn,
// not that the right thing was drawn. Six hundred glyphs cannot be asserted
// into correctness, but they can be *looked at*, and a wrong slot is obvious
// to a human the moment it sits next to its own name.
//
// So the sweep also lays every symbol out on a contact sheet. This is the same
// argument the picture test in the oracle suite is built on -- it has caught
// two defects the position test was structurally blind to -- applied to the
// part of the atlas the corpus does not reach.

namespace {

const int kCols = 10;
const int kRows = 10;
const int kCellW = 92;
const int kCellH = 76;
const int kGlyphH = 58;  // the part of a cell the glyph gets; rest is caption
const int kMargin = 8;

void blitInk(stximg::RgbImage& dst, const stximg::Image& src, int x0, int y0,
             int sx0, int sy0, int sw, int sh) {
  for (int y = 0; y < sh; ++y) {
    for (int x = 0; x < sw; ++x) {
      const int sx = sx0 + x, sy = sy0 + y;
      if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h) continue;
      const std::uint8_t v = src.px[stximg::pixelIndex(sx, sy, src.w)];
      if (v == 0) continue;
      const int dx = x0 + x, dy = y0 + y;
      if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
      std::uint8_t* p = &dst.px[stximg::rgbIndex(dx, dy, dst.w)];
      p[0] = p[1] = p[2] = v;
    }
  }
}

/** Tight ink bounds of `im`, or false when it has none. */
bool inkBounds(const stximg::Image& im, int& x0, int& y0, int& x1, int& y1) {
  x0 = im.w;
  y0 = im.h;
  x1 = -1;
  y1 = -1;
  for (int y = 0; y < im.h; ++y) {
    for (int x = 0; x < im.w; ++x) {
      if (im.px[stximg::pixelIndex(x, y, im.w)] == 0) continue;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  return x1 >= x0 && y1 >= y0;
}

}  // namespace

static void test_contact_sheets_are_written() {
  clearArtifacts("symbols_");

  const int perSheet = kCols * kRows;
  const int sheets = (symbolCount() + perSheet - 1) / perSheet;
  int drawn = 0;

  for (int s = 0; s < sheets; ++s) {
    stximg::RgbImage sheet;
    sheet.w = kCols * kCellW + 2 * kMargin;
    sheet.h = kRows * kCellH + 2 * kMargin + 18;
    sheet.px.assign(static_cast<size_t>(sheet.w) * sheet.h * 3, 20);

    char title[96];
    std::snprintf(title, sizeof(title), "statex symbols %d-%d of %d  (36px em)",
                  s * perSheet + 1,
                  std::min((s + 1) * perSheet, symbolCount()), symbolCount());
    drawCaption(sheet, kMargin, kMargin + 10, title, 11, 200, 200, 210);

    for (int k = 0; k < perSheet; ++k) {
      const int idx = s * perSheet + k;
      if (idx >= symbolCount()) break;
      const SymbolEntry* e = symbolAt(idx);

      Canvas canvas;
      canvas.width = 220;
      canvas.height = 190;
      canvas.originX = 40.0f;
      canvas.baseline = 130.0f;
      canvas.sizePx = 36.0f;
      const RenderOutput out = renderStatex(commandFor(e->name), canvas);

      const int cx = kMargin + (k % kCols) * kCellW;
      const int cy = kMargin + 18 + (k / kCols) * kCellH;

      int x0, y0, x1, y1;
      if (out.ok() && inkBounds(out.image, x0, y0, x1, y1)) {
        const int iw = x1 - x0 + 1, ih = y1 - y0 + 1;
        // Centre the ink in the glyph band, clipping rather than scaling: a
        // scaled glyph would hide exactly the size errors worth seeing.
        const int ox = cx + (kCellW - iw) / 2;
        const int oy = cy + (kGlyphH - ih) / 2;
        blitInk(sheet, out.image, ox, oy, x0, y0, iw, ih);
        ++drawn;
      } else {
        drawCaption(sheet, cx + 4, cy + kGlyphH / 2, "??", 14, 255, 80, 80);
      }

      // The name under each glyph is the whole point: a wrong slot is only
      // obvious when the picture sits next to what it claims to be.
      std::string label(e->name);
      int lw = measureCaption(label, 8);
      while (lw > kCellW - 4 && label.size() > 2) {
        label.resize(label.size() - 1);
        lw = measureCaption(label + ".", 8);
      }
      if (label != e->name) label += ".";
      drawCaption(sheet, cx + (kCellW - measureCaption(label, 8)) / 2,
                  cy + kCellH - 6, label, 8, 150, 170, 190);
    }

    char name[64];
    std::snprintf(name, sizeof(name), "symbols_sheet_%02d.png", s + 1);
    const std::string path = artifactPath(name);
    TEST_ASSERT_TRUE_MESSAGE(stximg::writePng(sheet, path), path.c_str());
  }

  std::printf("  wrote %d contact sheet(s) covering %d symbols to %s\n", sheets,
              drawn, artifactDir().c_str());
  TEST_ASSERT_EQUAL_INT(symbolCount(), drawn);
}

static void test_the_suite_is_actually_wide() {
  // Guards the guard. Every assertion above is vacuous if the table has
  // collapsed, and a sweep that silently checks fourteen names would still
  // pass while proving nothing.
  TEST_ASSERT_GREATER_THAN_INT(400, symbolCount());
}

static void test_overlays_carry_ink_without_advancing() {
  // TeX draws these on top of the symbol that follows, so they declare a zero
  // advance while having a real picture. They are the one place a zero advance
  // is correct rather than a metric lookup that failed, and the sweep above
  // would not distinguish the two.
  static const char* kOverlays[] = {"not", "mapstochar", "mapsfromchar"};
  for (const char* name : kOverlays) {
    const SymbolEntry* e = findSymbol(name, (int)std::string(name).size());
    TEST_ASSERT_TRUE_MESSAGE(e != nullptr, name);
    const GlyphRecord* g = findGlyphRecord(Face::Symbol, e->glyph);
    TEST_ASSERT_TRUE_MESSAGE(g != nullptr, name);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g->advance, name);
    // Zero advance, but a real box: that is the whole point of an overlay.
    TEST_ASSERT_TRUE_MESSAGE(g->boxW > 0 && g->boxH > 0, name);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_suite_is_actually_wide);
  RUN_TEST(test_every_symbol_renders_ink);
  RUN_TEST(test_overlays_carry_ink_without_advancing);
  RUN_TEST(test_contact_sheets_are_written);
  return UNITY_END();
}
