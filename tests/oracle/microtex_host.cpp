#include "microtex_host.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "fonts/font_basic.h"

namespace tex {

// The two platform hooks MicroTeX declares but leaves to the port.
sptr<Font> Font::_create(const std::string& family, int style, float size) {
  return sptrOf<stxoracle::HostFont>(family, style, size);
}

// FontInfo calls this with the font's registered path, which is what
// identifies the TeX font (cmr10, cmmi10, cmsy10, ...). The returned Font is
// owned by the FontInfo that asked for it, hence the raw pointer.
Font* Font::create(const std::string& file, float size) {
  return new stxoracle::HostFont(file, PLAIN, size);
}

sptr<TextLayout> TextLayout::create(const std::wstring& src,
                                    const sptr<Font>& font) {
  auto f = std::static_pointer_cast<stxoracle::HostFont>(font);
  if (!f) f = sptrOf<stxoracle::HostFont>("", PLAIN, 12.f);
  return sptrOf<stxoracle::HostTextLayout>(src, f);
}

}  // namespace tex

namespace stxoracle {

tex::sptr<tex::Font> HostFont::deriveFont(int style) const {
  return tex::sptrOf<HostFont>(_family, style, _size);
}

bool HostFont::operator==(const tex::Font& f) const {
  const HostFont* o = dynamic_cast<const HostFont*>(&f);
  return o != nullptr && o->_family == _family && o->_style == _style &&
         o->_size == _size;
}

void HostTextLayout::getBounds(tex::Rect& bounds) {
  // \text{} is outside the StaTeX grammar, so a real measurement would be
  // inventing numbers. Report an empty box; the oracle counts the call and
  // the differential test skips any formula that triggers it.
  bounds.x = 0;
  bounds.y = 0;
  bounds.w = 0;
  bounds.h = 0;
}

void HostTextLayout::draw(tex::Graphics2D& g2, float x, float y) {
  g2.drawText(_src, x, y);
}

RecordingGraphics2D::RecordingGraphics2D(Recording& out) : _out(out) {}

void RecordingGraphics2D::translate(float dx, float dy) {
  _tx += dx * _sx;
  _ty += dy * _sy;
}

void RecordingGraphics2D::scale(float sx, float sy) {
  _sx *= sx;
  _sy *= sy;
}

void RecordingGraphics2D::rotate(float angle) {
  if (angle != 0.0f) _sawRotation = true;
}

void RecordingGraphics2D::rotate(float angle, float, float) {
  if (angle != 0.0f) _sawRotation = true;
}

void RecordingGraphics2D::reset() {
  _sx = _sy = 1.0f;
  _tx = _ty = 0.0f;
}

void RecordingGraphics2D::drawChar(wchar_t c, float x, float y) {
  RecordedChar rc;
  rc.slot = c;
  rc.x = x * _sx + _tx;
  rc.y = y * _sy + _ty;
  rc.sx = _sx;
  rc.sy = _sy;
  // FontInfo created this Font from the face's registered path, so the family
  // string names the TeX face. That plus the slot is the whole glyph identity.
  const auto* hf = dynamic_cast<const HostFont*>(_font);
  if (hf != nullptr) rc.fontPath = hf->family();
  _out.chars.push_back(rc);
}

void RecordingGraphics2D::drawText(const std::wstring&, float, float) {
  _out.textLayoutCalls++;
}

void RecordingGraphics2D::drawLine(float x1, float y1, float x2, float y2) {
  // MicroTeX draws horizontal rules as lines in some paths; record them as a
  // rect of the current stroke width so the oracle sees fraction bars.
  RecordedRect r;
  r.x = std::min(x1, x2) * _sx + _tx;
  r.y = std::min(y1, y2) * _sy + _ty;
  r.w = std::abs(x2 - x1) * _sx;
  r.h = std::abs(y2 - y1) * _sy;
  if (r.h <= 0.0f) r.h = _stroke.lineWidth * _sy;
  if (r.w <= 0.0f) r.w = _stroke.lineWidth * _sx;
  _out.rects.push_back(r);
}

void RecordingGraphics2D::drawRect(float x, float y, float w, float h) {
  fillRect(x, y, w, h);
}

void RecordingGraphics2D::fillRect(float x, float y, float w, float h) {
  RecordedRect r;
  r.x = x * _sx + _tx;
  r.y = y * _sy + _ty;
  r.w = w * _sx;
  r.h = h * _sy;
  _out.rects.push_back(r);
}

void RecordingGraphics2D::drawRoundRect(float x, float y, float w, float h,
                                        float, float) {
  fillRect(x, y, w, h);
}

void RecordingGraphics2D::fillRoundRect(float x, float y, float w, float h,
                                        float, float) {
  fillRect(x, y, w, h);
}

}  // namespace stxoracle
