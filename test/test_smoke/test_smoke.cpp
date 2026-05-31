// Phase 0 smoke test: confirms the native harness builds and runs, and that
// the StaTeX core header compiles under -fno-exceptions -fno-rtti on the host.
#include <unity.h>
#include <statex_types.h>

using namespace statex;

void setUp() {}
void tearDown() {}

static void test_harness_runs() {
  TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

static void test_handle_sentinel() {
  TEST_ASSERT_FALSE(valid(NO_NODE));
  TEST_ASSERT_TRUE(valid(static_cast<Handle>(0)));
  TEST_ASSERT_EQUAL_UINT(2u, sizeof(Handle));
}

static void test_span_defaults() {
  Span s;
  TEST_ASSERT_EQUAL_UINT(0u, s.first);
  TEST_ASSERT_EQUAL_UINT(0u, s.count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_harness_runs);
  RUN_TEST(test_handle_sentinel);
  RUN_TEST(test_span_defaults);
  return UNITY_END();
}
