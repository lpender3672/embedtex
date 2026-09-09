# LVGL integration: implementation plan

The architecture — how apps, extensions, LVGL and StaTeX relate, why there is
no wrapper layer, why isolation is the MPU's job — is in
`StaTeX-app-architecture.md`. This is only the order of work and how each step
is verified.

Everything below was checked against the **v9.5.0** tag rather than recalled.

---

## What StaTeX has to grow

Two additions, both required by the `lv_font_t` integration *and* by any
scrolling transcript:

- **`Renderer::measure()`** — extents after layout, never entering the draw
  walk. `RenderStats` is already computed from the box-tree root before
  `drawTree` runs, so this is small. ~6% of the cost of a full render.
- **A glyph-metrics query** that does not sample the SDF, for `get_glyph_dsc`.

The trap that makes these necessary rather than convenient: `drawTree` calls
`renderGlyphCoverage` *before* `blendCoverage`, so measuring by rendering into
a null backend still pays the whole sampler. The two-pass measure in the Teensy
demo is currently paying 2× the sampler for one render, and its comment
claiming measurement "costs nothing but time" is wrong.

**`Graphics2D` stays colourless.** No `Paint`. See the architecture doc.

---

## Verified mechanics

**The A8 path holds — confirmed in source.** `src/draw/sw/lv_draw_sw_img.c` has
an explicit `if(!transformed && !radius && cf == LV_COLOR_FORMAT_A8)` branch
that uses the coverage bytes as `blend_dsc.mask_buf` and `draw_dsc->recolor` as
the fill colour. `lv_bin_decoder.c` excludes A8 from alpha expansion, so a
static A8 buffer is passed **zero-copy** — LVGL never allocates or takes
ownership.

**Colour depth: `LV_COLOR_DEPTH 24`.** `lv_color_t` is unconditionally
`{blue, green, red}` in v9, so LVGL's RGB888 is B,G,R and the panel wants
R,G,B — an in-place three-byte reversal. RGB565 would need a *second* staging
buffer, since 2→3 bytes cannot expand in place. The swap on a 480×32 area is
~0.10 ms against 18.4 ms of SPI for the same block.

**`lv_display_flush_ready()` synchronously**, at the end of `flush_cb`.
`TeensySpiBus::write` is a polled FIFO loop, not DMA, so the bytes are gone
when it returns. Calling it early lets LVGL render into a buffer still being
transmitted.

### Corrections to earlier assumptions

- **`LV_CONF_PATH` is not the v9.5 CMake variable.** The v8-era
  `set(LV_CONF_PATH ... CACHE STRING "" FORCE)` is silently ignored; it is
  `LV_BUILD_CONF_PATH` (type `PATH`). Getting it wrong yields a working build
  against every default — 16-bit colour, 64 KB heap, all widgets on — with no
  error.
- **`vendor/lvgl/src/` cannot be pruned.** `lvgl.h` includes every widget
  header unconditionally. Disabled features cost nothing anyway: the bodies are
  `#if`-guarded and compile to empty objects.
- **`CONFIG_LV_USE_THORVG_INTERNAL` must be `OFF`** or its empty source glob
  makes `add_library()` a hard configure error.
- **`lv_conf_template.h` opens with `#if 0`.** Change it to `#if 1` first; the
  only symptom otherwise is a `#pragma message` in the build log.

---

## Phases

### Phase 1 — measurement API

- `Renderer::measure()`; glyph-metrics query.
- Fix the Teensy demo's two-pass render to use it.

**Verify:** `ctest` unchanged (22/23, only the known-red oracle gate). Teensy
image rebuilds; measure the render-time drop from not sampling twice.

### Phase 2 — the A8 coverage backend, with **no LVGL at all**

The compositing class needs nothing from LVGL — it is a `Graphics2D` writing
into a `uint8_t*` with a stride. So it comes first and is host-testable.

- `backends/statex_a8/`: writes into a caller-supplied byte grid, clipping like
  `Ili9488::blitCoverage`. Composites with **`max`** — see Risks.
- Host test: render corpus formulas through it, dump via `tests/support/stx_png.h`,
  compare against the `ImageGraphics` render with the existing similarity engine.

**Verify:** the A8 grid and the direct render agree on ink, with no hardware and
no LVGL in the build.

### Phase 3 — vendor LVGL, make it build

- `vendor/lvgl/` at v9.5.0, pinned to `85aa60d18b3d5e5588d7b247abf90198f07c8a63`.
  Prune top-level only — `tests/` (65 MB), `demos/`, `scripts/`, `docs/`,
  `examples/`, `env_support/cmsis-pack`, `libs/nema_gfx`. ~180 MB → ~21 MB.
  Record the exclusions and the reason in `vendor/README.md`.
- `targets/teensy41/lv_conf.h` — one file in the target, not a shared base. The
  U5's will diverge in more than colour depth and pool size. Factor it out when
  the second one exists and shows the seam.
- `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN` with a fixed `LV_MEM_SIZE`: static
  TLSF pool, no system `malloc`, and `lv_mem_monitor()` for a measured
  high-water mark — the same discipline as STX-MEM-02.
