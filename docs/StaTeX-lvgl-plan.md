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

### Phase 2 — the A8 coverage backend, with **no LVGL at all**

The valuable realisation from the design pass: the compositing class needs
nothing from LVGL. It is a `Graphics2D` that writes into a `uint8_t*` with a
stride. So it comes first, is host-testable, and de-risks the whole idea before
a line of integration exists.

- `backends/statex_a8/statex_a8.h`: `statex::backends::CoverageGraphics`,
  writing into a caller-supplied byte grid. Clips like
  `Ili9488::blitCoverage` does. Composites with **`max`** (see Risks).
  `fillRect` becomes a `memset` of the rule rect, keeping the existing
  min-1px rounding.
- Host test: render corpus formulas through it, dump via
  `tests/support/stx_png.h`, and compare against the `ImageGraphics` render
  with the existing similarity engine.

**Verify:** the A8 grid and the direct render agree on ink, with no hardware
and no LVGL in the build.

### Phase 3 — Vendor LVGL, make it build

- `vendor/lvgl/` at **v9.5.0**, pinned to commit
  `85aa60d18b3d5e5588d7b247abf90198f07c8a63` (verified reachable). Prune
  top-level only — `tests/` (65 MB), `demos/`, `scripts/`, `docs/`,
  `examples/`, `env_support/cmsis-pack`, `libs/nema_gfx`. **Nothing under
  `src/`**, because `lvgl.h` includes every widget header unconditionally.
  ~180 MB → ~21 MB. Record the exclusions and that reason in
  `vendor/README.md`.
- `targets/teensy41/lv_conf.h` from the template — one file, in the target, not
  a shared base. The U5's config will diverge in more than colour depth and
  pool size (LTDC, DMA2D, direct-render mode, alignment), which is the same
  shape of divergence the layering doc describes for the two display stacks.
  Factor it out when the second one exists and shows the seam.
- Memory: `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN` with a fixed `LV_MEM_SIZE`
  — a static TLSF pool, no system `malloc`, and `lv_mem_monitor()` for a
  measured high-water mark, which is the same discipline as STX-MEM-02.
- CMake: set `LV_BUILD_CONF_PATH` (**not** `LV_CONF_PATH`) plus the three
  `CONFIG_LV_*` options `OFF` before `add_subdirectory`. LVGL must be added
  *after* the existing `add_compile_options`, so it inherits the Cortex-M7
  arch flags.

**Verify:** LVGL links into a firmware that only calls `lv_init()`. Run
`arm-none-eabi-size` — this is the measurement that says whether the DTCM
problem in Risks needs the linker-script fix.

### Phase 4 — Teensy LVGL port, on the panel

- `lv_tick_set_cb(millis)` — v9 has no `LV_TICK_CUSTOM`, and `millis` is an
  exact type match for `uint32_t(*)(void)`, so no ISR is needed.
  `lv_timer_handler_run_in_period(5)` in `loop()`.
- **`drivers::Ili9488` needs one new entry point:** `blitRgb888(x, y, w, h,
  const uint8_t* rgb)` — one address window then a single bus write. LVGL's
  flush hands over a raw pixel block, and neither `fillRect` nor
  `blitCoverage` can take one. The driver's contract stays in the panel's own
  terms; LVGL's byte order is the backend's problem.
- `backends/lvgl_ili9488/` holds the `flush_cb` — it knows LVGL and the driver
  and nothing about the board, so by the layering rule it is a backend, not
  target code.
- A partial draw buffer of ~1/10 screen (480×32 RGB888 = 46 KB) in `DMAMEM`,
  single-buffered because the flush is synchronous. Note `lv_area_t` bounds are
  **inclusive** — `w = x2 - x1 + 1`.
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

## Mechanics, verified against the v9.5.0 tag

**The A8 idea holds — confirmed in source, not inferred.**
`src/draw/sw/lv_draw_sw_img.c` has an explicit fast path:
`if(!transformed && !radius && cf == LV_COLOR_FORMAT_A8)` sets the A8 bytes as
`blend_dsc.mask_buf` and `draw_dsc->recolor` as `blend_dsc.color`, straight into
the RGB888 blend. And `lv_bin_decoder.c` deliberately excludes A8 from the
"alpha-only formats get expanded" branch, so a static A8 buffer is passed
through **zero-copy** — LVGL never allocates or takes ownership of it.

