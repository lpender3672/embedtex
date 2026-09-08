# Teensy 4.1 target

StaTeX firmware for a Teensy 4.1 driving a 320×480 ILI9488 over SPI.

```sh
cmake -S targets/teensy41 -B build-teensy41 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/toolchain-teensy41.cmake
cmake --build build-teensy41
cmake --build build-teensy41 --target flash
```

## Why this is a separate CMake project

A cross build needs a different toolchain from the host suites, and one CMake
configure resolves exactly one toolchain. So this is its own project rather
than a subdirectory of the root `CMakeLists.txt`; it consumes `lib/StaTeX` as
sources. The root project still builds the host tests and the MicroTeX oracle.

## Layout

| path | what |
|---|---|
| `src/main.cpp` | the demo: four formulas, measured then stacked |
| `src/teensy_bus.h` | `drivers::DisplayBus` over Teensy SPI + GPIO |
| `imxrt1062_t41_xip.ld` | linker script |

The PJRC core and SPI library are in `vendor/teensy4/` at the repo root, not
here: they belong to the silicon rather than the board. See `vendor/README.md`.

`src/teensy_bus.h` is the **only** file here that knows what an MCU is. The
ILI9488 itself, and its `Graphics2D` binding, live in `drivers/ili9488/` and
include no vendor header — a driver is specific to its peripheral, not to the
machine driving it. Three layers, each ignorant of the one below:

```
lib/StaTeX          Graphics2D; no idea a display controller exists
drivers/ili9488     the controller; talks to an abstract DisplayBus
backends/ili9488    binds the two; the only file that knows both
targets/teensy41    DisplayBus over this board's SPI and pins
```

See `docs/StaTeX-layering.md` for why there is no HAL layer.

## Things that will bite you

**The ILI9488 has no 16-bit mode over SPI.** Pixel format is set to `0x66` and
every pixel goes out as three bytes, six significant bits each, left-aligned.
Sending RGB565 gives you a scrambled panel rather than an error.

**`Blink.cc` is excluded from the vendored core.** It defines its own `setup()`
and `loop()`, and PJRC documents it as "not actually part of the core library".
The CMake glob deliberately covers `*.c`, `*.cpp` and `*.S` but not `*.cc`.

**Upload uses `teensy_post_compile -reboot`, not `teensy_loader_cli`.** The
latter's `-w` blocks forever waiting for a physical button press, and `-s`
(soft reboot) did not take on this board.

**The toolchain defaults to PJRC's** (`toolchain-gccarmnoneeabi-teensy`)
because it is the one shipping `libarm_cortexM7lfsp_math.a`. Nothing here links
CMSIS DSP, so any `arm-none-eabi` GCC should work — point at it with
`-DARM_TOOLCHAIN_DIR=`.

Compiler and linker flags were taken from `pio run -v` on the PlatformIO build
this replaced, so the image is comparable rather than freshly guessed.