- CMake: set `LV_BUILD_CONF_PATH` and the `CONFIG_LV_*` options *before*
  `add_subdirectory`, and add LVGL *after* the existing `add_compile_options`
  so it inherits the Cortex-M7 arch flags.

**Verify:** LVGL links into a firmware that only calls `lv_init()`. Run
`arm-none-eabi-size` — this is the measurement that says whether the DTCM
problem in Risks needs the linker-script fix.

### Phase 4 — display port, on the panel

- `lv_tick_set_cb(millis)` — v9 has no `LV_TICK_CUSTOM` and `millis` is an
  exact type match. `lv_timer_handler_run_in_period(5)` in `loop()`.
- **`drivers::Ili9488` needs one new entry point:** `blitRgb888(x, y, w, h, rgb)`
  — one address window, one bus write. LVGL's flush hands over a raw pixel
  block and neither existing method can take one.
- `backends/lvgl_ili9488/` holds the `flush_cb`: it knows LVGL and the driver
  and nothing about the board, so it is a backend, not target code.
- Partial draw buffer ~1/10 screen (480×32 RGB888 = 46 KB) in `DMAMEM`, single
  buffered because the flush is synchronous. `lv_area_t` bounds are
  **inclusive** — `w = x2 - x1 + 1`.

**Verify:** a red screen first. Blue means the byte swap is inverted; scrambled
means the address window or the inclusive-bounds arithmetic is wrong.

### Phase 5 — the font, and a formula on screen

- `lv_font_statex_create(em_px)` over the atlas — `get_glyph_dsc` from
  `GlyphRecord`, `get_glyph_bitmap` from the sampler into an A8 draw buf.
- A static A8 buffer via `lv_draw_buf_init` (not `LV_DRAW_BUF_DEFINE_STATIC`,
  whose designated initialisers are a GCC extension in C++), attached to an
  `lv_image` with `img_recolor` set.
- A screen with a label in the StaTeX font above a rendered formula.

**Verify:** the label and the formula are visibly the same typeface. Compare
against a host PNG of the same screen.

### Later

Simulator target; the U5 port with DMA2D and NeoChrom draw units; the
transcript widget from the architecture doc.

---

## Risks

- **On the Teensy, `.rodata` lands in DTCM, not flash.**
  `imxrt1062_t41_xip.ld` routes `*(.rodata*)` into `.data > DTCM`, so every
  LVGL class table, style table and font descriptor is copied into RAM1 at
  boot. DTCM is 480 KB with 96 KB already StaTeX's scratch and `bss` at 113 KB.
  There is headroom, but measure with `arm-none-eabi-size` right after LVGL
  first links. The targeted fix is one line, because LVGL marks the right
  array: `#define LV_ATTRIBUTE_LARGE_CONST __attribute__((section(".progmem")))`,
  mirroring what `STATEX_FLASH` already does for the SDF atlas.
- **Overlapping glyph boxes.** Compositing into an accumulator is not the same
  as writing to a panel. Glyph boxes overlap — an integral against its limits,
  italic kerning, a radical overbar — and the ILI9488 backend never noticed
  because later writes simply overwrote earlier ones on the wire. The A8 writer
  must use **`max`**: idempotent, where saturating add darkens crossings into
  visible blobs.
- **`LV_DRAW_LAYER_SIMPLE_BUF_SIZE` defaults to 24 KB**, allocated out of
  `LV_MEM_SIZE` the first time a widget needs a simple layer (any
  `style_opa < 255`, any transform). With a 32 KB pool that is a near-certain
  out-of-memory.
- **Teensy flush bandwidth.** Full-screen RGB666 over 20 MHz SPI is ~184 ms.
  Partial redraw is the operating model, not an optimisation; never invalidate
  the whole screen. Smooth full-screen scrolling is a U5 capability.
- **LVGL brings a heap.** A fixed TLSF pool, not system `malloc`, but
  `lib/StaTeX`'s no-allocation guarantee stops covering the application. This
  is the accepted cost of the LVGL decision.

## Still unverified

- **A8 + recolour on hardware draw units.** Verified for `LV_USE_DRAW_SW`, the
  only unit on the Teensy. Not audited for DMA2D / NemaGFX / VG-Lite. Each
  accelerator's `_evaluate` step is *supposed* to decline work it cannot do and
  fall back to software — architecture, not evidence. Re-check when the U5
  enables a hardware unit; if a hardware unit declines A8 recolour the
  compositing story changes shape.
- **`LV_COLOR_DEPTH 24` render correctness.** Upstream CI builds 24-bit but
  only render-tests 32-bit. Test a gradient, an alpha-blended rect and the A8
  image early.

---

## Found while planning — deliberately not in this work

`backends/ili9488/ili9488_graphics.h:39` truncates a rule's `x` and `top` while
rounding its `w` and `h`, and the host path rounds both edges instead. Device
and host disagree about rule placement by up to a pixel. A latent half-pixel
bug, not a regression; fixing it moves pixels, so it wants its own change with
its own before/after oracle capture.