**Colour depth: `LV_COLOR_DEPTH 24`.** `lv_color_t` is unconditionally
`{blue, green, red}` in v9, so LVGL's RGB888 is B,G,R and the panel wants
R,G,B — the conversion is a three-byte reversal, in place. RGB565 would need a
*second* staging buffer, because 2→3 bytes cannot expand in place. The swap on
a 480×32 area is ~0.10 ms against 18.4 ms of SPI for the same block: 0.5% of
the flush.

**`lv_display_flush_ready()` synchronously**, at the end of `flush_cb`.
`TeensySpiBus::write` is a polled FIFO loop, not DMA, so the bytes are gone when
it returns. Calling it early lets LVGL render into a buffer still being
transmitted — intermittent tearing, miserable to diagnose.

### Corrections to earlier assumptions

- **`LV_CONF_PATH` is not the v9.5 CMake variable.** The v8-era
  `set(LV_CONF_PATH ... CACHE STRING "" FORCE)` is silently ignored. It is
  `LV_BUILD_CONF_PATH` (type `PATH`), from which LVGL derives the `LV_CONF_PATH`
  *compiler* define. Getting this wrong yields a working build against every
  default — 16-bit colour, 64 KB heap, all widgets on — with no error.
- **`vendor/lvgl/src/` cannot be pruned.** `lvgl.h` includes every widget and
  lib header unconditionally, with no `#if LV_USE_*` guard. Disabled features
  cost nothing anyway: the `.c` bodies are guarded and compile to empty objects.
  Pruning is top-level directories only (~180 MB → ~21 MB).
- **`CONFIG_LV_USE_THORVG_INTERNAL` must be `OFF`** or its source glob is empty
  and `add_library()` is a hard configure error.
- **`lv_conf_template.h` opens with `#if 0`.** Change it to `#if 1` first; the
  only symptom otherwise is a `#pragma message` in the build log.

### Still unverified

- **A8 + recolour on hardware draw units.** Verified for `LV_USE_DRAW_SW`,
  which is the only unit on the Teensy. Not audited for DMA2D / NemaGFX /
  VG-Lite. Each accelerator's `_evaluate` step is *supposed* to decline work it
  cannot do and fall back to software, but that is architecture, not evidence.
  Re-check when the U5 enables a hardware unit.
- **`LV_COLOR_DEPTH 24` render correctness.** Upstream CI builds 24-bit but
  only render-tests 32-bit. Test a gradient, an alpha-blended rect and the A8
  image early rather than trusting it.

I will fold the answers into Phase 2/3 rather than guessing at them.

## Risks

- **On the Teensy, `.rodata` lands in DTCM, not flash.**
  `targets/teensy41/imxrt1062_t41_xip.ld` routes `*(.rodata*)` into
  `.data > DTCM`, so every `const lv_obj_class_t`, style table, font glyph
  descriptor and kern table LVGL contributes is copied into RAM1 at boot. This
  is the least obvious cost of adding LVGL to *this* target. DTCM is 480 KB,
  96 KB of it already StaTeX's scratch, `bss` at 113 KB — there is headroom,
  but it must be measured with `arm-none-eabi-size` right after LVGL first
  links, not assumed. The targeted mitigation is one line, because LVGL marks
  the right array: `#define LV_ATTRIBUTE_LARGE_CONST __attribute__((section(".progmem")))`
  puts font bitmaps in flash, exactly as `STATEX_FLASH` already does for the
  SDF atlas.
- **Overlapping glyph boxes.** Compositing into an accumulator is *not* the
  same as writing straight to a panel. Glyph boxes overlap — an integral
  against its limits, italic kerning, a radical overbar — and the current
  ILI9488 backend never noticed because later writes simply overwrote earlier
  ones on the wire. The A8 writer must use **`max`**, not overwrite and not
  saturating add: `max` is idempotent, add darkens crossings into visible blobs.
- **`LV_DRAW_LAYER_SIMPLE_BUF_SIZE` defaults to 24 KB** and is allocated out of
  `LV_MEM_SIZE` the first time any widget needs a simple layer (any
  `style_opa < 255`, any transform). With a 32 KB pool that is a near-certain
  out-of-memory. Set it to 8 KB or avoid those styles.
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
