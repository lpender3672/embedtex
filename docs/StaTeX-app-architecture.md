# Application architecture: apps, extensions, and how they get drawn

`StaTeX-layering.md` answers *where code lives*. This answers *how an
application gets on screen*, and why every app looks the same without anyone
policing app authors.

The target is a calculator whose functionality arrives as apps — the main
calculator, a grapher, a circuit solver, a structural solver — some of which
may eventually be third-party. They must look like one product.

---

## The three integration points

There is deliberately **no wrapper layer over LVGL**. Apps call LVGL directly.
Consistency comes from three narrow mechanisms instead:

```
   apps ──────────────────▶ LVGL widgets, directly
     │                        │
     │                        ├── theme            colours, spacing, fonts
     │                        ├── lv_font_statex   ◀── StaTeX glyphs, below LVGL
     └──▶ lv_statex widget ───┘                        maths layout, beside LVGL
```

### 1. Below LVGL — StaTeX's glyphs are an `lv_font_t`

This is the mechanism that makes apps match, and it is the important one.

LVGL's font interface is two callbacks: `get_glyph_dsc` (metrics, no
rasterisation) and `get_glyph_bitmap` (fills an `lv_draw_buf_t`). A8 is a
supported glyph format, which is exactly what the SDF sampler already emits.
This is how `lv_freetype` and `lv_tiny_ttf` plug in; nothing exotic.

So `lv_font_statex_create(em_px)` yields an `lv_font_t*` backed by the atlas.
A grapher's axis labels, a solver's component values, and a formula in the
transcript are then the *same typeface from the same rasteriser*.

Consistency stops being a style guide and becomes a property of the font an app
cannot avoid. That is the whole point.

`GlyphRecord::advance` is already 1/256 em, so `get_glyph_dsc` is a table
lookup. Kerning returns 0 — a measured, recorded decision worth 0.031 em on one
corpus case (`StaTeX-known-standins.md`).

### 2. Beside LVGL — maths layout is a widget

Fractions, scripts and matrices are 2D box layout; LVGL's text engine handles
linear runs with line breaking. That cannot be a font. It is a widget rendering
into an A8 buffer, which LVGL then colours and composites.

### 3. Above LVGL — a theme and a thin vocabulary, not a layer

`lv_theme_t` is LVGL's own mechanism for display-wide colours, fonts and
spacing. Above it, a handful of composite helpers (a result row, a titled
panel) — convenience, not abstraction.

**What not to build:** a wrapper hiding LVGL behind our own widget API. LVGL
has ~30 widgets; wrapping them is enormous, the wrapper always leaks, and the
first app needing something unwrapped forces an escape hatch that defeats the
purpose.

---

## Colour belongs to the compositor

`Graphics2D` stays **colourless**. Everything StaTeX renders under LVGL lands in
an A8 buffer — a font glyph or a formula image — and A8 carries coverage, not
colour. LVGL applies colour through `img_recolor` and style.

An earlier draft of the LVGL plan added a per-call `Paint` because the direct
ILI9488 backend bakes its colour in at construction. That backend becomes a
bring-up and test path once LVGL owns the screen, so `Paint` would serve one
non-production consumer. Per this repo's own rule — *wait for the second
implementation to demonstrate the need* — it is not being added.

The one thing that could resurrect it is per-subterm colouring, e.g. a CAS
highlighting part of an expression. That wants investigating as sub-range
rendering into separate images before it justifies colour in the rasteriser.

---

## What StaTeX must expose

Both the font interface and any scrolling transcript need the same thing:
**metrics without rasterisation.**

- `Renderer::measure()` — extents after layout, never entering the draw walk.
  `RenderStats` is already computed from the box-tree root *before* `drawTree`
  runs, so this is a small addition, and it is roughly 6% of the cost of a full
  render.
- A glyph-metrics query that does not sample the SDF, for `get_glyph_dsc`.

