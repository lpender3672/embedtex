# STM32U5G9J-DK target

CubeMX bringup for the STM32U5G9J discovery kit. **Not yet running StaTeX** —
this is the generated board setup, split so the render work can land on top.

```sh
PATH=/path/to/arm-none-eabi/bin:$PATH \
cmake -S targets/stm32u5g9j-dk -B build-stm32u5g9j -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$PWD/targets/stm32u5g9j-dk/cmake/gcc-arm-none-eabi.cmake
cmake --build build-stm32u5g9j
```

Budget, from the first link: **FLASH 45,557 B of 4 MB, RAM 3,560 B of 3008 KB.**
The StaTeX glyph atlas is ~413 KB, so it fits in flash many times over and
could be copied to SRAM outright if flash read latency turns out to matter.

## What CubeMX configured

`LTDC`, `DMA2D`, `ICACHE`, `HSPI1`, `I2C2`, `RTC`, `TIM3`, `USART1`,
`USB_OTG_HS`. That is most of what the render path wants: a framebuffer via
LTDC, `blendCoverage` offloaded to DMA2D in A8 mode, and ICACHE enabled rather
than left off as it is at reset.

## What is here and what is not

Only board-specific, CubeMX-owned files live here: `Core/` (clocks, MSP pin
mux, ISRs, newlib stubs), the `.ioc`, the linker scripts, and the generated
CMake. The ST HAL, CMSIS, `system_stm32u5xx.c` and `startup_stm32u5g9xx.s` are
in `vendor/stm32u5xx/` — see `vendor/README.md` for why.

## Regenerating from the .ioc

CubeMX does not know about `vendor/`. It will recreate `Drivers/`,
`startup_*.s` and `Core/Src/system_*.c` here, and rewrite
`cmake/stm32cubemx/CMakeLists.txt` to point at them. Afterwards, run:

```sh
python tools/repoint_cubemx.py targets/stm32u5g9j-dk
```

That is the standing cost of hoisting the HAL out. It is one idempotent
command, but forgetting it means you silently build against a second copy of
the HAL sitting inside this directory.

## Next step for StaTeX

A `Graphics2D` writing into the LTDC framebuffer, which is a different shape
from the Teensy's: no `DisplayBus`, no `drivers/ili9488`, just pixel writes
into SRAM (and then DMA2D for the coverage blend, since `blendCoverage`'s
signature is already an A8 job). `lib/StaTeX` is consumed as sources exactly as
`targets/teensy41/CMakeLists.txt` does it.
