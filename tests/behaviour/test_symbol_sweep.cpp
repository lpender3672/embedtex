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

#include <cstdio>
#include <string>
#include <vector>

#include "statex_glyphstore.h"
#include "statex_symbols.h"
#include "stx_render.h"

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
  return UNITY_END();
}
