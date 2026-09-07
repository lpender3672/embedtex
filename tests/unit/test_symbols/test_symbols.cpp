// Phase 3 — flash symbol table + binary-search lookup (STX-RES-01, STX-LNG-02).
#include <unity.h>
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
  // Boundaries of the binary search.
  TEST_ASSERT_NOT_NULL(find("alpha"));  // first
  TEST_ASSERT_NOT_NULL(find("times"));  // last
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
  TEST_ASSERT_GREATER_THAN_INT(0, symbolCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_known_symbols_resolve);
  RUN_TEST(test_first_and_last_entries);
  RUN_TEST(test_unknown_returns_null);
  RUN_TEST(test_len_is_respected_not_nul);
  RUN_TEST(test_count_sane);
  return UNITY_END();
}
