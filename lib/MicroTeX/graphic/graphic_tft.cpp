#include "graphic/graphic_tft.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <Arduino.h>
#include "common.h"

namespace tex {

// Static factory functions required by MicroTeX
Font* Font::create(const std::string& file, float size) {
    return new Font_tft(file, size);
}

sptr<Font> Font::_create(const std::string& family, int style, float size) {
    return sptrOf<Font_tft>(family, style, size);
}

sptr<TextLayout> TextLayout::create(const std::wstring& src, const sptr<Font>& font) {
    auto f = std::static_pointer_cast<Font_tft>(font);
    if (!f) {
        f = sptrOf<Font_tft>("", PLAIN, 12.f);
    }
    return sptrOf<TextLayout_tft>(src, f);
}

/**************************************************************************************************/
// Font_tft implementation
/**************************************************************************************************/

Font_tft::Font_tft(const std::string& family, int style, float size)
    : _family(family), _file(""), _style(style), _size(size) {}

Font_tft::Font_tft(const std::string& file, float size)
    : _family(""), _file(file), _style(PLAIN), _size(size) {}

sptr<Font> Font_tft::deriveFont(int style) const {
    auto f = sptrOf<Font_tft>(_family, style, _size);
    return f;
}

bool Font_tft::operator==(const Font& f) const {
    const Font_tft* other = static_cast<const Font_tft*>(&f);
    return _file == other->_file && _size == other->_size;
}

bool Font_tft::operator!=(const Font& f) const {
    return !(*this == f);
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

/**************************************************************************************************/
// Graphics2D_tft implementation
/**************************************************************************************************/


Graphics2D_tft::Graphics2D_tft(TFT_eSPI* tft, OpenFontRender* ofr)
    : _default_font("", PLAIN, 12.f),
      _tft(tft),
      _ofr(ofr),
      _color(BLACK),
      _font(&_default_font),
      _sx(1.f), _sy(1.f),
      _tx(0.f), _ty(0.f) {}

void Graphics2D_tft::ensureFontLoaded() {
    if (!_font) return;

    if (!_ofr) return;
    
    std::string file = _font->getFile();
    if (file.empty() || file == _currentFontFile) return;
    
    // Build SD path relative to MicroTeX resource root.
    // Font definitions can vary:
    //   - "fonts/..." (preferred, relative to RES_BASE)
    //   - "res/fonts/..." (legacy, already includes default RES_BASE)
    // Avoid producing duplicated roots like "/res/res/fonts/...".
    std::string base = tex::RES_BASE;
    if (!base.empty()) {
        if (base[0] != '/') base = "/" + base;
        while (base.size() > 1 && base.back() == '/') base.pop_back();
    }
    std::string baseNoSlash = base;
    if (!baseNoSlash.empty() && baseNoSlash[0] == '/') baseNoSlash.erase(0, 1);

    std::string sdPath = file;
    if (!sdPath.empty() && sdPath[0] != '/') {
        // Relative path. If it already begins with RES_BASE (e.g. "res/fonts/..."),
        // treat it as already rooted.
        const bool alreadyRooted = (!baseNoSlash.empty() &&
                                   (sdPath == baseNoSlash ||
                                    sdPath.rfind(baseNoSlash + "/", 0) == 0));
        if (alreadyRooted) {
            sdPath = "/" + sdPath;
        } else if (!base.empty()) {
            sdPath = base + "/" + sdPath;
        } else {
            sdPath = "/" + sdPath;
        }
    } else {
        // Absolute path. If it's already under RES_BASE, keep; otherwise we leave it
        // untouched because caller explicitly provided an absolute path.
    }
    
    Serial.printf("Loading font: %s\n", sdPath.c_str());

    const FT_Error err = _ofr->loadFont(sdPath.c_str());
    if (err == 0) {
        _currentFontFile = file;
        _currentFontSizePx = 0;
        _currentAscentPx = 0;
        Serial.println("Font loaded OK");
    } else {
        Serial.printf("Failed to load font (err=%d): %s\n", static_cast<int>(err), sdPath.c_str());
    }
}

void Graphics2D_tft::ensureFontMetrics(unsigned int fontSizePx) {
    if (!_ofr) return;
    if (fontSizePx == 0) fontSizePx = 1;
    if (fontSizePx == _currentFontSizePx && _currentAscentPx > 0) return;

    // OpenFontRender does not expose ascender/descender publicly.
    // For Align::TopLeft, its internal baseline is computed as (y + ascender).
    // We therefore need a reasonable ascender estimate in pixels.
    //
    // IMPORTANT: Do NOT use the full line height (ascender - descender) as ascender,
    // otherwise glyphs are shifted upward by roughly |descender| and TeX rule lines
    // (fraction bar, sqrt overbar) appear too low.
    const char* probe = "Hg"; // tends to exercise ascender+descender
    const FT_BBox top = _ofr->calculateBoundingBox(0, 0, fontSizePx, Align::TopLeft, Layout::Horizontal, probe);
    const int32_t height = std::abs(static_cast<int32_t>(top.yMax - top.yMin));

    // Typical fonts have ascender around 70–85% of line height.
    int32_t ascent = (height > 0) ? (height * 4) / 5 : static_cast<int32_t>(fontSizePx);
    if (ascent <= 0) ascent = static_cast<int32_t>(fontSizePx);

    _currentFontSizePx = fontSizePx;
    _currentAscentPx = ascent;
}

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

void Graphics2D_tft::setFont(const Font* font) {
    if (font) {
        _font = static_cast<const Font_tft*>(font);
    }
}

void Graphics2D_tft::drawChar(wchar_t c, float x, float y) {
    int px = static_cast<int>((x * _sx) + _tx);
    int py = static_cast<int>((y * _sy) + _ty);
    
    ensureFontLoaded();

    if (!_ofr) return;
    
    // MicroTeX renders in TeX-units and applies scale() to convert to pixels.
    // Use current X scale as the pixel font size, clamped to >= 1.
    const unsigned int fontSizePx = std::max(1u, static_cast<unsigned int>(std::lround(std::abs(_sx))));
    _ofr->setFontSize(fontSizePx);
    _ofr->setFontColor(colorTo565(_color));

    // MicroTeX's y is baseline-aligned. OpenFontRender's default alignment treats y as a
    // text-box anchor (TopLeft), shifting internally by ascender. Convert baseline->TopLeft.
    _ofr->setAlignment(Align::TopLeft);
    ensureFontMetrics(fontSizePx);
    const int pyTop = py - _currentAscentPx;
    
    // Convert wchar to UTF-8 for OpenFontRender
    char utf8[5] = {0};
    if (c < 0x80) {
        utf8[0] = static_cast<char>(c);
    } else if (c < 0x800) {
        utf8[0] = 0xC0 | (c >> 6);
        utf8[1] = 0x80 | (c & 0x3F);
    } else {
        utf8[0] = 0xE0 | (c >> 12);
        utf8[1] = 0x80 | ((c >> 6) & 0x3F);
        utf8[2] = 0x80 | (c & 0x3F);
    }
    
    _ofr->setCursor(px, pyTop);
    _ofr->printf("%s", utf8);
}

void Graphics2D_tft::drawText(const std::wstring& t, float x, float y) {
    for (size_t i = 0; i < t.length(); i++) {
        drawChar(t[i], x, y);
        // Note: proper advance would need font metrics
        x += 0.5f;  // Rough estimate, will need tuning
    }
}

void Graphics2D_tft::drawLine(float x1, float y1, float x2, float y2) {
    Serial.printf("drawLine: %.1f,%.1f -> %.1f,%.1f\n", x1, y1, x2, y2);

    int px1 = static_cast<int>((x1 * _sx) + _tx);
    int py1 = static_cast<int>((y1 * _sy) + _ty);
    int px2 = static_cast<int>((x2 * _sx) + _tx);
    int py2 = static_cast<int>((y2 * _sy) + _ty);
    uint16_t col = colorTo565(_color);

    // Respect stroke width for TeX rules (fraction bar, sqrt overbar, etc.).
    // These are overwhelmingly axis-aligned lines.
    const float lw = _stroke.lineWidth;
    if (py1 == py2) {
        const int tPx = std::max(1, static_cast<int>(std::lround(std::abs(lw * _sy))));
        const int xMin = std::min(px1, px2);
        const int xMax = std::max(px1, px2);
        const int yTop = py1 - (tPx / 2);
        _tft->fillRect(xMin, yTop, (xMax - xMin + 1), tPx, col);
        return;
    }
    if (px1 == px2) {
        const int tPx = std::max(1, static_cast<int>(std::lround(std::abs(lw * _sx))));
        const int yMin = std::min(py1, py2);
        const int yMax = std::max(py1, py2);
        const int xLeft = px1 - (tPx / 2);
        _tft->fillRect(xLeft, yMin, tPx, (yMax - yMin + 1), col);
        return;
    }

    // Fallback: TFT_eSPI only draws 1px lines.
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
