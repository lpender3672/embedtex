# embedtex

Home of **StaTeX**: a statically-allocated, bounded, deterministic TeX
*maths* renderer for embedded displays. No heap after init, no exceptions,
no RTTI; the caller supplies a scratch arena and a two-method `Graphics2D`
backend. See `docs/StaTeX-requirements.md` and `docs/StaTeX-layering.md`.

- `lib/StaTeX/` — the portable core (parser, layout, draw)
- `lib/glyphstore/` — SDF glyph atlas + sampler (generated tables)
- `backends/` — `Graphics2D` implementations
- `zephyr/` — Zephyr module glue (`CONFIG_STATEX`)
- `tests/` — host CTest suites, including a differential oracle

Host build: `cmake -S . -B build -G Ninja && ctest --test-dir build`.

## Licence

First-party code is MIT (see `LICENSE`). Vendored third-party code under
`vendor/` and `lib/MicroTeX/` retains its own upstream licences; none of it
is part of the firmware module build.
