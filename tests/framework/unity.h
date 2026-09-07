#ifndef STATEX_TEST_UNITY_SHIM_H
#define STATEX_TEST_UNITY_SHIM_H

// A dependency-free stand-in for ThrowTheSwitch Unity, covering exactly the
// macro surface the StaTeX suites use. It exists so the CMake build needs no
// external test-framework dependency, and so the suites originally written
// against PlatformIO's bundled Unity compile here unchanged.
//
// Semantics match Unity where it matters: an assertion failure reports
// file/line and aborts the current test (Unity longjmps; we return), the
// process exit code is the number of failed tests, and setUp()/tearDown() run
// around every test.
//
// One extension: TEST_DEFECT(id, summary) annotates a test as covering a
// known-open defect. It does NOT make the test pass -- red stays red, per
// docs/StaTeX-dev-process.md §1 -- it just prefixes the failure with the
// defect id so `ctest -L red` output reads as a defect list. See
// tests/README.md.

#include <cmath>
#include <cstdio>
#include <cstring>

// Provided by each test translation unit (Unity contract).
void setUp();
void tearDown();

namespace stxtest {

struct State {
  int run = 0;
  int failed = 0;
  const char* current = "";
  const char* defectId = nullptr;   // set by TEST_DEFECT for the current test
  const char* defectWhat = nullptr;
  bool testFailed = false;
};

inline State& state() {
  static State s;
  return s;
}

inline void fail(const char* file, int line, const char* msg) {
  State& s = state();
  s.testFailed = true;
  if (s.defectId != nullptr) {
    std::printf("%s:%d: [%s] %s\n", file, line, s.defectId, msg);
  } else {
    std::printf("%s:%d: %s\n", file, line, msg);
  }
}

inline bool floatWithin(double delta, double expected, double actual) {
  if (std::isnan(expected) || std::isnan(actual)) return false;
  if (std::isinf(expected) || std::isinf(actual)) {
    // Same infinity, same sign. Written without == so the header stays clean
    // under -Wfloat-equal, which the suites build with.
    return !(expected < actual) && !(actual < expected);
  }
  const double d = expected - actual;
  return (d < 0 ? -d : d) <= delta;
}

}  // namespace stxtest

// --- internal plumbing -----------------------------------------------------
#define STX__FAILF(...)                                     \
  do {                                                      \
    char stx__buf[512];                                     \
    std::snprintf(stx__buf, sizeof(stx__buf), __VA_ARGS__); \
    ::stxtest::fail(__FILE__, __LINE__, stx__buf);          \
    return;                                                 \
  } while (0)

