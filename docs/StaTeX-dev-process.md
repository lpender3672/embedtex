# StaTeX — Development Process (Test-Driven)

Companion to `StaTeX-requirements.md`. Defines how StaTeX is built: stub-first,
test-driven, host-first, bottom-up. Each requirement (`STX-*`) becomes one or more failing
tests before any implementation exists.

---

## 1. The cycle

For every unit of work:

1. **Stub** — declare the real public surface; bodies return a defined default
   (a refusal / null handle / `false`), enough to compile and link.
2. **Test (red)** — write tests asserting the *intended* behavior against the stub. They must
   fail for the right reason (wrong answer, not a link error).
3. **Unstub** — implement until the tests compile-link-run.
4. **Pass (green)** — all tests green; then refactor under green.

Rule: no production line is written without a red test demanding it. A stub returning a
refusal is the canonical starting state — it also happens to be the *safe* state (STX-ERR-03),
so partial builds are always well-behaved.

---

## 2. Host-first — why it works here

The requirements removed every barrier to native testing:

| Removed | Requirement | Consequence for testing |
|---|---|---|
| Heap | STX-MEM-01 | arena is a plain byte array on host; fully inspectable |
| Exceptions | STX-ERR-01 | status returns are trivially assertable |
| RTTI | STX-TYP-01 | kind-tags are plain enums, comparable in tests |
| File I/O / SD | STX-RES-02/03 | flash tables are `constexpr` arrays; no I/O to mock |
| Hardware draw | STX-API-03 | inject a recording mock backend |

⇒ The core (arena, parser, layout, draw-op generation) compiles and runs **natively** with no
Arduino, no Teensy, no display. Only glyph *pixels* and the real TFT need hardware (§8).

**Infrastructure:** add a `[env:native]` PlatformIO target (`platform = native`) running the
built-in Unity framework (or GoogleTest), tests under `test/`. CI = `pio test -e native`.
Determinism (no heap, no clock dependence) makes every test bit-reproducible.

---

## 3. Two test fixtures you build first (they are the assertion mechanism)

Most layer tests need a way to *observe* an internal tree without a screen:

- **Tree serializer** — dumps an Atom or Box tree (from arena handles) to a canonical text
  form: kind-tag, key fields, children. Parser/layout tests assert `dump(tree) == golden`.
  This is the substitute for inspecting `sptr` graphs.
- **Recording backend** — a `Graphics2D` implementation (per STX-API-03, replacing
  `Graphics2D_tft`, `lib/MicroTeX/graphic/graphic_tft.*`) that records each draw call
  (glyph id, x, y, color) into a list instead of pushing pixels. Draw tests assert the
  op-list against a golden. Op-lists are far less brittle than pixel diffs.

Both are themselves TDD'd before the layers that depend on them.

---

## 4. The reference oracle (differential testing)

The dynamic MicroTeX still builds on native, so it serves as a **reference oracle** for the
subset StaTeX supports: feed the same formula to both in one test process and compare. This
turns golden generation from hand-computed TeX metrics into "ask the reference" — the biggest
single accelerator for Phase 5 (layout) and Phase 7 (integration).

**Wiring.** A `[env:native_oracle]` (or a test tag) links the legacy `lib/MicroTeX/` *for
tests only* — it is never shipped (STX-BLD-02). A small adapter runs MicroTeX's
`createBox`/draw through the same recording backend and serializer used for StaTeX, so both
sides emit comparable op-lists / tree dumps.

**Three preconditions for the comparison to mean anything:**
- **Same metric source.** StaTeX's flash tables (STX-RES-01) must be generated from the very
  values MicroTeX uses (`lib/MicroTeX/res/builtin/*.res.cpp`, `lib/MicroTeX/res/font/*.def.cpp`).
  Otherwise divergence is just different input numbers, not an algorithm bug.
- **Tolerance, not equality.** Metrics are floats; the two codepaths round differently. Compare
  positions/metrics within a sub-pixel epsilon.
- **Same backend + serializer.** Both libraries must funnel through the §3 fixtures so the
  comparison is apples-to-apples.

**Two modes:**
- **Generate-then-snapshot** — mint goldens from MicroTeX once, freeze them in the repo. This
  is the regression guard; it stays stable even if the legacy tree later changes.
- **Live differential + fuzz** — run both libraries every test on random valid formulas drawn
  from the closed grammar (STX-LNG-02) and assert agreement within tolerance. Cheap breadth on
  the layout math, and a strong probe for the work-stack edge cases of STX-EXE-01.

**What the oracle does *not* cover (no MicroTeX counterpart):** refusal on malformed/oversized
input, arena exhaustion, and depth bounding (STX-MEM-03, STX-ERR-03, STX-EXE-03). MicroTeX
would render, throw, or grow the heap instead of refusing. The oracle validates *"did I port
the math faithfully"*; the bounding/refusal behavior — the part that earns the safety claim —
is covered only by the hand-written intent tests in §6. Keep the two sets distinct.

---

## 5. Build order (bottom-up, dependency-driven)

Each phase: the stub surface, representative red tests, then unstub. Later phases depend only
on earlier ones.

### Phase 0 — Harness
Native env + Unity wired; a `PASS` smoke test; serializer and recording-backend skeletons
stubbed. *Green when `pio test -e native` runs and the smoke test passes.*

### Phase 1 — Arena + handle pools  (STX-MEM-01/02/03/04/06, STX-EXE-02)
*Stub:* `alloc()` always returns null; `reset()` no-op.
*Red tests:* alloc returns distinct handles; alloc past capacity returns null (not crash);
`reset()` makes prior space reusable; high-water mark reported; alignment honored.
*Unstub:* bump allocator over a fixed `u8[]`.
> This is the foundation — exhaustion-returns-null is tested before anything can allocate.

