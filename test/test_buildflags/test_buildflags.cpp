// Build-flag guard (STX-BLD-01): this translation unit fails to compile if
// exceptions or RTTI are enabled, so the safety-aligned flags can't silently
// regress.
#include <unity.h>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#error "StaTeX must be built with -fno-exceptions (STX-BLD-01)"
#endif

#if defined(__GXX_RTTI)
#error "StaTeX must be built with -fno-rtti (STX-BLD-01)"
#endif

void setUp() {}
void tearDown() {}

static void test_flags_enforced_at_compile_time() {
  // Reaching here means the #error guards above did not trigger.
  TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_flags_enforced_at_compile_time);
  return UNITY_END();
}