// --- Unity-compatible assertions ------------------------------------------
#define TEST_ASSERT_TRUE(cond)                             \
  do {                                                     \
    if (!(cond)) STX__FAILF("expected TRUE: %s", #cond);   \
  } while (0)

#define TEST_ASSERT_FALSE(cond)                            \
  do {                                                     \
    if ((cond)) STX__FAILF("expected FALSE: %s", #cond);   \
  } while (0)

#define TEST_ASSERT_TRUE_MESSAGE(cond, msg)                          \
  do {                                                               \
    if (!(cond)) STX__FAILF("expected TRUE: %s (%s)", #cond, (msg)); \
  } while (0)

#define TEST_ASSERT_FALSE_MESSAGE(cond, msg)                          \
  do {                                                                \
    if ((cond)) STX__FAILF("expected FALSE: %s (%s)", #cond, (msg));  \
  } while (0)

#define TEST_ASSERT_NULL(p)                                  \
  do {                                                       \
    if ((p) != nullptr) STX__FAILF("expected NULL: %s", #p); \
  } while (0)

#define TEST_ASSERT_NOT_NULL(p)                                  \
  do {                                                           \
    if ((p) == nullptr) STX__FAILF("expected non-NULL: %s", #p); \
  } while (0)

#define TEST_ASSERT_EQUAL_PTR(expected, actual)                            \
  do {                                                                     \
    const void* stx__e = (const void*)(expected);                          \
    const void* stx__a = (const void*)(actual);                            \
    if (stx__e != stx__a)                                                  \
      STX__FAILF("expected pointer %p, got %p (%s)", stx__e, stx__a,       \
                 #actual);                                                 \
  } while (0)

#define TEST_ASSERT_EQUAL_INT(expected, actual)                             \
  do {                                                                      \
    const long long stx__e = (long long)(expected);                         \
    const long long stx__a = (long long)(actual);                           \
    if (stx__e != stx__a)                                                   \
      STX__FAILF("expected %lld, got %lld (%s)", stx__e, stx__a, #actual);  \
  } while (0)

#define TEST_ASSERT_EQUAL_INT_MESSAGE(expected, actual, msg)             \
  do {                                                                   \
    const long long stx__e = (long long)(expected);                      \
    const long long stx__a = (long long)(actual);                        \
    if (stx__e != stx__a)                                                \
      STX__FAILF("expected %lld, got %lld: %s", stx__e, stx__a, (msg));  \
  } while (0)

#define TEST_ASSERT_EQUAL_UINT(expected, actual)                            \
  do {                                                                      \
    const unsigned long long stx__e = (unsigned long long)(expected);       \
    const unsigned long long stx__a = (unsigned long long)(actual);         \
    if (stx__e != stx__a)                                                   \
      STX__FAILF("expected %llu, got %llu (%s)", stx__e, stx__a, #actual);  \
  } while (0)

#define TEST_ASSERT_INT_WITHIN(delta, expected, actual)                  \
  do {                                                                   \
    const long long stx__e = (long long)(expected);                      \
    const long long stx__a = (long long)(actual);                        \
    const long long stx__d =                                             \
        stx__e > stx__a ? stx__e - stx__a : stx__a - stx__e;             \
    if (stx__d > (long long)(delta))                                     \
      STX__FAILF("expected %lld +/- %lld, got %lld", stx__e,             \
                 (long long)(delta), stx__a);                            \
  } while (0)

#define TEST_ASSERT_GREATER_THAN_INT(threshold, actual)                      \
  do {                                                                       \
    const long long stx__t = (long long)(threshold);                         \
    const long long stx__a = (long long)(actual);                            \
    if (!(stx__a > stx__t))                                                  \
      STX__FAILF("expected > %lld, got %lld (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_LESS_THAN_INT(threshold, actual)                         \
  do {                                                                       \
    const long long stx__t = (long long)(threshold);                         \
    const long long stx__a = (long long)(actual);                            \
    if (!(stx__a < stx__t))                                                  \
      STX__FAILF("expected < %lld, got %lld (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_GREATER_THAN_UINT(threshold, actual)                     \
  do {                                                                       \
    const unsigned long long stx__t = (unsigned long long)(threshold);       \
    const unsigned long long stx__a = (unsigned long long)(actual);          \
    if (!(stx__a > stx__t))                                                  \
      STX__FAILF("expected > %llu, got %llu (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_GREATER_OR_EQUAL_UINT(threshold, actual)                  \
  do {                                                                        \
    const unsigned long long stx__t = (unsigned long long)(threshold);        \
    const unsigned long long stx__a = (unsigned long long)(actual);           \
    if (!(stx__a >= stx__t))                                                  \
      STX__FAILF("expected >= %llu, got %llu (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_LESS_THAN_UINT(threshold, actual)                        \
  do {                                                                       \
    const unsigned long long stx__t = (unsigned long long)(threshold);       \
    const unsigned long long stx__a = (unsigned long long)(actual);          \
    if (!(stx__a < stx__t))                                                  \
      STX__FAILF("expected < %llu, got %llu (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_GREATER_THAN_FLOAT(threshold, actual)                \
  do {                                                                   \
    const double stx__t = (double)(threshold);                           \
    const double stx__a = (double)(actual);                              \
    if (!(stx__a > stx__t))                                              \
      STX__FAILF("expected > %g, got %g (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_LESS_THAN_FLOAT(threshold, actual)                   \
  do {                                                                   \
    const double stx__t = (double)(threshold);                           \
    const double stx__a = (double)(actual);                              \
    if (!(stx__a < stx__t))                                              \
      STX__FAILF("expected < %g, got %g (%s)", stx__t, stx__a, #actual); \
  } while (0)

#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)                    \
  do {                                                                       \
    const double stx__e = (double)(expected);                                \
    const double stx__a = (double)(actual);                                  \
    if (!::stxtest::floatWithin((double)(delta), stx__e, stx__a))            \
      STX__FAILF("expected %g +/- %g, got %g (%s)", stx__e, (double)(delta), \
                 stx__a, #actual);                                           \
  } while (0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual)                          \
  do {                                                                      \
    const char* stx__e = (expected);                                        \
    const char* stx__a = (actual);                                          \
    if (stx__e == nullptr || stx__a == nullptr ||                           \
        std::strcmp(stx__e, stx__a) != 0)                                   \
      STX__FAILF("expected \"%s\", got \"%s\"", stx__e ? stx__e : "(null)", \
                 stx__a ? stx__a : "(null)");                               \
  } while (0)

#define TEST_FAIL_MESSAGE(msg) \
  do {                         \
    STX__FAILF("%s", (msg));   \
  } while (0)

// --- defect annotation (StaTeX extension) ----------------------------------
//
// Declares that the enclosing test covers a known-open defect. It does not
// suppress the failure: the test is meant to be RED until the defect is fixed.
// It only tags the output so `ctest -L red` reads as a defect report.
#define TEST_DEFECT(id, summary)              \
  do {                                        \
    ::stxtest::State& s = ::stxtest::state(); \
    s.defectId = (id);                        \
    s.defectWhat = (summary);                 \
  } while (0)

// --- runner ----------------------------------------------------------------
#define UNITY_BEGIN()                         \
  do {                                        \
    ::stxtest::State& s = ::stxtest::state(); \
    s.run = 0;                                \
    s.failed = 0;                             \
    std::printf("--- %s ---\n", __FILE__);    \
  } while (0)

#define RUN_TEST(fn)                                                 \
  do {                                                               \
    ::stxtest::State& s = ::stxtest::state();                        \
    s.current = #fn;                                                 \
    s.testFailed = false;                                            \
    s.defectId = nullptr;                                            \
    s.defectWhat = nullptr;                                          \
    s.run++;                                                         \
    setUp();                                                         \
    fn();                                                            \
    tearDown();                                                      \
    if (s.testFailed) {                                              \
      s.failed++;                                                    \
      if (s.defectWhat != nullptr) {                                 \
        std::printf("%s [FAILED] open defect: %s\n", #fn,            \
                    s.defectWhat);                                   \
      } else {                                                       \
        std::printf("%s [FAILED]\n", #fn);                           \
      }                                                              \
    } else {                                                         \
      std::printf("%s [PASSED]\n", #fn);                             \
    }                                                                \
  } while (0)

// Exit code is "number of failures", as Unity does -- but clamped to 255.
// A process exit status is taken modulo 256, so a suite with exactly 256
// failing tests would have exited 0 and reported itself green. No suite is
// near that today; the clamp costs nothing and removes the failure mode
// rather than relying on it staying that way.
#define UNITY_END()                                                  \
  ([]() -> int {                                                     \
    ::stxtest::State& s = ::stxtest::state();                        \
    std::printf("%d tests, %d failed\n", s.run, s.failed);           \
    return s.failed > 255 ? 255 : s.failed;                          \
  }())

#endif  // STATEX_TEST_UNITY_SHIM_H
