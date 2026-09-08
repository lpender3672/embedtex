#ifndef EMBEDTEX_DISPLAY_BUS_H
#define EMBEDTEX_DISPLAY_BUS_H

#include <stddef.h>
#include <stdint.h>

namespace drivers {

/**
 * The transport a command/data display controller needs, and nothing more.
 *
 * This is the whole MCU-facing surface of drivers/. A controller driver above
 * it is specific to its *peripheral* (an ILI9488 is an ILI9488) but agnostic
 * about the machine driving it; a target below it supplies ~30 lines of SPI
 * and GPIO.
 *
 * Pixel data moves a row at a time, so the virtual dispatch here is per-row,
 * not per-pixel, and costs nothing measurable.
 *
 * Implementations are stack or static objects — never heap — so the destructor
 * is non-virtual and protected.
 */
class DisplayBus {
 public:
  /** Assert chip select and configure the clock for a burst. */
  virtual void beginTransaction() = 0;
  virtual void endTransaction() = 0;

  /** true selects the command register (DC low), false pixel/parameter data. */
  virtual void setCommandMode(bool command) = 0;

  /** Write `n` bytes. Received bytes are discarded. */
  virtual void write(const uint8_t* data, size_t n) = 0;

  /** Pulse the controller's reset line. A no-op where RST is not wired. */
  virtual void hardReset() = 0;

  virtual void delayMs(uint32_t ms) = 0;

 protected:
  ~DisplayBus() = default;
};

}  // namespace drivers

#endif  // EMBEDTEX_DISPLAY_BUS_H
