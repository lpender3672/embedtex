// StaTeX on Teensy 4.1: parse + lay out + draw several math formulas to a TFT,
// using a single static scratch arena and the flash glyph atlas. No heap, no
// exceptions, no RTTI, no runtime font/SD access on the render path.
//
// Each line is laid out twice: once against a Graphics2D that discards its
// output, purely to learn the line's width/height/depth, and once for real at
// the baseline that measurement implies. Renders are self-contained
// (STX-API-02), so measuring costs nothing but time and the second pass is
// guaranteed to place what the first one measured.
#include <Arduino.h>
#undef PI

#include "ili9488.h"
#include "ili9488_graphics.h"
#include "statex_render.h"
#include "teensy_bus.h"

using namespace statex;

static teensy41::TeensySpiBus g_bus;
static drivers::Ili9488 panel(g_bus);

// The one and only working memory for rendering (STX-MEM-02). Sized once here;
// if a formula doesn't fit, render() refuses gracefully (STX-MEM-03). The
// corpus below peaks at ~42 KB.
static uint8_t g_scratch[96 * 1024];
static Renderer g_renderer(g_scratch, sizeof(g_scratch));

static const float kSizePx = 18.0f;
static const float kOriginX = 8.0f;
static const float kLineGap = 6.0f;

/** Swallows every primitive: used for the measuring pass. */
class NullGraphics : public Graphics2D {
 public:
  void blendCoverage(int, int, int, int, const u8*) override {}
  void drawRule(float, float, float, float) override {}
};

struct Formula {
  const c32* tex;
  int len;
};

#define TEX_LINE(lit) \
  Formula { lit, static_cast<int>(sizeof(lit) / sizeof(c32)) - 1 }

// Only the closed command set (STX-LNG-02): \frac, \sqrt, the four \math*
// faces, bmatrix, and the symbols carried in the flash table.
static const Formula kLines[] = {
    // eta(2) = pi^2/12 = (1/2) zeta(2)
    TEX_LINE(U"\\sum_{n=1}^{\\infty}\\frac{(-1)^{n+1}}{n^{2}}"
             U"=\\frac{\\pi^{2}}{12}"
             U"=\\frac{1}{2}\\sum_{k=1}^{\\infty}\\frac{1}{k^{2}}"),
    // Debye integral: Gamma(4) zeta(4) = pi^4/15 = 6 zeta(4)
    TEX_LINE(U"\\int_{0}^{\\infty}\\frac{x^{3}}{e^{x}-1}dx"
             U"=\\frac{\\pi^{4}}{15}"
             U"=6\\sum_{k=1}^{\\infty}\\frac{1}{k^{4}}"),
    // phi = sqrt(1 + phi), unrolled four deep
    TEX_LINE(U"\\phi=\\frac{1+\\sqrt{5}}{2}"
             U"=\\sqrt{1+\\sqrt{1+\\sqrt{1+\\sqrt{1+\\phi}}}}"),
    // A matrix whose cells are fractions, radicals and a limited big operator
    TEX_LINE(U"\\mathbf{A}=\\begin{bmatrix}"
             U"\\frac{\\alpha^{2}+\\beta^{2}}{\\gamma_{i}}"
             U"&\\sqrt{\\theta^{2}+\\omega^{2}}\\\\"
             U"\\sum_{k=1}^{n}\\phi_{k}&\\frac{\\pi}{2}\\times\\infty"
             U"\\end{bmatrix}"),
};

static const int kLineCount =
    static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));

void setup() {
  Serial.begin(115200);

  g_bus.begin();
  panel.begin();
  panel.setRotation(1);
  panel.fillScreen(0x000000);

  // Pass 1: measure. A refusal here drops the line from the stack, so the
  // remaining lines still centre correctly.
  RenderStats st[kLineCount];
  bool ok[kLineCount];
  float total = 0.0f;
  for (int i = 0; i < kLineCount; ++i) {
    NullGraphics none;
    st[i] = RenderStats{};
    const ParseError e = g_renderer.render(kLines[i].tex, kLines[i].len,
                                           kSizePx, kOriginX, 0.0f, none,
                                           &st[i]);
    ok[i] = (e == ParseError::Ok);
    if (ok[i]) {
      total += st[i].height + st[i].depth + kLineGap;
    } else {
      Serial.printf("line %d refused, code=%d\n", i + 1, static_cast<int>(e));
    }
  }
  if (total > 0.0f) total -= kLineGap;

  // Pass 2: draw, stacked and centred vertically.
  float y = (static_cast<float>(panel.height()) - total) * 0.5f;
  if (y < 2.0f) y = 2.0f;

  for (int i = 0; i < kLineCount; ++i) {
    if (!ok[i]) continue;
    backends::Ili9488Graphics g(panel, 0xFFFFFF);
    const ParseError e =
        g_renderer.render(kLines[i].tex, kLines[i].len, kSizePx, kOriginX,
                          y + st[i].height, g);
    if (e != ParseError::Ok) {
      Serial.printf("line %d refused while drawing, code=%d\n", i + 1,
                    static_cast<int>(e));
    }
    y += st[i].height + st[i].depth + kLineGap;
  }
}

void loop() {}
