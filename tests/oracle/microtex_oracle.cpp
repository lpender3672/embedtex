#include "microtex_oracle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "latex.h"
#include "render.h"

namespace stxoracle {
namespace {

bool g_ready = false;
std::string g_initError;

FreeTypeRasteriser& rasteriser() {
  static FreeTypeRasteriser r;
  return r;
}

std::wstring widen(const std::string& s) {
  std::wstring w;
  w.reserve(s.size());
  for (char ch : s) {
    w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
  }
  return w;
}

struct Metrics {
  float w = 0.0f, h = 0.0f, d = 0.0f;
  // Distance from the top of MicroTeX's render box down to the baseline.
  // TeXRender::draw() places the box's TOP-LEFT at the given point, whereas
  // StaTeX draws from the baseline, so every recorded y needs this subtracted
  // before the two can be laid over each other. getBaseline() returns a
  // *fraction* of the height, not a distance.
  float baselineFromTop = 0.0f;
};

/**
 * Run one formula through MicroTeX and capture what it drew. Everything
 * MicroTeX can throw is contained here: the oracle must never take the test
 * process down because a formula was rejected.
 */
bool typeset(const std::string& tex, float sizePx, Recording* out, Metrics* m,
             std::string* err) {
  out->clear();
  try {
    tex::TeXRender* r =
        tex::LaTeX::parse(widen(tex), /*width=*/4000, sizePx,
                          /*lineSpace=*/sizePx * 0.2f, /*fg=*/0xff000000);
    if (r == nullptr) {
      if (err != nullptr) *err = "MicroTeX returned no render";
      return false;
    }
    std::unique_ptr<tex::TeXRender> owned(r);  // latex.h leaks this; we do not
    RecordingGraphics2D g(*out);
    owned->draw(g, 0, 0);
    if (m != nullptr) {
      m->w = static_cast<float>(owned->getWidth());
      m->h = static_cast<float>(owned->getHeight());
      m->d = static_cast<float>(owned->getDepth());
      m->baselineFromTop = owned->getBaseline() * m->h;
    }
    if (g.sawRotation() && err != nullptr) *err = "render used rotation";
    return true;
  } catch (const std::exception& e) {
    if (err != nullptr) *err = std::string("MicroTeX threw: ") + e.what();
    return false;
  } catch (...) {
    if (err != nullptr) *err = "MicroTeX threw a non-std exception";
    return false;
  }
}

}  // namespace

bool ensureReady(std::string* error) {
  if (g_ready) return true;
  if (!g_initError.empty()) {
    if (error != nullptr) *error = g_initError;
    return false;
  }
  try {
    tex::LaTeX::init("");
  } catch (const std::exception& e) {
    g_initError = std::string("LaTeX::init threw: ") + e.what();
    if (error != nullptr) *error = g_initError;
    return false;
  } catch (...) {
    g_initError = "LaTeX::init threw a non-std exception";
    if (error != nullptr) *error = g_initError;
    return false;
  }
  if (!rasteriser().ok()) {
    g_initError = "FreeType unavailable: " + rasteriser().error();
    if (error != nullptr) *error = g_initError;
    return false;
  }
  g_ready = true;
  return true;
}

OracleRender render(const std::string& tex, const stxtest::Canvas& canvas) {
  OracleRender out;
  if (!ensureReady(&out.error)) return out;

  Recording rec;
  Metrics m;
  std::string err;
  if (!typeset(tex, canvas.sizePx, &rec, &m, &err)) {
    out.error = err;
    return out;
  }
  out.width = m.w;
  out.height = m.h;
  out.depth = m.d;
  out.textRuns = rec.textLayoutCalls;

  // Line the two baselines up so the comparison measures layout rather than a
  // constant frame-of-reference difference.
  const float yOrigin = canvas.baseline - m.baselineFromTop;

  for (const RecordedChar& rc : rec.chars) {
    OracleGlyph g;
    g.fontPath = rc.fontPath;
    g.slot = static_cast<unsigned>(rc.slot);
    g.x = canvas.originX + rc.x;
    g.baselineY = yOrigin + rc.y;
    g.emPx = std::fabs(rc.sx);
    out.glyphs.push_back(g);
  }

  for (const RecordedRect& r : rec.rects) {
    stxtest::RulePlacement rp;
    rp.x = canvas.originX + r.x;
    rp.top = yOrigin + r.y;
    rp.w = r.w;
    rp.h = r.h;
    out.rules.push_back(rp);
  }

  if (std::getenv("STX_ORACLE_DEBUG") != nullptr) {
    std::printf("[dbg] '%s' em=%.1f box %.0fx%.0f+%.0f baselineFromTop=%.1f\n",
                tex.c_str(), static_cast<double>(canvas.sizePx),
                static_cast<double>(m.w), static_cast<double>(m.h),
                static_cast<double>(m.d),
                static_cast<double>(m.baselineFromTop));
    for (const OracleGlyph& g : out.glyphs) {
      std::printf("[dbg]   slot 0x%02X %-34s pen=%.1f base=%.1f em=%.1f\n",
                  g.slot, g.fontPath.c_str(), static_cast<double>(g.x),
                  static_cast<double>(g.baselineY),
                  static_cast<double>(g.emPx));
    }
    for (const stxtest::RulePlacement& r : out.rules) {
      std::printf("[dbg]   rule x=%.1f top=%.1f w=%.1f h=%.1f\n",
                  static_cast<double>(r.x), static_cast<double>(r.top),
                  static_cast<double>(r.w), static_cast<double>(r.h));
    }
  }

  out.ok = true;
  return out;
}

stximg::Image rasterise(const OracleRender& r, const stxtest::Canvas& canvas,
                        int* unrendered) {
  stximg::Image img(canvas.width, canvas.height);
  int failed = 0;
  for (const OracleGlyph& g : r.glyphs) {
    if (!rasteriser().draw(g, img)) {
      failed++;
      if (std::getenv("STX_ORACLE_DEBUG") != nullptr) {
        std::printf("[dbg] could not render slot 0x%02X of %s\n", g.slot,
                    g.fontPath.c_str());
      }
    }
  }
  for (const stxtest::RulePlacement& rp : r.rules) {
    img.fillRect(rp.x, rp.top, rp.w, rp.h, 255);
  }
  if (unrendered != nullptr) *unrendered = failed;
  return img;
}

int openFaceCount() { return rasteriser().faceCount(); }

}  // namespace stxoracle
