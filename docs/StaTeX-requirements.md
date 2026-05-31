# StaTeX — High-Level Requirements

**StaTeX** is a statically-allocated, bounded, deterministic TeX *math* renderer for
embedded targets. It is a ground-up reimplementation of the layout algorithms vendored
today under `lib/MicroTeX/`, discarding that code's hosted-OS assumptions (heap ownership,
exceptions, RTTI, runtime macro expansion, runtime resource parsing) while keeping its
TeX box-model math.

This document is the high-level requirement set. Each requirement references the existing
dynamic MicroTeX code it derives from or replaces, so the rewrite is traceable. Requirement
IDs are stable; detailed (low-level) requirements will refine these later.

---

## 1. Scope and definitions

- **Render request:** one input string → one drawn result, or a defined refusal.
- **Scratch arena:** a single, statically reserved memory region from which all
  per-request data is bump-allocated and freed wholesale.
- **Handle:** a `u16` index into a pool, used in place of a pointer.
- **POD node:** a trivially-destructible struct with no owning members.
- **Refusal:** a render request that completes by returning "not rendered" with a defined
  reason, without side effects on subsequent requests.

### In scope
A fixed, finite subset of math-mode TeX sufficient for displaying equations of the kind in
`src/main.cpp:65` (fractions, sub/superscripts, matrices, roots, accents, a fixed symbol set).

### Explicitly out of scope (dropped from MicroTeX)
- User-defined macros / environments.
- Runtime resource (XML/font-metric) parsing.
- Runtime font loading and file I/O on the render path.
- Arbitrary, open-grammar LaTeX.

---

## 2. Memory management

**STX-MEM-01 — No dynamic allocation after init.**
StaTeX shall perform no heap allocation (`new`, `malloc`, `make_shared`, STL container
growth) during a render request.
*Replaces:* the `sptr<T> = std::shared_ptr<T>` ownership model (`lib/MicroTeX/utils/utils.h:25`,
`sptrOf` at `:28`) used at 794 sites, and the `clone()`-via-`new` idiom
(`lib/MicroTeX/atom/atom.h:78`, macro `__decl_clone`).

**STX-MEM-02 — Single static scratch arena.**
All per-request working data shall be bump-allocated from one statically reserved region of
fixed size, decided at link time.
*Replaces:* heap-resident trees — `Formula::_root` / `Formula::_middle`
(`lib/MicroTeX/core/formula.h:68-70`), `BoxGroup::_children`
(`lib/MicroTeX/box/box.h:107`), and `ArrayFormula::_array`
(`lib/MicroTeX/core/formula.h:159-166`).

**STX-MEM-03 — Graceful exhaustion ("refuse, don't fail").**
When the arena cannot satisfy an allocation, the allocator shall return a null handle; the
request shall unwind to a refusal and reset the arena. Exhaustion shall never crash, corrupt
state, or leak. Arena size determines maximum formula complexity; it is a capability bound,
not a safety bound.
*Replaces:* unbounded growth of the above structures; aligns with the existing
`if (render) … else "Failed to parse"` contract at `src/main.cpp:78`.

**STX-MEM-04 — O(1) wholesale reset.**
Freeing the arena shall be a single pointer reset with no per-object destruction.
*Implies:* nodes are POD (see STX-DAT-01).

**STX-MEM-05 — Phase-scoped lifetimes.**
The intermediate atom representation shall be released before the draw phase, so that peak
scratch use is the larger of (atom tree) or (box tree), not their sum.
*Derives from:* the two-stage pipeline `Atom → createBox → Box`
(`lib/MicroTeX/atom/atom.h` `createBox(Environment&)` → `lib/MicroTeX/box/box.h`).

**STX-MEM-06 — No fragmentation.**
The allocator shall be bump/pool only; no general-purpose free of individual objects on the
render path.

---

## 3. Bounded execution (the agreed approach (b))

**STX-EXE-01 — No unbounded recursion on the call stack.**
Parsing, layout, and drawing shall not recurse on the C++ call stack proportionally to input
nesting. They shall iterate over an explicit work-stack.
*Replaces:* mutual recursion `parse()` → `getArgument()` → `Formula`/`parse()`
(`lib/MicroTeX/core/parser.cpp:856`, `:664`, `getScripts` `:594`, `processCommands` `:576`),
and recursive `createBox` / `draw` / `descendants` over the box tree
(`lib/MicroTeX/box/box.h:78,89,147`).

**STX-EXE-02 — Explicit work-stack in the arena.**
The work-stack used by STX-EXE-01 shall itself be allocated from the scratch arena, so that
depth exhaustion and node exhaustion unify into the single refusal path of STX-MEM-03.

