#ifndef EMBEDTEX_TEENSY_BUS_H
#define EMBEDTEX_TEENSY_BUS_H

// DisplayBus over the Teensy 4.1's SPI and GPIO. This is the whole
// MCU-specific surface of the display path -- everything else about driving
// the panel lives in drivers/ili9488 and knows nothing about this board.

#include <Arduino.h>
#include <SPI.h>

#include "display_bus.h"

namespace teensy41 {

class TeensySpiBus : public drivers::DisplayBus {
 public:
  // Defaults match the wiring the PlatformIO build used. rstPin 255 means the
  // reset line is not wired.
  TeensySpiBus(uint8_t csPin = 10, uint8_t dcPin = 9, uint8_t rstPin = 8,
               uint32_t spiHz = 20000000)
      : _cs(csPin), _dc(dcPin), _rst(rstPin), _hz(spiHz) {}

  /** Claim the pins and start SPI. Call before the controller's begin(). */
  void begin() {
    pinMode(_cs, OUTPUT);
    pinMode(_dc, OUTPUT);
    digitalWriteFast(_cs, HIGH);
    digitalWriteFast(_dc, HIGH);
    if (_rst != 255) {
      pinMode(_rst, OUTPUT);
      digitalWriteFast(_rst, HIGH);
    }
    SPI.begin();
  }

  void beginTransaction() override {
    SPI.beginTransaction(SPISettings(_hz, MSBFIRST, SPI_MODE0));
    digitalWriteFast(_cs, LOW);
  }

  void endTransaction() override {
    digitalWriteFast(_cs, HIGH);
    SPI.endTransaction();
  }

  void setCommandMode(bool command) override {
    digitalWriteFast(_dc, command ? LOW : HIGH);
  }

  void write(const uint8_t* data, size_t n) override {
    // A null receive buffer is explicitly supported by the IMXRT1062
    // implementation -- every store is guarded by `if (p_read)` -- so the
    // read side costs nothing and clobbers nothing.
    SPI.transfer(data, nullptr, n);
  }

  void hardReset() override {
    if (_rst == 255) return;
    digitalWriteFast(_rst, HIGH);
    delay(5);
    digitalWriteFast(_rst, LOW);
    delay(20);
    digitalWriteFast(_rst, HIGH);
    delay(150);
  }

  void delayMs(uint32_t ms) override { delay(ms); }

 private:
  uint8_t _cs, _dc, _rst;
  uint32_t _hz;
};

}  // namespace teensy41

#endif  // EMBEDTEX_TEENSY_BUS_H
