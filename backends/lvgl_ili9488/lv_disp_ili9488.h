#ifndef EMBEDTEX_LV_DISP_ILI9488_H
#define EMBEDTEX_LV_DISP_ILI9488_H

// LVGL display bound to an ILI9488.
//
// Knows LVGL and drivers/ili9488, and nothing about any board -- it takes an
// Ili9488&, not pins -- so by the layering rule it is a backend, not target
// code.

#include <stddef.h>
#include <stdint.h>

#include "ili9488.h"
#include "lvgl.h"

// Not `backends` -- that name is taken by statex::backends, and two namespaces
// with one name is ambiguous the moment a file does `using namespace statex`.
// "port" is LVGL's own word for this layer anyway.
namespace lvglport {

/**
 * Create and register an LVGL display that flushes to `panel`.
 *
 * @param panel   already begun and rotated by the caller
 * @param buf     draw buffer, at least `bufBytes`; a whole number of rows of
 *                `w` RGB888 pixels. ~1/10 of the screen is LVGL's guidance.
 * @param bufBytes size of `buf`
 *
 * Single-buffered on purpose: the flush is synchronous (the Teensy SPI write
 * is a polled FIFO loop, not DMA), so a second buffer would sit idle while the
 * first is transmitted. It starts to pay only once the transfer is
 * asynchronous.
 */
lv_display_t* lvDisplayCreateIli9488(drivers::Ili9488& panel, uint8_t* buf,
                                     size_t bufBytes);

}  // namespace lvglport

#endif  // EMBEDTEX_LV_DISP_ILI9488_H
