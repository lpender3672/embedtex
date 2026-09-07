#include "stx_render.h"

#include "statex_glyphstore.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>


namespace stxtest {

using namespace statex;

namespace {

// One scratch arena reused across renders. Sized to match src/main.cpp so the
// suite exercises the same capability bound the device has.
constexpr u32 kScratchBytes = 96 * 1024;
u8 g_scratch[kScratchBytes];

}  // namespace

void ImageGraphics::blendCoverage(int x, int y, int w, int h, const u8* cov) {
  blits++;
  bool anyInk = false;
  for (int i = 0, n = w * h; i < n && !anyInk; ++i) {
    if (cov[i] != 0) anyInk = true;
  }
  if (!anyInk) blankBlits++;
  _img.blendMax(x, y, w, h, cov);
}

void ImageGraphics::drawRule(float x, float top, float w, float h) {
  rules++;
  rulePlacements.push_back(RulePlacement{x, top, w, h});
  _img.fillRect(x, top, w, h, 255);
}

RenderOutput renderStatex(const std::u32string& tex, const Canvas& canvas) {
  return renderStatex(tex, canvas, RenderCaps{});
}

RenderOutput renderStatex(const std::u32string& tex, const Canvas& canvas,
                          const RenderCaps& caps) {
  RenderOutput out;
  out.image = stximg::Image(canvas.width, canvas.height);
  ImageGraphics g(out.image);

  Renderer renderer(g_scratch, kScratchBytes, caps);
  RenderStats stats{};
  // Sized past the largest corpus formula; `truncated()` below turns an
  // overflow into a visible failure rather than a short list that silently
  // compares equal for its first N entries.
  GlyphPlacement slots[512];
  GlyphProbe probe;
  probe.out = slots;
  probe.cap = static_cast<u16>(sizeof(slots) / sizeof(slots[0]));
  out.error = renderer.render(tex.data(), static_cast<int>(tex.size()),
                              canvas.sizePx, canvas.originX, canvas.baseline,
                              g, &stats, &probe);
  out.stats = stats;
  out.placementsTruncated = probe.truncated();
  const u16 kept = probe.truncated() ? probe.cap : probe.count;
  out.placements.assign(slots, slots + kept);
  out.coverageBlits = g.blits;
  out.rules = g.rules;
  out.blankBlits = g.blankBlits;
  out.rulePlacements = g.rulePlacements;
  return out;
}

std::u32string tex32(const char* ascii) {
  std::u32string s;
  for (const char* p = ascii; *p != '\0'; ++p) {
    s.push_back(static_cast<char32_t>(static_cast<unsigned char>(*p)));
  }
  return s;
}

const std::string& artifactDir() {
  static std::string dir = []() {
    const char* env = std::getenv("STATEX_TEST_ARTIFACTS");
    std::string d = env != nullptr ? env : "statex-test-artifacts";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
  }();
  return dir;
}

std::string artifactPath(const std::string& name) {
  return artifactDir() + "/" + name;
}

const GlyphRecord* mathGlyphRecord(char ascii) {
  const c32 src = static_cast<c32>(static_cast<unsigned char>(ascii));
  const c32 cp = mathModeGlyph(src);
  const Face face = isMathModeSymbol(src) ? Face::Symbol : defaultMathFace(cp);
  return findGlyphRecord(face, cp);
}

double bareAdvanceSum(const char* ascii, float emPx) {
  double sum = 0.0;
  for (const char* p = ascii; *p != '\0'; ++p) {
    const GlyphRecord* g = mathGlyphRecord(*p);
    if (g != nullptr) sum += emUnits(g->advance, emPx);
  }
  return sum;
}

int clearArtifacts(const std::string& prefix) {
  // Artifacts are written only for cases that FAIL, so a case that starts
  // passing leaves its last failure picture behind for ever. Browsing the
  // folder then shows a mix of current and historical failures with nothing
  // to tell them apart, which makes progress look like stagnation. Each
  // suite wipes its own prefix before it writes, so what is on disk after a
  // run is exactly what that run found.
  int removed = 0;
  std::error_code ec;
  std::filesystem::directory_iterator it(artifactDir(), ec);
  if (ec) return 0;
  for (const auto& entry : it) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (name.rfind(prefix, 0) != 0) continue;
    if (std::filesystem::remove(entry.path(), ec)) removed++;
  }
  return removed;
}

}  // namespace stxtest
