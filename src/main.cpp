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

static const float kSizePx = 22.0f;

static void renderFormula(const c32* tex, int len, float x, float baseline) {
  TftGraphics g(tft, TFT_WHITE);
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

  // A showcase matrix: bold label + relation + a 2x2 bmatrix whose cells mix
  // fractions, super/subscripts, a radical, Greek, and big operators.
  const c32 tex[] =
      U"\\mathbf{A}=\\begin{bmatrix}"
      U"\\frac{x^2+1}{2} & \\sqrt{\\omega} \\\\"
      U"\\alpha^2_i & \\sum\\leq\\infty"
      U"\\end{bmatrix}";
  renderFormula(tex, (int)(sizeof(tex) / sizeof(c32)) - 1, 12.0f, 170.0f);
}

void loop() {}