**STX-EXE-03 — Bounded C++ call depth.**
Maximum C++ call-stack depth shall be a constant independent of input, verifiable against the
target's reserved stack.

**STX-EXE-04 — Bounded work (optional WCET guard).**
Where a frame/latency deadline applies, the work-stack loop shall honor a maximum-iteration
budget; budget-exceeded shall route to the same refusal path as STX-MEM-03.

---

## 4. Error handling and determinism

**STX-ERR-01 — No C++ exceptions.**
StaTeX shall be built with exceptions disabled and contain no `throw`/`try`/`catch`.
*Replaces:* the `ex_tex` hierarchy (`lib/MicroTeX/utils/exceptions.h`), 105 `throw` sites,
and `-fexceptions` (`platformio.ini:30`).

**STX-ERR-02 — Status-return error model.**
Fallible operations shall return an explicit status / `expected`-style result. A single
refusal reason shall propagate to the public API.
*Replaces:* exception-based control flow in the parser (e.g. `getDollarGroup`, `getGroup`,
`getArgument` documented to "throw ex_parse", `lib/MicroTeX/core/parser.h:258-299`).

**STX-ERR-03 — Defined behavior for malformed input.**
Any input that is malformed, too large, too deeply nested, or uses an unsupported command
shall produce a refusal, never undefined behavior.

---

## 5. Data model

**STX-DAT-01 — POD, handle-based nodes.**
Atom and Box nodes shall be trivially-destructible structs. Child references shall be `u16`
handles or arena spans `{u16 offset, u16 count}`; no node shall embed an owning pointer or a
growable container.
*Replaces:* `sptr<Atom>` / `sptr<Box>` members, `std::list<sptr<MiddleAtom>>`
(`lib/MicroTeX/core/formula.h:68`), and `std::vector<sptr<Box>> _children`
(`lib/MicroTeX/box/box.h:107`).

**STX-DAT-02 — Strings as arena slices.**
Per-request text shall be stored as `{offset, len}` slices into an arena char buffer, not as
`std::string`/`std::wstring`.
*Replaces:* ~713 `std::string` uses and the mutable `std::wstring _latex`
(`lib/MicroTeX/core/parser.h:27`); also `tex::RES_BASE` (`lib/MicroTeX/common.h:38`).

**STX-DAT-03 — Fixed-capacity collections.**
Where a runtime collection is unavoidable, it shall be a fixed-capacity container backed by
the arena (no reallocation).
*Replaces:* runtime `std::vector`/`std::map` instances, e.g. `ArrayFormula::_rowSpecifiers` /
`_cellSpecifiers` and `Formula::_xmlMap` (`lib/MicroTeX/core/formula.h:54,165-166`).

**STX-DAT-04 — Bounded matrix grid.**
Array/matrix layout shall use a fixed `[MAX_ROW][MAX_COL]` grid (sized by the arena), refusing
larger inputs.
*Replaces:* `std::vector<std::vector<sptr<Atom>>> _array` (`lib/MicroTeX/core/formula.h:164`).

---

## 6. Type dispatch (no RTTI)

**STX-TYP-01 — No RTTI.**
StaTeX shall be built with `-fno-rtti` and contain no `dynamic_cast`/`typeid`.
*Replaces:* 51 `dynamic_cast` sites, concentrated in layout logic
(`lib/MicroTeX/atom/atom_basic.cpp:51,59,140,265,…`), and the current removal of `-fno-rtti`
(`platformio.ini:35`).

**STX-TYP-02 — Tag-based dispatch.**
Node class identity shall be carried as an explicit `enum` kind tag; type-dependent layout
shall branch on the tag (or via a visitor), then `static_cast`.
*Note:* distinct from the existing semantic `AtomType` (spacing class,
`lib/MicroTeX/atom/atom.h:27`), which is retained for its original purpose.

---

## 7. Resources

**STX-RES-01 — All static data in flash.**
Symbol tables, font metrics, and predefined-formula tables shall be `constexpr`/PROGMEM data
linked into flash, looked up by binary search; none shall be parsed or built at runtime. This
includes **per-glyph layout metrics** (advance, bearing, em-height, em-depth, italic
correction) and global **font parameters** (axis height, x-height, default rule thickness,
superscript/subscript shifts), which layout (STX-DAT-03) consumes in place of hard-coded
constants. See STX-FNT-* (§12) for the glyph record.
*Replaces:* init-time `std::map` dictionaries — `Formula::_symbol*Mappings` /
`_predefinedTeXFormulas` (`lib/MicroTeX/core/formula.h:59-66`), `DefaultTeXFont` maps
(`lib/MicroTeX/fonts/fonts.h:34-37`), `MacroInfo::_commands` (`lib/MicroTeX/core/macro.h:95`),
and the script maps (`lib/MicroTeX/core/parser.h:57-58`) — together with the generated
`lib/MicroTeX/res/builtin/*.res.cpp` tables, which become the flash source of truth.

