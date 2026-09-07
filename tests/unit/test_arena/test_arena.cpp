// Phase 1 — Arena allocator tests (STX-MEM-01/02/03/04/06).
#include <unity.h>
#include <statex_arena.h>
#include <cstdint>

using namespace statex;

static u8 g_buf[1024];

static Arena makeArena(u32 cap = sizeof(g_buf)) { return Arena(g_buf, cap); }

void setUp() {}
void tearDown() {}

static void test_initial_state() {
  Arena a = makeArena(256);
  TEST_ASSERT_EQUAL_UINT(256u, a.capacity());
  TEST_ASSERT_EQUAL_UINT(0u, a.used());
  TEST_ASSERT_EQUAL_UINT(256u, a.remaining());
  TEST_ASSERT_EQUAL_UINT(0u, a.highWater());
}

static void test_alloc_returns_distinct_nonnull() {
  Arena a = makeArena();
  void* p = a.alloc(16, 1);
  void* q = a.alloc(16, 1);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_NOT_NULL(q);
  TEST_ASSERT_TRUE(p != q);
  // Non-overlapping: q starts at or after p+16.
  TEST_ASSERT_TRUE(reinterpret_cast<u8*>(q) >= reinterpret_cast<u8*>(p) + 16);
}

static void test_used_accounting() {
  Arena a = makeArena();
  a.alloc(10, 1);
  TEST_ASSERT_EQUAL_UINT(10u, a.used());
  a.alloc(20, 1);
  TEST_ASSERT_EQUAL_UINT(30u, a.used());
  TEST_ASSERT_EQUAL_UINT(a.capacity() - 30u, a.remaining());
}

static void test_alignment_honored() {
  Arena a = makeArena();
  a.alloc(1, 1);  // force a likely-misaligned top
  void* p = a.alloc(8, 8);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT(0u, reinterpret_cast<uintptr_t>(p) % 8u);
  void* q = a.alloc(4, 4);
  TEST_ASSERT_EQUAL_UINT(0u, reinterpret_cast<uintptr_t>(q) % 4u);
}

static void test_exhaustion_returns_null() {
  Arena a = makeArena(64);
  void* p = a.alloc(64, 1);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT(64u, a.used());
  // No room left: must refuse, not crash.
  void* q = a.alloc(1, 1);
  TEST_ASSERT_NULL(q);
}

static void test_failed_alloc_does_not_consume() {
  Arena a = makeArena(64);
  a.alloc(40, 1);
  u32 before = a.used();
  void* q = a.alloc(40, 1);  // doesn't fit
  TEST_ASSERT_NULL(q);
  TEST_ASSERT_EQUAL_UINT(before, a.used());  // unchanged
  // A smaller request that DOES fit still succeeds afterwards.
  void* r = a.alloc(16, 1);
  TEST_ASSERT_NOT_NULL(r);
}

static void test_reset_reuses_space_and_keeps_highwater() {
  Arena a = makeArena(128);
  a.alloc(100, 1);
  TEST_ASSERT_EQUAL_UINT(100u, a.highWater());
  a.reset();
  TEST_ASSERT_EQUAL_UINT(0u, a.used());
  TEST_ASSERT_EQUAL_UINT(128u, a.remaining());
  TEST_ASSERT_EQUAL_UINT(100u, a.highWater());  // preserved across reset
  // Full capacity available again.
  void* p = a.alloc(128, 1);
  TEST_ASSERT_NOT_NULL(p);
}

static void test_highwater_tracks_max() {
  Arena a = makeArena(128);
  a.alloc(50, 1);
  a.reset();
  a.alloc(30, 1);
  TEST_ASSERT_EQUAL_UINT(50u, a.highWater());  // max over all epochs
  a.reset();
  a.alloc(70, 1);
  TEST_ASSERT_EQUAL_UINT(70u, a.highWater());
}

static void test_mark_rewind() {
  Arena a = makeArena(128);
  a.alloc(20, 1);
  u32 m = a.mark();
  TEST_ASSERT_EQUAL_UINT(20u, m);
  a.alloc(40, 1);
  TEST_ASSERT_EQUAL_UINT(60u, a.used());
  a.rewind(m);  // free the second alloc only
  TEST_ASSERT_EQUAL_UINT(20u, a.used());
  TEST_ASSERT_EQUAL_UINT(60u, a.highWater());  // preserved
  // The reclaimed space is reusable.
  void* p = a.alloc(50, 1);
  TEST_ASSERT_NOT_NULL(p);
}

static void test_alloc_array_typed() {
  Arena a = makeArena();
  u32* arr = a.allocArray<u32>(8);
  TEST_ASSERT_NOT_NULL(arr);
  TEST_ASSERT_EQUAL_UINT(0u, reinterpret_cast<uintptr_t>(arr) % alignof(u32));
  for (int i = 0; i < 8; ++i) arr[i] = static_cast<u32>(i * 7);
  TEST_ASSERT_EQUAL_UINT(0u, arr[0]);
  TEST_ASSERT_EQUAL_UINT(49u, arr[7]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state);
  RUN_TEST(test_alloc_returns_distinct_nonnull);
  RUN_TEST(test_used_accounting);
  RUN_TEST(test_alignment_honored);
  RUN_TEST(test_exhaustion_returns_null);
  RUN_TEST(test_failed_alloc_does_not_consume);
  RUN_TEST(test_reset_reuses_space_and_keeps_highwater);
  RUN_TEST(test_highwater_tracks_max);
  RUN_TEST(test_mark_rewind);
  RUN_TEST(test_alloc_array_typed);
  return UNITY_END();
}
