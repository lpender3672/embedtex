# vendor/

Third-party SDKs, verbatim. **Nothing here is edited.**

| directory | what | used by |
|---|---|---|
| `teensy4/core` | PJRC Teensy 4 core (`cores/teensy4`) | `targets/teensy41` |
| `teensy4/SPI` | PJRC Teensy SPI library | `targets/teensy41` |
| `stm32u5xx/CMSIS` | ARM CMSIS + ST device headers | `targets/stm32u5g9j-dk` |
| `stm32u5xx/STM32U5xx_HAL_Driver` | ST HAL | `targets/stm32u5g9j-dk` |
| `stm32u5xx/system_stm32u5xx.c` | ST system init | `targets/stm32u5g9j-dk` |
| `stm32u5xx/startup_stm32u5g9xx.s` | ST startup / vector table | `targets/stm32u5g9j-dk` |

These sit at the repo root rather than under a target because they belong to
the *silicon*, not the board: every STM32U5 board shares one HAL, and both
Teensy 4.0 and 4.1 share one core. Keeping them here also makes "code we did
not write" a single place to exclude from warnings, linting and review.

## The layers this completes

```
lib/StaTeX/       portable core; declares Graphics2D, knows nothing below it
drivers/          hardware we drive; knows nothing about the application
backends/         Graphics2D implementations; the only layer that knows both
targets/<board>/  pins, clocks, linker script, main()
vendor/           third-party SDKs, never edited
```

The reasoning, and why there is deliberately no HAL layer, is in
`docs/StaTeX-layering.md`.

A `drivers/` entry may depend on a vendor SDK when the hardware *is* an
on-chip peripheral (LTDC, DMA2D) — that dependency is inherent. It must never
depend on a *board*. `drivers/ili9488` is the pure case: it drives an external
controller and includes no vendor header at all, reaching the bus through
`drivers::DisplayBus`, which a target implements.

## CubeMX regeneration

CubeMX writes `Drivers/`, `startup_*.s` and `Core/Src/system_*.c` next to the
`.ioc`, and regenerates `cmake/stm32cubemx/CMakeLists.txt` pointing at them.
That fights this layout. After any regeneration:

```sh
python tools/repoint_cubemx.py targets/stm32u5g9j-dk
```

It repoints the generated CMake at `vendor/` and deletes the copies CubeMX
recreated. It is idempotent, so running it when nothing changed is safe.
