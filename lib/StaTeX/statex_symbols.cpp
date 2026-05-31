#include "statex_symbols.h"

namespace statex {
namespace {

// Compile-time-sorted by `name` (ASCII). Keep this list sorted; the
// static_assert below enforces it at build time (STX-RES-01).
constexpr SymbolEntry kSymbols[] = {
    {"alpha", 0x03B1, AtomType::Ordinary},
    {"beta", 0x03B2, AtomType::Ordinary},
    {"cdot", 0x22C5, AtomType::BinaryOp},
    {"gamma", 0x03B3, AtomType::Ordinary},
    {"geq", 0x2265, AtomType::Relation},
    {"infty", 0x221E, AtomType::Ordinary},
    {"int", 0x222B, AtomType::BigOp},
    {"leq", 0x2264, AtomType::Relation},
    {"omega", 0x03C9, AtomType::Ordinary},
    {"phi", 0x03D5, AtomType::Ordinary},
    {"pi", 0x03C0, AtomType::Ordinary},
    {"sum", 0x2211, AtomType::BigOp},
    {"theta", 0x03B8, AtomType::Ordinary},
    {"times", 0x00D7, AtomType::BinaryOp},
};

constexpr int kCount = static_cast<int>(sizeof(kSymbols) / sizeof(kSymbols[0]));

// Compare a NUL-terminated entry name against the key slice key[0..len).
// Returns <0 if name<key, 0 if equal, >0 if name>key.
constexpr int cmpNameKey(const char* name, const char* key, int len) {
  int i = 0;
  for (; i < len; ++i) {
    const unsigned char cn = static_cast<unsigned char>(name[i]);
    if (cn == 0) return -1;  // name ran out first -> shorter -> less
    const unsigned char ck = static_cast<unsigned char>(key[i]);
    if (cn != ck) return (cn < ck) ? -1 : 1;
  }
  return (name[i] == '\0') ? 0 : 1;  // name longer than key -> greater
}

constexpr bool tableSorted(const SymbolEntry* a, int n) {
  for (int i = 1; i < n; ++i) {
    // strictly increasing: a[i-1].name < a[i].name
    int len = 0;
    while (a[i].name[len] != '\0') ++len;
    if (cmpNameKey(a[i - 1].name, a[i].name, len) >= 0) return false;
  }
  return true;
}

static_assert(tableSorted(kSymbols, kCount),
              "kSymbols must be sorted by name for binary search");

}  // namespace

const SymbolEntry* findSymbol(const char* key, int len) {
  if (key == nullptr || len <= 0) return nullptr;
  int lo = 0;
  int hi = kCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const int c = cmpNameKey(kSymbols[mid].name, key, len);
    if (c == 0) return &kSymbols[mid];
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

int symbolCount() { return kCount; }

}  // namespace statex
