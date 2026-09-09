# Layering: why there is no HAL

The question this answers: *how do you pair hardware with drivers when different
boards have entirely different peripherals?*

This document covers *where code lives*. `StaTeX-app-architecture.md` covers
*how an application gets on screen* — the LVGL integration points, why colour
belongs to the compositor, and why app isolation is the MPU's job rather than
TrustZone's.

The answer is that you don't try. **There is no middle HAL layer in this repo,
and adding one would be a mistake.** This document exists because that is a
tempting thing to add, and the reason not to is not obvious.

---

## The trap

The instinct is a portable HAL: `Spi`, `Gpio`, `I2c`, `Timer`, implemented once
per target. It fails as soon as two targets differ in kind rather than degree.

The Teensy drives its panel as an ILI9488 over SPI: a byte pipe and a
command/data line. The STM32U5G9J drives its panel over LTDC with a framebuffer
in SRAM and a DMA2D blitter. There is no SPI in the second story at all. A
portable `Spi` abstraction is not merely unhelpful there — it is *meaningless*,
and any layer built on it forces one board to pretend to be the other.

Vendor HALs already exist, they are not interchangeable, and they are not
supposed to be. That is fine. The gap they leave is not a gap to paper over.

## The rule

**Abstract the capability the application needs, not the hardware that
provides it.**

StaTeX needs one thing from a machine: *put ink on a surface*. That is
`Graphics2D`, and it is two calls — `blendCoverage` and `drawRule`
(STX-API-03). Every target can do it. How is nobody's business.

`Graphics2D` is portable because it is defined by what the application needs,
which does not vary. A `Spi` interface would be defined by what some hardware
happens to be, which does.

## The layers

```
lib/StaTeX/       portable core. Declares Graphics2D. Knows nothing below it.
drivers/          hardware we drive. Knows nothing about the application.
backends/         Graphics2D implementations. The ONLY layer that knows both.
targets/<board>/  pins, clocks, linker script, main().
vendor/           third-party SDKs, verbatim, never edited.
```

Dependencies point one way. `lib/StaTeX` includes nothing from the others;
`drivers/` includes nothing from `lib/StaTeX`.

## Two stacks, no shared middle

```
                 lib/StaTeX ──uses──▶ Graphics2D          the one portable seam
                                       ▲          ▲
                        ┌──────────────┘          └──────────────┐
          backends/ili9488                              backends/ltdc
                        │                                        │
                 drivers/ili9488                      ST HAL: LTDC + DMA2D
                        │
                 drivers::DisplayBus     only boards with this panel implement it
                        │
                 targets/teensy41/src/teensy_bus.h   (PJRC SPI)
```

The two slices are different heights and different shapes. **That is the
expected outcome, not a defect.** They meet only at the top. Nothing about the
Teensy's bus is imposed on the STM32, which has no bus at all.

## Why `DisplayBus` is not a HAL

This is the distinction that makes the whole thing work.

`drivers::DisplayBus` is **owned by one driver**. It exists because the ILI9488
needs a byte pipe with a DC line — it is that driver's stated requirement, not
a system-wide abstraction over SPI. A board with no ILI9488 never implements
it, never includes it, never compiles it.

An interface scoped to a driver is not a middle layer. The test: *can a target
ignore it completely and still work?* For `DisplayBus`, yes. For a HAL, no —
that is what makes a HAL a HAL, and what makes it break.

## When does an interface earn its place

**Two or more real implementations with genuinely identical semantics.** Not
imagined ones.

`Graphics2D` passes easily — it has four: the ILI9488 backend, the host image
recorder (`tests/support`), the null measurer in the Teensy `main.cpp`, and
LTDC when it lands. `DisplayBus` has one, and would have been wrong as a
general SPI abstraction; it is justified only as the ILI9488's own contract.

If an interface has one implementation and you are picturing the second, do not
write it. Let the second arrive and show you where the seam actually is.

## Where does X go

| if it… | it goes in |
|---|---|
| depends on a board — pins, wiring, clocks | `targets/<board>/` |
| depends on a chip family's SDK (LTDC, DMA2D) | `drivers/<family>/`, or the target while there is one user |
| drives an external part and needs only a byte pipe | `drivers/<part>/`, plus a bus interface that driver owns |
| implements `Graphics2D` for some hardware | `backends/` |
| the application needs regardless of hardware | an interface in `lib/StaTeX` |
| we did not write | `vendor/` |

A `drivers/` entry **may** depend on a vendor SDK when the hardware *is* an
on-chip peripheral — that dependency is inherent. It must never depend on a
*board*, and never on `lib/StaTeX`.

## What this replaced

Three inversions existed and were fixed:

- `drivers/ili9488/ili9488_graphics.h` included `statex_graphics.h` and
  declared its class inside `namespace statex` — a driver depending upward on
  the application and squatting a namespace it does not own. It is now
  `backends/ili9488/`, in `statex::backends`, and the `ili9488` CMake target no
  longer links `statex` at all.
- `lib/StaTeX/statex_tft.h` was a TFT_eSPI backend inside the portable core
  behind `#if defined(ARDUINO)`. The core carried a display dependency no other
  target could use.
- `lib/StaTeX/statex_record.h` was test scaffolding in the shipped core; it is
  now `tests/support/stx_record.h`.

## Judgment calls left standing

**`statex_serialize` stays in `lib/StaTeX` though only tests use it today.** It
is tree introspection, which has a plausible on-target use (dumping a box tree
over serial), and it costs nothing: `nm` on the linked firmware finds zero of
its symbols, because `--gc-sections` drops it when unreferenced. Revisit if it
grows dependencies rather than on principle.

**`Graphics2D` has no frame concept** — no `beginFrame`/`endFrame`. A
framebuffer target wants to draw into a back buffer and swap, which works today
by wrapping the render call in `main()`. If the interface does need to grow,
that should be a second real implementation demonstrating it, not anticipation.
Do not add it pre-emptively for the U5.
