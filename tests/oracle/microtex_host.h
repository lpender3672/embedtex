#ifndef STATEX_ORACLE_MICROTEX_HOST_H
#define STATEX_ORACLE_MICROTEX_HOST_H

// A host port of MicroTeX's platform interface (tex::Font, tex::TextLayout,
// tex::Graphics2D) that records draw calls instead of pushing pixels.
//
// MicroTeX computes every glyph position from its own flash metric tables
// (lib/MicroTeX/res/font/*.def.cpp), not from the Font backend -- the backend
// is only asked to *draw*. That is what makes an oracle possible without
// linking a real font rasteriser: we implement the interface as a recorder,
// run MicroTeX's real layout, and read the resulting placements back out.
//
// The one thing the backend genuinely owns is TextLayout::getBounds, used for
// \text{} runs. StaTeX's closed grammar has no text mode, so that path is
// stubbed and any use of it is reported rather than silently guessed at.

#include <string>
#include <vector>

#include "graphic/graphic.h"

namespace stxoracle {

/** One drawChar call, with the transform MicroTeX had accumulated. */
struct RecordedChar {
  wchar_t slot = 0;      // TeX font position, NOT Unicode
  std::string fontPath;  // the face MicroTeX selected, e.g. res/fonts/base/cmex10.ttf
  float x = 0.0f;      // device position after translate/scale
  float y = 0.0f;
  float sx = 1.0f;     // accumulated scale == the em size in pixels
  float sy = 1.0f;
};

struct RecordedRect {
  float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

/** Everything a MicroTeX render emitted. */
struct Recording {
  std::vector<RecordedChar> chars;
  std::vector<RecordedRect> rects;
  int textLayoutCalls = 0;  // non-zero means we hit an unsupported path
  void clear() {
    chars.clear();
    rects.clear();
    textLayoutCalls = 0;
  }
};

/** A minimal tex::Font: identity only, no metrics (MicroTeX supplies those). */
class HostFont : public tex::Font {
 public:
  HostFont(std::string family, int style, float size)
      : _family(std::move(family)), _style(style), _size(size) {}

  float getSize() const override { return _size; }
  tex::sptr<tex::Font> deriveFont(int style) const override;
  bool operator==(const tex::Font& f) const override;
  bool operator!=(const tex::Font& f) const override { return !(*this == f); }

  const std::string& family() const { return _family; }
  int style() const { return _style; }

 private:
  std::string _family;
  int _style;
  float _size;
};

/** A tex::TextLayout stub: \text{} is outside StaTeX's grammar. */
class HostTextLayout : public tex::TextLayout {
 public:
  HostTextLayout(std::wstring src, tex::sptr<HostFont> font)
      : _src(std::move(src)), _font(std::move(font)) {}
  void getBounds(tex::Rect& bounds) override;
  void draw(tex::Graphics2D& g2, float x, float y) override;

 private:
  std::wstring _src;
  tex::sptr<HostFont> _font;
};

/**
 * The recording Graphics2D. Implements the full tex::Graphics2D surface;
 * everything that is pure state (colour, stroke, font) is stored so MicroTeX
 * sees a consistent context, and the transform is tracked so drawChar can be
 * resolved to a device position.
 */
class RecordingGraphics2D : public tex::Graphics2D {
 public:
  explicit RecordingGraphics2D(Recording& out);

  const tex::Font* currentFont() const { return _font; }

  void setColor(tex::color c) override { _color = c; }
  tex::color getColor() const override { return _color; }
  void setStroke(const tex::Stroke& s) override { _stroke = s; }
  const tex::Stroke& getStroke() const override { return _stroke; }
  void setStrokeWidth(float w) override { _stroke.lineWidth = w; }
  const tex::Font* getFont() const override { return _font; }
  void setFont(const tex::Font* font) override { _font = font; }

  void translate(float dx, float dy) override;
  void scale(float sx, float sy) override;
  void rotate(float angle) override;
  void rotate(float angle, float px, float py) override;
  void reset() override;
  float sx() const override { return _sx; }
  float sy() const override { return _sy; }

  void drawChar(wchar_t c, float x, float y) override;
  void drawText(const std::wstring& t, float x, float y) override;
  void drawLine(float x1, float y1, float x2, float y2) override;
  void drawRect(float x, float y, float w, float h) override;
  void fillRect(float x, float y, float w, float h) override;
  void drawRoundRect(float x, float y, float w, float h, float rx,
                     float ry) override;
  void fillRoundRect(float x, float y, float w, float h, float rx,
                     float ry) override;

  /** True if MicroTeX asked for a rotation, which the oracle cannot map. */
  bool sawRotation() const { return _sawRotation; }

 private:
  Recording& _out;
  tex::color _color = 0xff000000;
  tex::Stroke _stroke;
  const tex::Font* _font = nullptr;
  float _sx = 1.0f, _sy = 1.0f;
  float _tx = 0.0f, _ty = 0.0f;
  bool _sawRotation = false;
};

}  // namespace stxoracle

#endif  // STATEX_ORACLE_MICROTEX_HOST_H
