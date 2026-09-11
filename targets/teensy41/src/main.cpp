// StaTeX under LVGL on a Teensy 4.1 driving a 320x480 ILI9488.
//
// Formulas are rendered once into A8 coverage buffers and handed to LVGL as
// images. LVGL colours them via img_recolor -- LV_COLOR_FORMAT_A8 is documented
// as being for exactly this, font-like bitmaps that are one colour -- and
// composites and scrolls them from then on. Nothing re-rasterises on a scroll.
//
// Each formula is measured first (parse and layout only, no sampler) so its
// buffer can be sized before anything is drawn into it.
#include <Arduino.h>
#undef PI

#include <string.h>

#include "ili9488.h"
#include "lv_disp_ili9488.h"
#include "lvgl.h"
#include "statex_a8.h"
#include "statex_render.h"
#include "teensy_bus.h"

using namespace statex;

// --- hardware ---------------------------------------------------------------
static teensy41::TeensySpiBus g_bus;
static drivers::Ili9488 g_panel(g_bus);

// --- StaTeX -----------------------------------------------------------------
// The one and only working memory for rendering (STX-MEM-02).
static uint8_t g_scratch[96 * 1024];
static Renderer g_renderer(g_scratch, sizeof(g_scratch));

static const float kEmPx = 18.0f;

// --- LVGL memory ------------------------------------------------------------
// Both buffers live in RAM2 (.dmabuffers). DTCM is 480 KB and StaTeX's scratch
// already takes 96 KB of it. Note .dmabuffers is NOLOAD and startup.c clears
// only _sbss.._ebss, so nothing here is zeroed at boot -- memset before use.
static const int kFlushRows = 32;  // ~1/10 screen, LVGL's guidance
DMAMEM __attribute__((aligned(32)))
static uint8_t g_lv_draw_buf[480 * kFlushRows * 3];

// One A8 buffer per formula. 1 byte/px, against 2-4 for a colour canvas.
static const int kFormulaCount = 4;
static const int kA8W = 464;
// Tall enough for the deepest formula here: the bracketed matrix measures
// 111 px at an 18 px em, and 92 clipped 338 of its pixels on the panel.
static const int kA8H = 128;
DMAMEM __attribute__((aligned(4)))
static uint8_t g_a8[kFormulaCount][kA8W * kA8H];
static lv_draw_buf_t g_a8_buf[kFormulaCount];

// --- content ----------------------------------------------------------------
struct Formula {
  const c32* tex;
  int len;
};

#define TEX_LINE(lit) \
  Formula { lit, static_cast<int>(sizeof(lit) / sizeof(c32)) - 1 }

// Only the closed command set (STX-LNG-02).
static const Formula kLines[kFormulaCount] = {
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

/**
 * Render one formula into its A8 buffer and return an lv_image showing it,
 * or nullptr if StaTeX refused.
 */
static lv_obj_t* makeFormula(lv_obj_t* parent, int i) {
  RenderStats st{};
  ParseError e =
      g_renderer.measure(kLines[i].tex, kLines[i].len, kEmPx, &st);
  if (e != ParseError::Ok) {
    Serial.printf("line %d refused at measure, code=%d\n", i + 1, (int)e);
    return nullptr;
  }

  const int pad = 4;
  int w = static_cast<int>(st.width) + 2 * pad;
  int h = static_cast<int>(st.height + st.depth) + 2 * pad;
  if (w > kA8W) w = kA8W;
  if (h > kA8H) h = kA8H;

  memset(g_a8[i], 0, sizeof(g_a8[i]));
  backends::CoverageGraphics g(g_a8[i], w, h, w);
  e = g_renderer.render(kLines[i].tex, kLines[i].len, kEmPx,
                        static_cast<float>(pad), st.height + pad, g);
  if (e != ParseError::Ok) {
    Serial.printf("line %d refused at draw, code=%d\n", i + 1, (int)e);
    return nullptr;
  }
  if (g.clipped() != 0) {
    Serial.printf("line %d: %ld px clipped by its A8 buffer\n", i + 1,
                  g.clipped());
  }

  // Built at runtime rather than with LV_DRAW_BUF_DEFINE_STATIC, whose
  // designated initialisers on a bitfield struct are a GCC extension in C++.
  // lv_draw_buf_init sets header.flags = 0, so set_flag afterwards is
  // required, not decorative. It also does not align for you -- hence the
  // aligned attribute on g_a8.
  lv_draw_buf_init(&g_a8_buf[i], static_cast<uint32_t>(w),
                   static_cast<uint32_t>(h), LV_COLOR_FORMAT_A8,
                   static_cast<uint32_t>(w), g_a8[i],
                   static_cast<uint32_t>(w * h));
  lv_draw_buf_set_flag(&g_a8_buf[i], LV_IMAGE_FLAGS_MODIFIABLE);

  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, &g_a8_buf[i]);
  lv_obj_set_style_image_recolor(img, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
  return img;
}

static void buildUi() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  // A scrolling column: the formulas are taller than the panel, so this is the
  // case the A8 design exists for -- scrolling composites cached coverage and
  // re-rasterises nothing.
  lv_obj_t* col = lv_obj_create(scr);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(col, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(col, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(col, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 8, LV_PART_MAIN);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);

  for (int i = 0; i < kFormulaCount; ++i) makeFormula(col, i);
}

void setup() {
  Serial.begin(115200);

  // Hardware first: the very first lv_timer_handler() can flush immediately.
  g_bus.begin();
  g_panel.begin();
  g_panel.setRotation(1);  // 480x320; LVGL must never rotate in software
  g_panel.fillScreen(0x000000);

  // lv_init before any other lv_* call: it runs lv_mem_init(), so the pool
  // does not exist before this point.
  lv_init();

  // v9 has no LV_TICK_CUSTOM. millis is an exact match for uint32_t(*)(void),
  // and using it avoids an ISR entirely -- which matters because with
  // LV_OS_NONE nothing lv_* may be called from interrupt context.
  lv_tick_set_cb(millis);

  memset(g_lv_draw_buf, 0, sizeof(g_lv_draw_buf));
  if (lvglport::lvDisplayCreateIli9488(g_panel, g_lv_draw_buf,
                                       sizeof(g_lv_draw_buf)) == nullptr) {
    Serial.println("lv_display_create failed");
    return;
  }

  buildUi();

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("lvgl heap: used=%u free=%u frag=%u%% max_used=%u\n",
                (unsigned)(mon.total_size - mon.free_size),
                (unsigned)mon.free_size, (unsigned)mon.frag_pct,
                (unsigned)mon.max_used);
  Serial.printf("statex arena high-water %lu of %u B\n",
                (unsigned long)g_renderer.highWater(),
                (unsigned)sizeof(g_scratch));
}

void loop() { lv_timer_handler_run_in_period(5); }
