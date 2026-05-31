// StaTeX on Teensy 4.1: parse + lay out + draw a math formula to a TFT, using
// a single static scratch arena and the flash glyph atlas. No heap, no
// exceptions, no RTTI, no runtime font/SD access on the render path.
#include <Arduino.h>
#undef PI

#include <TFT_eSPI.h>

#include "statex_render.h"
#include "statex_tft.h"

using namespace statex;

TFT_eSPI tft;

// The one and only working memory for rendering (STX-MEM-02). Sized once here;
// if a formula doesn't fit, render() refuses gracefully (STX-MEM-03).
static uint8_t g_scratch[96 * 1024];
static Renderer g_renderer(g_scratch, sizeof(g_scratch));

static const float kSizePx = 24.0f;

static void renderFormula(const c32* tex, int len, float x, float baseline) {
  TftGraphics g(tft, TFT_WHITE, kSizePx);
  RenderStats st{};
  const ParseError e =
      g_renderer.render(tex, len, kSizePx, x, baseline, g, &st);
  if (e == ParseError::Ok) {
    Serial.printf("rendered: %dx%d (depth %d), scratch high-water %lu bytes\n",
                  (int)st.width, (int)st.height, (int)st.depth,
                  (unsigned long)st.highWater);
  } else {
    Serial.printf("render refused, code=%d\n", (int)e);
  }
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Glyphs present in the seed atlas: + 0 1 2 x y. This fraction uses them all.
  const c32 tex[] = U"\\frac{x^2+1}{2}";
  renderFormula(tex, (int)(sizeof(tex) / sizeof(c32)) - 1, 20.0f, 90.0f);
}

void loop() {}