**STX-RES-02 — No runtime resource parsing.**
The XML/font-metric parsers shall not exist on the target.
*Replaces:* `res/parser/font_parser.*` and `res/parser/formula_parser.*`, and the file reads in
`LaTeX::init` (`lib/MicroTeX/latex.cpp:77,85`, `fopen`).

**STX-RES-03 — Glyphs in flash; no render-path file I/O.**
Glyphs shall be served from flash as single-resolution **signed-distance fields** (SDF),
rendered to arbitrary size by bounded sampling + thresholding (STX-FNT-*, §12). No
SD/filesystem access shall occur during a render request.
*Replaces:* runtime TTF loading `OpenFontRender::loadFont` (`lib/MicroTeX/graphic/graphic_tft.cpp:130`)
and `SD.begin`/file access on the active path (`src/main.cpp:24`).
*Note:* supersedes the initial fixed-bitmap seed atlas (`lib/StaTeX/statex_atlas.*`); the SDF
store and its sampler are specified in §12.

---

## 8. Language scope (dropped features)

**STX-LNG-01 — No user-defined macros or environments.**
`\newcommand`, `\renewcommand`, `\def`, `\newenvironment` shall be unsupported.
*Replaces / removes:* the textual-substitution macro expander — `NewCommandMacro`
(`lib/MicroTeX/core/macro.h:21-91`, `_codes`/`_replacements`), and the expansion passes
`preprocessNewCmd` / `inflateNewCmd` / `inflateEnv`
(`lib/MicroTeX/core/parser.h:94-98`, `parser.cpp:756`). This is the change that makes a static
bound on tree size possible at all.

**STX-LNG-02 — Closed, finite command set.**
Only commands present in the flash tables (STX-RES-01) shall be recognized; unknown commands
refuse (STX-ERR-03).
*Replaces:* the open `MacroInfo`/`PreDefMacro` dispatch (`lib/MicroTeX/core/macro.h:93-173`).

**STX-LNG-03 — No in-place input mutation.**
The parser shall treat input as read-only and shall not rewrite the input buffer.
*Replaces:* `TeXParser::insert(beg, end, formula)` mutation of `_latex`
(`lib/MicroTeX/core/parser.h:71`) used by macro expansion.

**STX-LNG-04 — No runtime registration.**
Runtime registration of fonts, alphabets, colors, or symbol mappings shall be unsupported;
such data is fixed in flash.
*Replaces:* `Formula::addSymbolMappings` / `_externalFontMap`
(`lib/MicroTeX/core/formula.h:66,147`) and `_registeredAlphabets`
(`lib/MicroTeX/fonts/fonts.h:54`).

---

## 9. Public interface

**STX-API-01 — Caller-owned output, no hidden ownership.**
The render entry point shall write its result into caller-provided storage (or the arena with
explicit lifetime); it shall not return a raw owning pointer.
*Replaces:* `LaTeX::parse(...) -> TeXRender*` (`lib/MicroTeX/latex.h:52`), whose result is
leaked at the call site (`src/main.cpp:65`, no `delete`).

**STX-API-02 — Reentrancy contract.**
Each render request shall be self-contained: begin → build → layout → draw → reset, with no
state carried between requests except immutable flash data.

**STX-API-03 — Graphics backend abstraction.**
The drawing surface shall remain an abstract interface implemented per target. It shall expose
a **coverage/alpha primitive** (blend a glyph coverage span/rect with a colour) in addition to
a filled-rect primitive, so antialiased glyphs (STX-FNT-03) reach the device. StaTeX owns the
SDF→coverage sampling (STX-FNT-02) so the backend stays heap-free and trivially portable;
the backend only blends what it is handed.
*Derives from:* `Graphics2D` / `Graphics2D_tft` (`lib/MicroTeX/graphic/graphic.h`,
`lib/MicroTeX/graphic/graphic_tft.*`), kept but freed of heap use.

---

## 10. Build and toolchain

**STX-BLD-01 — Safety-aligned flags.**
The library shall build clean with exceptions and RTTI disabled
(`-fno-exceptions -fno-rtti`), inverting the current `platformio.ini:30,35` settings.

**STX-BLD-02 — No heap dependency at link.**
The final image shall not pull in `malloc`/`new` via StaTeX code; any such symbol from the
toolchain runtime shall be unreachable on the render path.

