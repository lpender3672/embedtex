# Vendor LVGL and wire StaTeX into it

## Context

The widget layer is the last missing piece. LVGL v9.5.0 is the chosen toolkit,
and hardware acceleration matters because the STM32U5G9J has DMA2D *and* a
NeoChrom GPU — LVGL exposes both as draw units and they can run together.

Two things have to happen for this to be neat rather than bolted on:

1. **`Graphics2D` needs the changes identified earlier.** Colour is currently
   baked into the backend at construction, so a UI with more than one colour is
   impossible. That is the real blocker; clipping and the framebuffer/present
   model turn out to be LVGL's job, not ours.
2. **LVGL must sit inside the layering already established** in
   `docs/StaTeX-layering.md`, not become a parallel universe. LVGL becomes
   another *surface behind* `Graphics2D`, exactly as the ILI9488 is. StaTeX
   stays ignorant of it, and the host tests keep working unchanged.

The payoff is that a formula renders once into an A8 buffer and LVGL scrolls
and composites it — on the GPU, on the U5.

---

## The key idea: render straight to A8

StaTeX's `blendCoverage` already emits 8-bit coverage. LVGL's
`LV_COLOR_FORMAT_A8` stores only alpha and takes its colour from
`style.img_recolor` — the docs describe it as intended for *"bitmaps similar to
fonts where the whole image is one colour that can be altered"*. That is
precisely our case.

So the backend becomes almost nothing: write coverage bytes into an A8
`lv_draw_buf`, and let LVGL apply the colour. Consequences:

- **1 byte/px** for cached formula bitmaps instead of 2–4.
- Colouring and compositing move to LVGL, i.e. **to the GPU** on the U5.
- Buffers can use `LV_DRAW_BUF_DEFINE_STATIC`, so no heap for canvas memory.

One caveat found while researching: LVGL's own `lv_canvas_draw_*` helpers
render only to RGB565/RGB888/XRGB8888/ARGB8888. We never use them — StaTeX
writes the A8 bytes itself and LVGL only *consumes* the buffer as an image.

---

## Design: `Graphics2D` v2

`lib/StaTeX/statex_graphics.h`

```cpp
/** Colour and opacity for one draw operation. */
struct Paint {
  u32 rgb = 0xFFFFFFu;  // 0xRRGGBB
  u8  opa = 255;        // 0 transparent .. 255 opaque
};

class Graphics2D {
 public:
  virtual void blendCoverage(int x, int y, int w, int h, const u8* cov,
                             Paint paint) = 0;
  virtual void fillRect(float x, float top, float w, float h,
                        Paint paint) = 0;   // was drawRule
 protected:
  ~Graphics2D() = default;
};
```

Four deliberate decisions, each with a reason:

**Paint is passed per call, not held as backend state.** No hidden state, and
it is reentrant. Bundling it in a struct means adding a blend mode later
changes no call sites. An A8 backend ignores `rgb` and folds `opa` into the
coverage.

**`fillRect` keeps FLOAT geometry — do not "unify" it to integers.** This was
my first instinct and it is wrong for two independent reasons:

- `tests/support/stx_render.cpp:36` records float `RulePlacement`s, which
  `tests/oracle/test_oracle_diff.cpp:461-476` and
  `tests/behaviour/test_radical.cpp:50` compare against MicroTeX at sub-pixel
  precision (`w=11.60 h=0.88 top=68.86`, tolerances `px/22` and `px/44`).
- Decisively: `stximg::Image::fillRect` (`tests/support/stx_image.cpp:32-37`)
  rounds the two *edges* independently, so the resulting width is
  position-dependent — and the oracle applies that **same** function to
  MicroTeX's rules (`tests/oracle/microtex_oracle.cpp:181-183`). That symmetry
  is what makes the image scores comparable at all. Rounding earlier, in the
  shared draw walk, would apply to StaTeX only and silently bias the
  differential.

It is also not an inconsistency: a coverage bitmap *is* a pixel grid, a rule
*is* an analytic rectangle. `blendCoverage` already receives ints because
`statex_draw.cpp:60,67,69` rounds the glyph origin; `fillRect` is the only
primitive that legitimately carries sub-pixel geometry across the seam.
Backends round, and each documents its own min-1px clamp.

**No clip rect.** LVGL clips the canvas inside a scroll view; backends
bounds-check their own surface, as `drawers/ili9488` already does. Adding one
would be speculation — per `docs/StaTeX-layering.md`, wait for a second
implementation to demonstrate the need.

