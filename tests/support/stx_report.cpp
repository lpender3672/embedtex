#include "stx_report.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "statex_glyphstore.h"
#include "statex_sdf.h"
#include "stx_render.h"

namespace stxtest {
namespace {

// Panel background and ink.
constexpr std::uint8_t kBgR = 16, kBgG = 17, kBgB = 20;
constexpr std::uint8_t kChromeR = 40, kChromeG = 42, kChromeB = 48;

constexpr int kMargin = 10;
constexpr int kCaptionEm = 13;
constexpr int kCaptionBand = 20;
constexpr int kTitleEm = 15;
constexpr int kTitleBand = 24;
constexpr int kDetailBand = 18;

constexpr int kCovCap = 128 * 128;
statex::u8 g_cov[kCovCap];

/** The atlas has no '_' or '\'; fall back to something visible. */
char captionChar(char c) {
  if (c == '_') return '-';
  if (c == '\\') return '/';
  return c;
}

void blendInk(stximg::RgbImage& dst, int x, int y, std::uint8_t cov,
              std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  if (cov == 0) return;
  if (x < 0 || y < 0 || x >= dst.w || y >= dst.h) return;
  const size_t i = stximg::rgbIndex(x, y, dst.w);
  const int a = cov;
  auto mix = [&](std::uint8_t base, std::uint8_t fg) {
    return static_cast<std::uint8_t>((base * (255 - a) + fg * a) / 255);
  };
  dst.px[i] = mix(dst.px[i], r);
  dst.px[i + 1] = mix(dst.px[i + 1], g);
  dst.px[i + 2] = mix(dst.px[i + 2], b);
}

}  // namespace

int measureCaption(const std::string& text, int emPx) {
  float pen = 0.0f;
  for (char raw : text) {
    const char c = captionChar(raw);
    const statex::GlyphRecord* rec =
        c == ' ' ? nullptr
                 : statex::findGlyphRecord(
                       statex::Face::Roman,
                       static_cast<statex::c32>(static_cast<unsigned char>(c)));
    pen += rec != nullptr ? statex::emUnits(rec->advance, static_cast<float>(emPx))
                          : static_cast<float>(emPx) * 0.32f;
  }
  return static_cast<int>(pen + 0.5f);
}

int drawCaption(stximg::RgbImage& dst, int x, int baselineY,
                const std::string& text, int emPx, std::uint8_t r,
                std::uint8_t g, std::uint8_t b) {
  float pen = static_cast<float>(x);
  for (char raw : text) {
    const char c = captionChar(raw);
    if (c == ' ') {
      pen += emPx * 0.32f;
      continue;
    }
    const statex::GlyphRecord* rec = statex::findGlyphRecord(
        statex::Face::Roman, static_cast<statex::c32>(
                                 static_cast<unsigned char>(c)));
    if (rec == nullptr) {
      pen += emPx * 0.32f;
      continue;
    }
    statex::GlyphCoverage cov{};
    if (statex::renderGlyphCoverage(*rec, statex::glyphSdfData(),
                                    statex::glyphSdfSpread(), emPx, g_cov,
                                    kCovCap, &cov) &&
        cov.w > 0 && cov.h > 0) {
      const int gx =
          static_cast<int>(pen + statex::emUnits(rec->boxX, emPx) + 0.5f);
      const int gy = static_cast<int>(baselineY -
                                      statex::emUnits(rec->boxY, emPx) + 0.5f);
      for (int j = 0; j < cov.h; ++j) {
        for (int i = 0; i < cov.w; ++i) {
          blendInk(dst, gx + i, gy + j,
                   g_cov[stximg::pixelIndex(i, j, cov.w)], r, g, b);
        }
      }
    }
    pen += statex::emUnits(rec->advance, emPx);
  }
  return static_cast<int>(pen) - x;
}

std::string artifactName(const std::string& id, const std::string& suffix) {
  std::string out;
  for (char c : id) {
    const unsigned char u = static_cast<unsigned char>(c);
    out.push_back((std::isalnum(u) != 0 || c == '_' || c == '-') ? c : '_');
  }
  return out + suffix;
}

bool writeFailureStack(const stximg::Image& reference,
                       const stximg::Image& actual, const std::string& title,
                       const std::string& detail, const std::string& path,
                       const std::string& referenceLabel,
                       const std::string& actualLabel) {
  // Crop both to one shared rectangle so the panels stay registered.
  const stximg::InkStats a = stximg::inkStats(reference);
  const stximg::InkStats b = stximg::inkStats(actual);
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (a.any() && b.any()) {
    x0 = std::min(a.x0, b.x0);
    y0 = std::min(a.y0, b.y0);
    x1 = std::max(a.x1, b.x1);
    y1 = std::max(a.y1, b.y1);
  } else if (a.any()) {
    x0 = a.x0; y0 = a.y0; x1 = a.x1; y1 = a.y1;
  } else if (b.any()) {
    x0 = b.x0; y0 = b.y0; x1 = b.x1; y1 = b.y1;
  } else {
    x0 = y0 = 0;
    x1 = y1 = 15;  // both blank: still emit something rather than nothing
  }
  const int pad = 6;
  x0 = std::max(0, x0 - pad);
  y0 = std::max(0, y0 - pad);
  x1 += pad;
  y1 += pad;
  const int cropW = std::max(16, x1 - x0 + 1);
  const int cropH = std::max(12, y1 - y0 + 1);

  // Magnify the panels by a whole number of pixels. Nearest-neighbour, so
  // what you are looking at is the real pixels enlarged, not a resample that
  // could invent or hide a difference. Small formulas get more magnification
  // than large ones so every picture ends up a similar, readable size.
  int zoom = 1;
  while (zoom < 4 && cropW * (zoom + 1) <= 1100 && cropH * (zoom + 1) <= 420) {
    ++zoom;
  }
  const int pw = cropW * zoom;
  const int ph = cropH * zoom;

  const std::string diffLabel =
      "diff:  red = reference only,  green = statex only,  grey = both";

  // The canvas has to fit the captions as well as the panels, or the text is
  // silently clipped -- which is worse than no caption at all.
  int textWidth = 0;
  const std::pair<const std::string*, int> texts[] = {
      {&title, kTitleEm},
      {&detail, kCaptionEm - 2},
      {&referenceLabel, kCaptionEm},
      {&actualLabel, kCaptionEm},
      {&diffLabel, kCaptionEm},
  };
  for (const auto& t : texts) {
    textWidth = std::max(textWidth, measureCaption(*t.first, t.second));
  }

  const int width = std::max(pw, textWidth) + kMargin * 2;
  const int height = kTitleBand + kDetailBand +
                     3 * (kCaptionBand + ph) + 4 + kMargin;

  stximg::RgbImage out(width, height);
  for (int y = 0; y < out.h; ++y) out.fillRow(y, kBgR, kBgG, kBgB);

  int cursor = 4;
  drawCaption(out, kMargin, cursor + kTitleEm, title, kTitleEm, 235, 235, 240);
  cursor += kTitleBand;
  drawCaption(out, kMargin, cursor + kCaptionEm - 3, detail, kCaptionEm - 2,
              150, 152, 165);
  cursor += kDetailBand;

  struct Panel {
    const stximg::Image* img;
    const std::string* label;
    int mode;  // 0 = plain ink, 1 = diff
  };
  const Panel panels[3] = {
      {&reference, &referenceLabel, 0},
      {&actual, &actualLabel, 0},
      {nullptr, nullptr, 1},
  };
  for (int p = 0; p < 3; ++p) {
    const std::string& label = panels[p].mode == 1 ? diffLabel : *panels[p].label;
    drawCaption(out, kMargin, cursor + kCaptionEm, label, kCaptionEm, 170, 174,
                190);
    const int top = cursor + kCaptionBand;

    for (int y = 0; y < ph; ++y) {
      for (int x = 0; x < pw; ++x) {
        const int sx = x0 + x / zoom;
        const int sy = y0 + y / zoom;
        if (panels[p].mode == 0) {
          const std::uint8_t v = panels[p].img->at(sx, sy);
          blendInk(out, kMargin + x, top + y, v, 245, 245, 250);
        } else {
          const std::uint8_t vr = reference.at(sx, sy);
          const std::uint8_t va = actual.at(sx, sy);
          const bool ir = vr >= 32, ia = va >= 32;
          if (ir && ia) {
            blendInk(out, kMargin + x, top + y,
                     static_cast<std::uint8_t>(std::max(vr, va)), 120, 122, 130);
          } else if (ir) {
            blendInk(out, kMargin + x, top + y, vr, 255, 70, 70);
          } else if (ia) {
            blendInk(out, kMargin + x, top + y, va, 60, 230, 120);
          }
        }
      }
    }
    cursor = top + ph;
    if (p < 2) {
      for (int x = 0; x < out.w; ++x) {
        out.set(x, cursor + 1, kChromeR, kChromeG, kChromeB);
      }
      cursor += 2;
    }
  }

  return stximg::writePng(out, path);
}

}  // namespace stxtest
