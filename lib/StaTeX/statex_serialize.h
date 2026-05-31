#ifndef STATEX_SERIALIZE_H
#define STATEX_SERIALIZE_H

#include "statex_node.h"

namespace statex {

/**
 * Test/debug fixture: write a canonical text form of the subtree rooted at `h`
 * into `out` (capacity `cap`, including the NUL terminator). Returns the number
 * of characters written (excluding NUL), or -1 on overflow / depth overflow.
 *
 * This is the assertion mechanism for parser/layout tests (dev-process §3).
 * It is host/test-only and not part of the shipped render path, so bounded
 * recursion here is acceptable.
 *
 * Canonical grammar:
 *   Char       -> the (ASCII) glyph, e.g. `x`
 *   Row        -> `[c0 c1 ...]`            (space-separated, `[]` if empty)
 *   Frac(rule) -> `(frac NUM DEN)`
 *   Frac(!rule)-> `(atop NUM DEN)`
 *   Script     -> `(scr BASE ^SUP _SUB)`   (absent script printed as `.`)
 *   Sqrt       -> `(sqrt BASE)` or `(sqrt[IDX] BASE)`
 *   absent/NO_NODE -> `.`
 */
int serialize(const NodeStore& store, Handle h, char* out, int cap);

}  // namespace statex

#include "statex_box.h"

namespace statex {

/**
 * Box-tree structure dumper (test fixture). Emits class structure only, not
 * metrics:
 *   Char  -> glyph (ASCII) or `<U+XXXX>`
 *   HList -> `(H c0 c1 ...)`
 *   VList -> `(V c0 c1 ...)`
 *   Rule  -> `R`
 * Returns chars written (excl. NUL), or -1 on overflow.
 */
int serializeBox(const BoxStore& store, Handle h, char* out, int cap);

}  // namespace statex

#endif  // STATEX_SERIALIZE_H