**No surface bounds accessor.** Sizing a canvas comes from `RenderStats`, not
from the graphics interface.

`drawRule` → `fillRect` is a rename: TeX jargon does not belong in a generic
drawing interface.

---

## Where everything lands

```
vendor/lvgl/                     LVGL v9.5.0, verbatim (docs/tests trimmed)
backends/lvgl/                   Graphics2D -> A8 lv_draw_buf
ui/lv_statex/                    LVGL-side helper that owns canvas + render
targets/teensy41/src/lv_port_*   lv_conf.h, tick, flush_cb -> ILI9488
targets/simulator/               (later) LVGL host port for screen tests
```

Unchanged: `lib/StaTeX` knows nothing about LVGL; `drivers/ili9488` knows
nothing about either.

---

## Phases

Each phase ends in something verifiable, so a failure is localised.

### Phase 1 — `Graphics2D` v2 (no LVGL)

Self-contained refactor, proved by the existing suite. The blast radius is
small and fully enumerated: **4 implementations, 2 call sites.**

- `lib/StaTeX/statex_graphics.h:23,25` — add `Paint`, new signatures, rename.
- `lib/StaTeX/statex_draw.cpp:70,76` — the only two call sites in the repo.
  `drawTree` gains a `Paint` parameter (defaulted white) and threads it
  through; `Renderer::render` (`statex_render.cpp:59`) forwards it.
- The four implementations:
  - `backends/ili9488/ili9488_graphics.h:20` — drop the stored `_fg`, take
    colour from `Paint`. Keep its rounding and min-1px clamp exactly as is.
  - `targets/teensy41/src/main.cpp:34` `NullGraphics` — signature only.
  - `tests/support/stx_render.h:70` `ImageGraphics` — signature only; it
    ignores colour (8-bit greyscale ink) and must keep passing raw floats to
    `stximg::Image::fillRect`.
  - `tests/support/stx_record.h:24` `RecordingGraphics<N>` — signature only.
    Its 18 construction sites in `tests/unit/test_draw` and
    `tests/unit/test_render` are default-constructed and need no change.
- Docs that assert the old shape and go stale:
  `docs/StaTeX-layering.md:32-33,89,104`,
  `docs/StaTeX-requirements.md:238-245` (STX-API-03),
  `docs/StaTeX-dev-process.md:59-60`, `vendor/README.md:22-24`,
  and both target READMEs.

**Two grep traps** — `tex::Graphics2D` (`lib/MicroTeX/graphic/graphic.h:95`) is
a completely unrelated 30-method legacy interface, as are `Graphics2D_tft` and
`RecordingGraphics2D` (note: *not* `RecordingGraphics`). None of them change.

**Verify:** `ctest --test-dir build` — 22/23, only the known-red
`test_oracle_diff`, and its aggregate verdicts byte-identical to before
(same before/after capture technique used for the sampler change). Teensy image
rebuilds and reflashes; panel unchanged.

### Phase 2 — Vendor LVGL, make it build

- `vendor/lvgl/` at **v9.5.0**, pinned to commit
  `85aa60d18b3d5e5588d7b247abf90198f07c8a63` (verified reachable), trimmed of
  `docs/`, `tests/`, `examples/`, `demos/` and unused ports. The pin goes in
  `vendor/README.md` next to the other SDKs.
- Shared base `lv_conf.h` with per-target overrides. Memory:
  `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN` with a fixed `LV_MEM_SIZE` (and
  `LV_MEM_ADR` if we want it in a named region) — LVGL's TLSF pool is O(1) and
  needs no system `malloc`, which keeps the determinism story as close to
  StaTeX's as LVGL allows.
- CMake: `add_subdirectory` with `LV_CONF_PATH` set as a `CACHE STRING ... FORCE`
  *before* it (a known v9 pitfall if formatted wrongly).

**Verify:** LVGL compiles for the Teensy toolchain and links into a firmware
that does nothing with it yet. Report flash/RAM delta.

### Phase 3 — `backends/lvgl` A8 backend, tested on the host

- `backends/lvgl/statex_lv_canvas.h`: `statex::backends::LvCanvasGraphics`
  writing coverage into an A8 `lv_draw_buf`, honouring `Paint::opa`, ignoring
  `Paint::rgb`.