**STX-BLD-03 — Static analyzability.**
Code shall be structured to permit MISRA-C++ / AUTOSAR-style static checking: bounded loops,
single-exit error propagation, no `std::function`-style type-erased heap closures
(*cf.* `binIndexOf`'s `std::function` parameter, `lib/MicroTeX/utils/utils.h:50-54`).

---

## 11. Traceability summary

| Requirement area | Existing dynamic construct | Reference |
|---|---|---|
| Ownership → arena handles | `sptr` / `make_shared` (794×), `clone()`=`new` | `utils/utils.h:25`, `atom/atom.h:78` |
| Trees → POD pools | `_root`,`_middle`,`_children`,`_array` | `core/formula.h:68,164`, `box/box.h:107` |
| Recursion → work-stack | `parse`/`getArgument`/`createBox`/`draw` | `core/parser.cpp:664,856`, `box/box.h:78` |
| Exceptions → status | `ex_tex` tree, 105 `throw`, `-fexceptions` | `utils/exceptions.h`, `platformio.ini:30` |
| RTTI → tags | 51 `dynamic_cast`, `-fno-rtti` removed | `atom/atom_basic.cpp:51+`, `platformio.ini:35` |
| Runtime tables → flash | static `std::map` dictionaries | `core/formula.h:59-66`, `fonts/fonts.h:34-37` |
| Runtime parse/IO → none | XML/font parsers, `fopen`, SD, `loadFont` | `latex.cpp:77,85`, `graphic/graphic_tft.cpp:130` |
| Macros → dropped | `NewCommandMacro`, `insert()` expansion | `core/macro.h:21`, `core/parser.h:71` |
| Owning return → caller storage | `LaTeX::parse -> TeXRender*` (leaked) | `latex.h:52`, `src/main.cpp:65` |
| Fonts → SDF + metric tables in flash | runtime TTF rasterization (OpenFontRender) | `graphic/graphic_tft.cpp:130`; see §12 |

---

## 12. Fonts and glyphs

Glyph *images* and glyph *metrics* are distinct datasets, both `constexpr` in internal flash
(the Teensy 4.1's ~8 MB is ample; no external flash chip is required). Images use signed
distance fields so a single stored resolution scales smoothly to any size while keeping the
runtime fully static and bounded — no outline rasterizer, no heap (the reason MicroTeX's
runtime TTF path was dropped). The full math face set is in scope (roman, math-italic, bold,
symbol, blackboard).

**STX-FNT-01 — Flash glyph record.**
Each glyph shall be a `constexpr` record carrying: a single-resolution **SDF bitmap** (N×N,
8-bit distance), placement (bearing, advance), and the layout metrics of STX-RES-01
(em-height, em-depth, italic correction). Records are keyed by (face, codepoint) and looked up
by binary search. No glyph data is parsed or built at runtime (with STX-RES-02).

**STX-FNT-02 — Bounded SDF rasterization.**
Rendering a glyph shall sample the SDF (bilinear) and threshold to coverage, writing into a
fixed scratch coverage buffer of at most G×G pixels. A glyph whose target size exceeds G×G
shall refuse (with STX-MEM-03), never grow memory. Sampling shall use no heap and complete in
bounded, input-independent steps per output pixel (with STX-EXE-03).

**STX-FNT-03 — Antialiasing.**
Edge coverage shall be derived from the sampled distance as a ~1-pixel linear ramp, yielding
smooth edges at any scale (replacing the blocky nearest-neighbour scaling of the seed atlas).

**STX-FNT-04 — Offline atlas generation.**
The SDF bitmaps, metrics, and font parameters shall be produced by a **host-side tool** from
source fonts and emitted as checked-in `constexpr` tables. The device shall never rasterize
from outlines nor read font files (reinforces STX-RES-02). The generator is off-device tooling,
analogous to the differential oracle of the dev process.

**STX-FNT-05 — Faces and style selection.**
The glyph store shall carry a **face dimension** (roman, math-italic, bold, symbol,
blackboard). Math style commands (`\mathrm`, `\mathit`, `\mathbf`, `\mathsf`, `\mathbb`, …)
shall select the face for their argument; an unavailable (face, glyph) pair refuses
(STX-LNG-02 / STX-ERR-03). Style selection is part of the closed command set, not runtime
registration (STX-LNG-04).

*Testability:* SDF sampling, AA edge ramp, scale invariance, and metric-driven layout are all
host-testable — the recording backend (dev-process §3) captures coverage blits, asserted
against goldens; the MicroTeX differential oracle (§4) still applies to the supported subset.