Note the trap that makes this necessary rather than optional: `drawTree` calls
`renderGlyphCoverage` *before* it calls `blendCoverage`, so measuring by
rendering into a null backend still pays the entire sampler. Two independent
requirements landing on the same API is reasonable evidence the seam is right.

---

## Isolation: MPU, not TrustZone

Third-party apps must not be able to bring down the calculator. TrustZone is
the wrong tool for that, for two reasons.

**It gives one boundary, not N.** TrustZone-M splits the world in two and
protects Secure from Non-secure, one way. Five extensions all live in the same
Non-secure world and can corrupt each other freely — which is the actual
concern.

**It would enlarge the trusted computing base.** TrustZone's value is a small,
auditable Secure world. LVGL is hundreds of thousands of lines of third-party
C. Putting the UI in Secure makes every LVGL bug a bug in the most privileged
code, and forces a veneer crossing plus SAU-configured shared buffers on every
app→UI call.

**Use the MPU.** The Cortex-M33 has one per security state, reprogrammable on
context switch. Each app is a task with its own RAM and code regions and
nothing else; calls into the base go through SVC; a misbehaving app takes a
MemManage fault and is killed or restarted on its own. Since the U5 is a Zephyr
target, `CONFIG_USERSPACE` is this mechanism already built — memory domains,
isolated threads, system calls.

**Secure world holds keys, not pixels:** secure boot, firmware-update
verification, key material. That is what it is for.

**StaTeX does not cross any boundary.** It is first-party, a pure function with
no heap and no syscalls, deterministic by construction. It lives in the base
beside LVGL. Crossing a veneer per glyph would be absurd and there is nothing
to protect it from.

### The discipline to adopt now, mechanism later

Building a sandboxed plugin ABI before there is one working screen is a large
speculative investment. But one discipline costs nothing today and preserves
the option:

> Apps never hold an LVGL pointer across a call boundary, and never receive a
> C++ reference from the base. Handle-based, data-in/data-out, coarse-grained.

If that holds from the start, MPU isolation can be added later without a
redesign. If it does not, it cannot. So `Graphics2D` is internal to the base
and is *not* part of the app-facing surface: what an app gets is closer to
"here is a region, render this into it", which is also what makes every app
look the same.

### The failure that actually happens

Memory corruption is the dramatic case. The mundane one is **hogging** — an app
that loops forever, or drains the shared LVGL pool, bricks the UI whether or
not the MPU is on. Two cheap policies matter more day to day than any isolation
hardware:

- **Per-app memory pools**, not one shared LVGL heap, so exhaustion is contained.
- **A time budget**: apps are called with a deadline and expected to yield.
  Cooperative, but it makes a runaway visible and recoverable.

---

## Consuming StaTeX: the long-output case

The calculator's main output is a long scrolling transcript of CAS results.
That case sets the shape for every app that shows a list of rendered content.

**Store source, not bitmaps.** A LaTeX string is a few hundred bytes; a
448×96 A8 tile is 43 KB. A bounded ring of source text is affordable and fits
the project's static-allocation thesis; a ring of rendered tiles is not.

**Virtualise both buffers and objects.** A small fixed pool of A8 buffers, and
of `lv_obj`s, recycled as entries scroll in and out — each LVGL object costs
150–300 B of heap, so a thousand children is 300 KB before anything is drawn.

**Measure at push time, cache the extents.** The scroll container needs every
entry's height, including offscreen ones. That is only viable because measuring
is ~6% of rendering — hence `measure()` above.

**Every entry needs a fallback.** The command set is `\frac`, `\sqrt`,
matrices, four `\math*` faces and **14 symbols**. A CAS emits `\ln`, `\sin`,
`\pm`, `\partial`, `\rightarrow` constantly. So `push()` records whether an
entry is renderable, and the view falls back to a plain label showing the raw
LaTeX. A calculator that silently drops a result is worse than one showing
unrendered source.

**Expanding the symbol table is a prerequisite**, not a follow-up — and it is
genfont work, not parser work.
