#ifndef STATEX_SYMBOLS_H
#define STATEX_SYMBOLS_H

#include "statex_node.h"
#include "statex_types.h"

namespace statex {

/**
 * A named-symbol entry: command name (without backslash) -> glyph + spacing.
 * The table is `constexpr` and lives in flash (STX-RES-01) — no runtime
 * parsing of resource files (replaces MicroTeX's runtime std::map symbol
 * dictionaries). Lookup is binary search over a compile-time-sorted table.
 */
struct SymbolEntry {
  const char* name;
  c32 glyph;
  AtomType type;
  // Whether scripts on this symbol stack above and below it in display style
  // rather than sitting beside it. TeX's `\limits` / `\nolimits`: `\sum`
  // takes limits, `\int` does not, and that is per-symbol data rather than
  // anything derivable from the atom type -- both are big operators.
  bool takesLimits;
};

/**
 * Look up a symbol by name slice `key[0..len)`. Returns the entry, or nullptr
 * if no such symbol (STX-LNG-02: closed command set -> unknown refuses).
 */
const SymbolEntry* findSymbol(const char* key, int len);

/** Number of entries in the table (for tests / introspection). */
int symbolCount();

/**
 * The `i`th entry in name order, or nullptr when `i` is out of range.
 *
 * Exists so a test can sweep the whole table -- every name must find itself,
 * fit the parser's key buffer, and have a glyph -- without the test carrying a
 * copy of the names, which is what made the old first/last assertions go stale
 * the moment the table grew.
 */
const SymbolEntry* symbolAt(int i);

}  // namespace statex

#endif  // STATEX_SYMBOLS_H
