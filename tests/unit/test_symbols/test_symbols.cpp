// Phase 3 — flash symbol table + binary-search lookup (STX-RES-01, STX-LNG-02).
#include <unity.h>
#include <statex_glyphstore.h>
#include <statex_symbols.h>
#include <cstring>

using namespace statex;

void setUp() {}
void tearDown() {}

static const SymbolEntry* find(const char* s) {
  return findSymbol(s, static_cast<int>(std::strlen(s)));
}

static void test_known_symbols_resolve() {
  const SymbolEntry* a = find("alpha");
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_UINT(0x03B1u, a->glyph);
  TEST_ASSERT_EQUAL_INT((int)AtomType::Ordinary, (int)a->type);

  const SymbolEntry* leq = find("leq");
  TEST_ASSERT_NOT_NULL(leq);
  TEST_ASSERT_EQUAL_UINT(0x2264u, leq->glyph);
  TEST_ASSERT_EQUAL_INT((int)AtomType::Relation, (int)leq->type);

  const SymbolEntry* sum = find("sum");
  TEST_ASSERT_NOT_NULL(sum);
  TEST_ASSERT_EQUAL_INT((int)AtomType::BigOp, (int)sum->type);
}

static void test_first_and_last_entries() {
  // Boundaries of the binary search. Uppercase sorts before lowercase in
  // ASCII, so the table runs from an AMS ligature to a Greek letter -- it is
  // no longer alpha..times, and hard-coding those would only re-break here the
  // next time the table grows.
  const int n = symbolCount();
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  TEST_ASSERT_NOT_NULL(find(symbolAt(0)->name));
  TEST_ASSERT_NOT_NULL(find(symbolAt(n - 1)->name));
  // Both ends must come back as themselves, not as a neighbour.
  TEST_ASSERT_EQUAL_PTR(symbolAt(0), find(symbolAt(0)->name));
  TEST_ASSERT_EQUAL_PTR(symbolAt(n - 1), find(symbolAt(n - 1)->name));
}

static void test_unknown_returns_null() {
  TEST_ASSERT_NULL(find("zzz"));
  TEST_ASSERT_NULL(find("alphas"));  // prefix of nothing, longer than 'alpha'
  TEST_ASSERT_NULL(find("alph"));    // proper prefix of 'alpha'
  TEST_ASSERT_NULL(find(""));
  TEST_ASSERT_NULL(findSymbol(nullptr, 5));
}

static void test_len_is_respected_not_nul() {
  // "pippo" with len=2 must match "pi", not require a terminator at the key.
  const SymbolEntry* p = findSymbol("pippo", 2);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT(0x03C0u, p->glyph);  // pi
}

static void test_count_sane() {
  // The suite is ~600 names. A table that has collapsed to a handful still
  // passes every test above, because they all name symbols that were in the
  // original fourteen.
  TEST_ASSERT_GREATER_THAN_INT(400, symbolCount());
}

static void test_the_commands_a_cas_actually_emits() {
  // The point of the whole symbol suite. Each of these was UnknownCommand
  // before it, and each is spelled the way LaTeX spells it.
  struct Case {
    const char* name;
    c32 glyph;
    AtomType type;
  };
  static const Case kCases[] = {
      {"pm", 0x00B1, AtomType::BinaryOp},
      {"partial", 0x2202, AtomType::Ordinary},
      {"nabla", 0x2207, AtomType::Ordinary},
      {"rightarrow", 0x2192, AtomType::Relation},
      {"approx", 0x2248, AtomType::Relation},
      {"forall", 0x2200, AtomType::Ordinary},
      {"exists", 0x2203, AtomType::Ordinary},
      {"in", 0x2208, AtomType::Relation},
      {"subset", 0x2282, AtomType::Relation},
      {"cup", 0x222A, AtomType::BinaryOp},
      {"cap", 0x2229, AtomType::BinaryOp},
      {"prod", 0x220F, AtomType::BigOp},
  };
  for (const Case& c : kCases) {
    const SymbolEntry* e = find(c.name);
    TEST_ASSERT_TRUE_MESSAGE(e != nullptr, c.name);
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned>(c.glyph),
                           static_cast<unsigned>(e->glyph));
    TEST_ASSERT_EQUAL_INT((int)c.type, (int)e->type);
  }
}

static void test_limits_follow_tex() {
  // TeX defines \int as \intop\nolimits: an integral's bounds sit beside it,
  // a summation's above and below. Getting this backwards is a visible layout
  // error on the most common formula there is.
  TEST_ASSERT_TRUE(find("sum")->takesLimits);
  TEST_ASSERT_TRUE(find("prod")->takesLimits);
  TEST_ASSERT_FALSE(find("int")->takesLimits);
  TEST_ASSERT_FALSE(find("oint")->takesLimits);
}

static void test_every_name_is_reachable_through_the_parser() {
  // statex_parser.cpp copies the command name into `char key[32]`, and refuses
  // anything longer before the table is ever consulted. An entry past that is
  // dead weight that can never be matched.
  for (int i = 0; i < symbolCount(); ++i) {
    const char* name = symbolAt(i)->name;
    TEST_ASSERT_TRUE_MESSAGE(std::strlen(name) < 32u, name);
    // And it must find *itself*, which is the real test of the ordering.
    TEST_ASSERT_TRUE_MESSAGE(find(name) == symbolAt(i), name);
  }
}

static void test_every_symbol_has_a_glyph() {
  // The parser and the atlas are two halves of one fact. A name whose
  // codepoint has no record parses happily and then fails in *layout* with
  // MissingGlyph -- a different stage and a different error from the
  // UnknownCommand you would expect, which makes it needlessly hard to place.
  // The generator checks this too; asserting it here means a hand-edited table
  // or a half-applied regeneration cannot get past the build.
  int missing = 0;
  for (int i = 0; i < symbolCount(); ++i) {
    const SymbolEntry* e = symbolAt(i);
    if (findGlyphRecord(Face::Symbol, e->glyph) == nullptr) ++missing;
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, missing,
                                "symbols in the table with no glyph in the "
                                "atlas");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_known_symbols_resolve);
  RUN_TEST(test_first_and_last_entries);
  RUN_TEST(test_unknown_returns_null);
  RUN_TEST(test_len_is_respected_not_nul);
  RUN_TEST(test_count_sane);
  RUN_TEST(test_the_commands_a_cas_actually_emits);
  RUN_TEST(test_limits_follow_tex);
  RUN_TEST(test_every_name_is_reachable_through_the_parser);
  RUN_TEST(test_every_symbol_has_a_glyph);
  return UNITY_END();
}