- Host test: render a corpus formula into an A8 buffer, dump via the existing
  `tests/support/stx_png.h`, compare against the `ImageGraphics` render.

**Verify:** the A8 path and the direct path agree on ink. This is the phase
that proves the idea *without hardware*, which is why it comes before the port.

### Phase 4 — Teensy LVGL port, on the panel

- `lv_tick` from `millis()`, `lv_timer_handler()` in `loop()`.
- `flush_cb` → `drivers::Ili9488`. **Colour format matters:** the ILI9488 is
  18-bit over SPI, so set LVGL to RGB888 and shift to 6-6-6 in the flush, which
  is the cheapest conversion. A partial draw buffer (~1/10 screen) keeps RAM
  small.
- No input: the Teensy rig has no touch wired (`TOUCH_CS` was never defined),
  so scrolling is exercised programmatically for now.
- `ui/lv_statex`: helper creating an `lv_image` over a static A8 buffer with
  the formula rendered into it and `img_recolor` set.
- A screen with a scrollable list containing several formulas.

**Verify:** flash and look. Compare against a host PNG of the same screen.

### Phase 5 — later, not now

Simulator target (LVGL SDL port, gives screen tests in `ctest` and a fast
iteration loop); U5 port with `LV_USE_DRAW_DMA2D` and the NeoChrom draw unit —
LVGL ships a reference port, `lv_port_riverdi_stm32u5`, with NeoChrom enabled
on Cortex-M33.

---

## What will NOT be accelerated, and why it matters

The GPU never accelerates **glyph rasterisation** — sampling the SDF is CPU
work on every target. What DMA2D/NeoChrom accelerate is compositing and
scrolling an *already-rendered* formula, which is the case that matters since a
scroll must not re-render. This is why the remaining sampler work still counts:
ink-box clipping is 21–35% fewer coverage pixels
(`docs/StaTeX-known-standins.md`).

Separately, `blendCoverage` with a fixed colour *is* a DMA2D A8→ARGB blend, so
on the U5 the backend can dispatch to DMA2D directly. That is ours to do, and
it is distinct from LVGL's own draw units.

---

## Mechanical details still to confirm (Phase 2 blockers, not design risks)

A second design pass on the LVGL wiring is still running; none of these change
the architecture above, but each needs to be right before Phase 2 lands:

- Exact `flush_cb` signature and `lv_display_flush_ready()` timing in v9.5
  (synchronous vs after a DMA completes).
- Draw-buffer sizing for partial rendering, and whether RGB888 or RGB565 is the
  better LVGL colour depth given the ILI9488's 18-bit bus — RGB888→666 is a
  shift, RGB565→666 an expand, and the draw buffer costs differ.
- Whether v9.5 renders an A8 image with `img_recolor` on **all** draw units, or
  only the software one. If only software, the U5 acceleration story needs the
  canvas in a different format and this is worth knowing before Phase 3.
- `LV_DRAW_BUF_DEFINE_STATIC` usage and the `lv_image`/`lv_canvas` lifecycle.
- Which `lv_conf.h` widgets/fonts to disable to keep flash small.

I will fold the answers into Phase 2/3 rather than guessing at them.

## Risks

- **Teensy flush bandwidth.** Full-screen RGB666 over 20 MHz SPI is ~184 ms.
  Partial buffers and small damage rects keep it usable; smooth full-screen
  scrolling is not achievable on this rig and is a U5 capability.
- **LVGL brings a heap.** A fixed TLSF pool, not system `malloc`, but
  `lib/StaTeX`'s no-allocation guarantee stops covering the application. This
  is the accepted cost of the LVGL decision and should be stated in the
  layering doc rather than left implicit.
- **`Paint` churn** touches every `Graphics2D` implementation at once. Phase 1
  is deliberately isolated so the oracle can prove nothing moved.

---

## Found while planning — deliberately NOT in this change

`backends/ili9488/ili9488_graphics.h:39` **truncates** the rule's `x` and `top`
(`static_cast<int>(x)`) while rounding its `w` and `h` (`+0.5f`). So a rule at
x=10.7 starts at pixel 10 but is `round(w)` wide, and the host path rounds both
edges instead. Device and host therefore disagree about rule placement by up to
a pixel.

That is a latent half-pixel bug, not a regression, and fixing it moves pixels —
so it wants its own change with its own before/after oracle capture, not to be
smuggled into an interface refactor. Recording it here so it is not lost.
