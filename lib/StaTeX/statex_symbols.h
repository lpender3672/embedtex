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
};

/**
 * Look up a symbol by name slice `key[0..len)`. Returns the entry, or nullptr
 * if no such symbol (STX-LNG-02: closed command set -> unknown refuses).
 */
const SymbolEntry* findSymbol(const char* key, int len);

/** Number of entries in the table (for tests / introspection). */
int symbolCount();

}  // namespace statex

#endif  // STATEX_SYMBOLS_H