### Phase 2 — POD nodes + tree builder + serializer  (STX-DAT-01/02)
*Stub:* node structs defined; builder returns null handles; serializer returns `""`.
*Red tests:* build a 3-node tree by hand, `dump()` equals expected canonical string;
trivially-destructible static-assert on every node type; string-slice round-trips.
*Unstub:* node layouts, child-span helpers, serializer walk.

### Phase 3 — Flash tables  (STX-RES-01, STX-LNG-02)
*Stub:* symbol/metric lookups return "not found".
*Red tests:* known symbol resolves to expected glyph/metrics; unknown symbol returns
not-found (→ refusal); table is sorted (static check) so binary search is valid.
*Unstub:* `constexpr` tables generated from `lib/MicroTeX/res/builtin/*.res.cpp` + bin search.

### Phase 4 — Parser: string → atom tree  (STX-EXE-01/02/03, STX-ERR-02/03, STX-LNG-03)
*Stub:* `parse()` returns refusal.
*Red tests (happy):* `x`, `x^2`, `\frac{a}{b}`, `\sqrt{x}`, a 2×2 matrix → expected
`dump()`. *Red tests (refusal):* unbalanced `{`, unknown command, nesting beyond what the
arena work-stack allows, oversized matrix → all refuse cleanly, arena resets, **no UB**.
*Unstub:* iterative parser over an explicit work-stack (STX-EXE-01), no recursion, no input
mutation. *Replaces* `lib/MicroTeX/core/parser.cpp` recursion (`:664,856`) and macro passes.

### Phase 5 — Layout: atom tree → box tree  (STX-MEM-05, STX-DAT-03/04)
*Stub:* `layout()` returns refusal.
*Red tests:* single glyph box has expected width/height/depth from flash metrics; fraction
stacks numerator/denominator with rule between; superscript shift matches golden numerics;
atom arena is released before draw (STX-MEM-05) — assert via high-water mark. Goldens here are
generated from the MicroTeX oracle (§4) and frozen, not hand-computed.
*Unstub:* port the TeX box-model math from `lib/MicroTeX` `createBox`/`box_factory`, on
handles. Tag-dispatch (STX-TYP-02) replaces the 51 `dynamic_cast` decisions. The live
differential mode (§4) is the primary check that the port matches the reference.

### Phase 6 — Draw: box tree → op-list  (STX-API-03)
*Stub:* `draw()` records nothing.
*Red tests:* `\frac{a}{b}` produces the expected ordered op-list (glyphs + rule at expected
coords) via the recording backend; iterative traversal, bounded call depth (STX-EXE-03).
*Unstub:* iterative box-tree walk emitting draw ops.

### Phase 7 — Integration + safety properties  (STX-MEM-03, STX-API-01/02)
*Red tests:*
- **Golden formulas** — the `src/main.cpp:65` matrix and a fixture set: full pipeline
  string→op-list matches goldens (minted from the MicroTeX oracle, §4, then frozen).
- **Differential fuzz** — random valid formulas from the closed grammar: StaTeX vs the live
  MicroTeX oracle agree within tolerance (§4), or StaTeX refuses cleanly.
- **Exhaustion** — same input with a deliberately tiny arena ⇒ refusal, clean reset, next
  request with full arena succeeds (proves graceful degradation + no residue).
- **Reentrancy** — N sequential renders leave identical arena state each time (STX-API-02);
  no carried state except flash.
- **Memory budget** — assert each golden's high-water mark ≤ a recorded bound (regression
  guard on the capability bound).
- **Caller-owned output** — result lands in caller storage; no leak surrogate
  (replaces leaked `TeXRender*`, `latex.h:52` / `main.cpp:65`).

### Phase 8 — Target bring-up (the only on-hardware part)
Glyph atlas in flash (STX-RES-03) + real TFT `Graphics2D` backend. The recording-backend
goldens from Phase 6/7 are the reference; on-target you verify pixels match for the fixture
set. Everything logical was already proven on host.

---

## 6. Cross-cutting test categories (apply at every phase)

- **Refusal tests** — malformed / oversized / too-deep input ⇒ defined refusal, never UB
  (STX-ERR-03). Write these alongside happy-path tests, not after.
- **Arena invariants** — after any operation, `reset()` returns to a clean baseline; no
  allocation outside the arena (assert allocator call counts).
- **POD/static guards** — `static_assert(std::is_trivially_destructible_v<Node>)` per type
  (STX-DAT-01); compile-time table-sorted checks (STX-RES-01).
- **Build-flag guards** — the native test build mirrors `-fno-exceptions -fno-rtti`
  (STX-BLD-01) so RTTI/exception use fails to compile, not just at review.
- **Oracle agreement** — for any supported input, StaTeX matches the MicroTeX oracle within
  tolerance (§4); the legacy lib is linked for tests only and never shipped (STX-BLD-02).

---

## 7. Definition of done (per requirement)

A requirement is done when: its stub is removed, its red tests are green, refusal behavior is
covered, and (where it touches memory) a high-water-mark assertion locks the bound. Trace the
test back to the `STX-*` id so coverage of the requirement set is auditable.

---

## 8. What cannot be host-TDD'd

Only two things, both deferred to Phase 8: actual glyph rasterization fidelity and the
physical TFT timing/pixels. Logic, layout math, memory bounds, and refusal behavior are all
proven natively before hardware is touched.
