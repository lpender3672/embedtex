#include "graphic/graphic_tft.h"
#include <cmath>
#include <cstring>
#include <Arduino.h>

namespace tex {

// Static factory functions required by MicroTeX
Font* Font::create(const std::string& file, float size) {
    return new Font_tft(file, size);
}

sptr<Font> Font::_create(const std::string& family, int style, float size) {
    return sptrOf<Font_tft>(family, style, size);
}

sptr<TextLayout> TextLayout::create(const std::wstring& src, const sptr<Font>& font) {
    sptr<Font_tft> f;
    if (font && font->kind() == FontKind::TFT) {
        f = std::static_pointer_cast<Font_tft>(font);
    } else {
        f = sptrOf<Font_tft>("", PLAIN, 12.f);
    }
    return sptrOf<TextLayout_tft>(src, f);
}

/**************************************************************************************************/
// Font_tft implementation
/**************************************************************************************************/

Font_tft::Font_tft(const std::string& family, int style, float size)
    : _family(family), _style(style), _size(size) {
    // Map size to TFT_eSPI font number (rough approximation)
    if (size <= 10) _tft_font = 1;
    else if (size <= 16) _tft_font = 2;
    else if (size <= 24) _tft_font = 4;
    else _tft_font = 4;
}

Font_tft::Font_tft(const std::string& file, float size)
    : _family(file), _style(PLAIN), _size(size) {
    if (size <= 10) _tft_font = 1;
    else if (size <= 16) _tft_font = 2;
    else if (size <= 24) _tft_font = 4;
    else _tft_font = 4;
}

std::string Font_tft::getFamily() const {
    return _family;
}

int Font_tft::getStyle() const {
    return _style;
}

float Font_tft::getSize() const {
    return _size;
}

sptr<Font> Font_tft::deriveFont(int style) const {
    return sptrOf<Font_tft>(_family, style, _size);
}

bool Font_tft::operator==(const Font& f) const {
    if (f.kind() != FontKind::TFT) return false;
    const Font_tft* other = static_cast<const Font_tft*>(&f);
    return _family == other->_family && _style == other->_style && _size == other->_size;
}

bool Font_tft::operator!=(const Font& f) const {
    return !(*this == f);
}

sptr<Font> Font_tft::create(const std::string& file, float size) {
    return sptrOf<Font_tft>(file, size);
}

sptr<Font> Font_tft::_create(const std::string& family, int style, float size) {
    return sptrOf<Font_tft>(family, style, size);
}

/**************************************************************************************************/
// TextLayout_tft implementation
/**************************************************************************************************/

TextLayout_tft::TextLayout_tft(const std::wstring& src, const sptr<Font_tft>& font)
    : _text(src), _font(font) {}

void TextLayout_tft::getBounds(Rect& r) {
    // Approximate bounds - TFT_eSPI doesn't have great metrics
    // You may need to adjust these multipliers for your specific font
    float charWidth = _font->getSize() * 0.6f;
    float height = _font->getSize();

    r.x = 0;
    r.y = -height * 0.8f;  // baseline offset
    r.w = charWidth * _text.length();
    r.h = height;
}

void TextLayout_tft::draw(Graphics2D& g2, float x, float y) {
    g2.drawText(_text, x, y);
}

sptr<TextLayout> TextLayout_tft::create(const std::wstring& src, const sptr<Font>& font) {
    sptr<Font_tft> f;
    if (font && font->kind() == FontKind::TFT) {
        f = std::static_pointer_cast<Font_tft>(font);
    } else {
        f = sptrOf<Font_tft>("", PLAIN, 12.f);
    }
    return sptrOf<TextLayout_tft>(src, f);
}

/**************************************************************************************************/
// Graphics2D_tft implementation
/**************************************************************************************************/

Graphics2D_tft::Graphics2D_tft(TFT_eSPI* tft)
    : _default_font("", PLAIN, 12.f),
      _tft(tft),
      _color(BLACK),
      _font(&_default_font),
      _sx(1.f), _sy(1.f),
      _tx(0.f), _ty(0.f) {}

uint16_t Graphics2D_tft::colorTo565(color c) const {
    uint8_t r = color_r(c);
    uint8_t g = color_g(c);
    uint8_t b = color_b(c);
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Graphics2D_tft::setColor(color c) {
    Serial.printf("setColor: 0x%08X -> 565: 0x%04X\n", c, colorTo565(c));
    _color = c;
}

color Graphics2D_tft::getColor() const {
    return _color;
}

void Graphics2D_tft::setStroke(const Stroke& s) {
    _stroke = s;
}

const Stroke& Graphics2D_tft::getStroke() const {
    return _stroke;
}

void Graphics2D_tft::setStrokeWidth(float w) {
    _stroke.lineWidth = w;
}

const Font* Graphics2D_tft::getFont() const {
    return _font;
}

void Graphics2D_tft::setFont(const Font* font) {
    Serial.printf("setFont called: %p\n", font);
    if (font && font->kind() == FontKind::TFT) {
        _font = static_cast<const Font_tft*>(font);
        _tft->setTextFont(_font->getTftFont());
    }
}

void Graphics2D_tft::translate(float dx, float dy) {
    _tx += dx * _sx;
    _ty += dy * _sy;
}

void Graphics2D_tft::scale(float sx, float sy) {
    _sx *= sx;
    _sy *= sy;
}

void Graphics2D_tft::rotate(float angle) {
    // TFT_eSPI doesn't support rotation transforms
    // Would need sprite-based rotation for full support
}

void Graphics2D_tft::rotate(float angle, float px, float py) {
    // Not supported
}

void Graphics2D_tft::reset() {
    _sx = _sy = 1.f;
    _tx = _ty = 0.f;
}

float Graphics2D_tft::sx() const {
    return _sx;
}

float Graphics2D_tft::sy() const {
    return _sy;
}

void Graphics2D_tft::drawChar(wchar_t c, float x, float y) {
    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    uint16_t col = colorTo565(_color);
    
    int textSize = max(1, (int)(_sx / 8));
    
    Serial.printf("drawChar: '%c' sx=%.1f textSize=%d at (%d,%d)\n", (char)c, _sx, textSize, px, py);
    
    _tft->setTextSize(textSize);
    
    if (c < 256) {
        _tft->drawChar(px, py, static_cast<char>(c), col, TFT_WHITE, textSize);
    }
}

void Graphics2D_tft::drawText(const std::wstring& t, float x, float y) {
    Serial.printf("drawText: len=%d at %.1f,%.1f\n", t.length(), x, y);

    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    uint16_t col = colorTo565(_color);

    _tft->setTextColor(col);
    _tft->setCursor(px, py);

    // Convert wstring to narrow string (ASCII subset)
    for (wchar_t c : t) {
        if (c < 256) {
            _tft->print(static_cast<char>(c));
        } else {
            _tft->print('?');  // Placeholder for unsupported chars
        }
    }
}

void Graphics2D_tft::drawLine(float x1, float y1, float x2, float y2) {
    Serial.printf("drawLine: %.1f,%.1f -> %.1f,%.1f\n", x1, y1, x2, y2);

    int px1 = static_cast<int>((x1 * _sx) + _tx);
    int py1 = static_cast<int>((y1 * _sy) + _ty);
    int px2 = static_cast<int>((x2 * _sx) + _tx);
    int py2 = static_cast<int>((y2 * _sy) + _ty);
    uint16_t col = colorTo565(_color);

    _tft->drawLine(px1, py1, px2, py2, col);
}

void Graphics2D_tft::drawRect(float x, float y, float w, float h) {
    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    int pw = static_cast<int>(w * _sx);
    int ph = static_cast<int>(h * _sy);
    uint16_t col = colorTo565(_color);

    _tft->drawRect(px, py, pw, ph, col);
}

void Graphics2D_tft::fillRect(float x, float y, float w, float h) {
    Serial.printf("fillRect: %.1f,%.1f %.1fx%.1f\n", x, y, w, h);

    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    int pw = static_cast<int>(w * _sx);
    int ph = static_cast<int>(h * _sy);
    uint16_t col = colorTo565(_color);

    _tft->fillRect(px, py, pw, ph, col);
}

void Graphics2D_tft::drawRoundRect(float x, float y, float w, float h, float rx, float ry) {
    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    int pw = static_cast<int>(w * _sx);
    int ph = static_cast<int>(h * _sy);
    int pr = static_cast<int>(rx * _sx);
    uint16_t col = colorTo565(_color);

    _tft->drawRoundRect(px, py, pw, ph, pr, col);
}

void Graphics2D_tft::fillRoundRect(float x, float y, float w, float h, float rx, float ry) {
    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    int pw = static_cast<int>(w * _sx);
    int ph = static_cast<int>(h * _sy);
    int pr = static_cast<int>(rx * _sx);
    uint16_t col = colorTo565(_color);

    _tft->fillRoundRect(px, py, pw, ph, pr, col);
}

}  // namespace tex
