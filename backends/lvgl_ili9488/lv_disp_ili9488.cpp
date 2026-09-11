#include "lv_disp_ili9488.h"

namespace lvglport {
namespace {

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  auto* panel = static_cast<drivers::Ili9488*>(lv_display_get_user_data(disp));

  // lv_area_t bounds are INCLUSIVE at both ends. Off-by-one here is the
  // classic first-flush bug.
  const int w = area->x2 - area->x1 + 1;
  const int h = area->y2 - area->y1 + 1;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);

  // LVGL's RGB888 is {blue, green, red} in memory -- lv_color_t is declared
  // that way unconditionally in v9 -- and the panel wants R,G,B. So the whole
  // conversion is a three-byte reversal, done in place.
  //
  // In place is sanctioned: in LV_DISPLAY_RENDER_MODE_PARTIAL with a single
  // buffer, LVGL re-renders the whole buffer before the next flush and treats
  // its contents as undefined once flushCb returns. It would NOT be safe in
  // DIRECT or FULL mode, where the buffer persists between frames.
  for (size_t i = 0; i < n; ++i) {
    uint8_t* p = px_map + i * 3;
    const uint8_t b = p[0];
    p[0] = p[2];
    p[2] = b;
  }

  panel->blitRgb888(area->x1, area->y1, w, h, px_map);

  // Strictly after the transfer completes. drivers::DisplayBus::write is
  // synchronous here, so the bytes are gone by now. Calling this early would
  // let LVGL render into a buffer still being transmitted -- intermittent
  // tearing, and miserable to diagnose.
  lv_display_flush_ready(disp);
}

}  // namespace

lv_display_t* lvDisplayCreateIli9488(drivers::Ili9488& panel, uint8_t* buf,
                                     size_t bufBytes) {
  lv_display_t* disp = lv_display_create(panel.width(), panel.height());
  if (disp == nullptr) return nullptr;

  // Colour format before buffers: the buffer's stride and row count are
  // derived from it.
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(disp, buf, nullptr, static_cast<uint32_t>(bufBytes),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(disp, &panel);
  lv_display_set_flush_cb(disp, flushCb);
  return disp;
}

}  // namespace lvglport
