#ifndef GRAPHIC_TFT_H_INCLUDED
#define GRAPHIC_TFT_H_INCLUDED

#include <string>
#include "graphic/graphic.h"
#include <TFT_eSPI.h>
#include <OpenFontRender.h>

namespace tex {

class Font_tft : public Font {
private:
    std::string _family;
    std::string _file;  // TTF file path
    int _style;
    float _size;

public:
    Font_tft(const std::string& family = "", int style = PLAIN, float size = 1.f);
    Font_tft(const std::string& file, float size);

    std::string getFamily() const { return _family; }
    std::string getFile() const { return _file; }
    int getStyle() const { return _style; }

    virtual float getSize() const override { return _size; }
    virtual FontKind kind() const override { return FontKind::TFT; }
    virtual sptr<Font> deriveFont(int style) const override;
    virtual bool operator==(const Font& f) const override;
    virtual bool operator!=(const Font& f) const override;
    virtual ~Font_tft() {}
};

/**************************************************************************************************/

class TextLayout_tft : public TextLayout {
private:
    std::wstring _text;
    sptr<Font_tft> _font;

public:
    TextLayout_tft(const std::wstring& src, const sptr<Font_tft>& font);

    virtual void getBounds(Rect& r) override;
    virtual void draw(Graphics2D& g2, float x, float y) override;
};

/**************************************************************************************************/

class Graphics2D_tft : public Graphics2D {
private:
    Font_tft _default_font;
    TFT_eSPI* _tft;
    OpenFontRender* _ofr;

    color _color;
    Stroke _stroke;
    const Font_tft* _font;
    float _sx, _sy;
    float _tx, _ty;
    
    std::string _currentFontFile;  // Track loaded font

    unsigned int _currentFontSizePx = 0;
    int32_t _currentAscentPx = 0;

    uint16_t colorTo565(color c) const;
    void ensureFontLoaded();
    void ensureFontMetrics(unsigned int fontSizePx);

public:
    Graphics2D_tft(TFT_eSPI* tft, OpenFontRender* ofr);

    TFT_eSPI* getTFT() const { return _tft; }

    virtual void setColor(color c) override;
    virtual color getColor() const override;
    virtual void setStroke(const Stroke& s) override;
    virtual const Stroke& getStroke() const override;
    virtual void setStrokeWidth(float w) override;
    virtual const Font* getFont() const override;
    virtual void setFont(const Font* font) override;
    virtual void translate(float dx, float dy) override;
    virtual void scale(float sx, float sy) override;
    virtual void rotate(float angle) override;
    virtual void rotate(float angle, float px, float py) override;
    virtual void reset() override;
    virtual float sx() const override;
    virtual float sy() const override;
    virtual void drawChar(wchar_t c, float x, float y) override;
    virtual void drawText(const std::wstring& t, float x, float y) override;
    virtual void drawLine(float x1, float y1, float x2, float y2) override;
    virtual void drawRect(float x, float y, float w, float h) override;
    virtual void fillRect(float x, float y, float w, float h) override;
    virtual void drawRoundRect(float x, float y, float w, float h, float rx, float ry) override;
    virtual void fillRoundRect(float x, float y, float w, float h, float rx, float ry) override;
};

}  // namespace tex

#endif