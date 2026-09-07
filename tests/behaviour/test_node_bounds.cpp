// Was RED (defects MATRIX-U16-OVERFLOW and LAYOUT-CELL-COUNT.
//
// MATRIX-U16-OVERFLOW  lib/StaTeX/statex_node.cpp:86
//     if (count != static_cast<u16>(rows * cols)) return NO_NODE;
//   `rows * cols` is an int expression truncated to u16, so the guard is
//   defeated by any (rows, cols) whose product is a multiple of 65536.
//   makeMatrix(256, 256, ..., count = 0) is accepted and produces a node
//   claiming a 256x256 grid over a zero-length cell span. cell() and cellAt()
//   then index past the end of the child buffer.
//
//   The parser bounds matrices at 32x32 so this is not reachable through
//   render(), but NodeStore is public API and the whole design rests on
//   "every factory either returns a valid node or NO_NODE" (STX-MEM-03).
//
// LAYOUT-CELL-COUNT  lib/StaTeX/statex_layout.cpp:288
//     const u16 total = static_cast<u16>(n.matrix.rows * n.matrix.cols);
//     for (u16 k = 0; k < total; ++k) push(_nodes.cellAt(it.atom, k), ...);
//   The pre-order walk trusts rows*cols rather than the authoritative span
//   length n.matrix.cells.count, and runs before combine()'s kMatMax guard.
//   Two sources of truth for one length; the span is the one that describes
//   the memory actually reserved.

#include <unity.h>

#include <cstdio>

#include "statex_arena.h"
#include "statex_node.h"
#include "stx_corpus.h"
#include "stx_render.h"

using namespace stxtest;
using namespace statex;

namespace {
u8 g_buf[64 * 1024];
}

void setUp() {}
void tearDown() {}

static void test_matrix_dimension_product_cannot_overflow_the_guard() {
  Arena a(g_buf, sizeof g_buf);
  NodeStore ns(a, 512, 512);
  TEST_ASSERT_TRUE(ns.ok());

  const Handle c = ns.makeChar(U'x');
  TEST_ASSERT_TRUE(valid(c));
  Handle cells[1] = {c};

  // 256 * 256 == 65536, which truncates to 0 and so "matches" count == 0.
  const Handle m = ns.makeMatrix(256, 256, MatrixEnv::Plain, cells, 0);
  if (valid(m)) {
    const Node& n = ns.get(m);
    std::printf("  accepted a node claiming %ux%u over %u cells\n",
                n.matrix.rows, n.matrix.cols, n.matrix.cells.count);
  }
  TEST_ASSERT_FALSE(valid(m));
}

static void test_other_overflowing_dimension_pairs_are_refused() {
  Arena a(g_buf, sizeof g_buf);
  NodeStore ns(a, 512, 512);
  const Handle c = ns.makeChar(U'x');
  Handle cells[1] = {c};

  struct Pair { u16 rows, cols; };
  const Pair pairs[] = {{256, 256}, {512, 128}, {1024, 64}, {65535, 0}};
  for (const Pair& p : pairs) {
    const Handle m = ns.makeMatrix(p.rows, p.cols, MatrixEnv::Plain, cells, 0);
    if (valid(m)) {
      std::printf("  makeMatrix(%u, %u, count=0) accepted\n", p.rows, p.cols);
    }
    TEST_ASSERT_FALSE(valid(m));
  }
}

static void test_well_formed_matrices_are_still_accepted() {
  // The guard must reject the overflow without becoming over-strict.
  Arena a(g_buf, sizeof g_buf);
  NodeStore ns(a, 512, 512);
  Handle cells[4];
  for (int i = 0; i < 4; ++i) {
    cells[i] = ns.makeChar(static_cast<c32>(U'a' + static_cast<unsigned>(i)));
    TEST_ASSERT_TRUE(valid(cells[i]));
  }
  const Handle m = ns.makeMatrix(2, 2, MatrixEnv::Bracket, cells, 4);
  TEST_ASSERT_TRUE(valid(m));
  const Node& n = ns.get(m);
  TEST_ASSERT_EQUAL_UINT(2u, n.matrix.rows);
  TEST_ASSERT_EQUAL_UINT(2u, n.matrix.cols);
  TEST_ASSERT_EQUAL_UINT(4u, n.matrix.cells.count);
}

static void test_mismatched_count_is_refused() {
  // The non-overflowing half of the same guard, which does work today.
  Arena a(g_buf, sizeof g_buf);
  NodeStore ns(a, 512, 512);
  Handle cells[4];
  for (int i = 0; i < 4; ++i) {
    cells[i] = ns.makeChar(static_cast<c32>(U'a' + static_cast<unsigned>(i)));
  }
  TEST_ASSERT_FALSE(valid(ns.makeMatrix(2, 3, MatrixEnv::Plain, cells, 4)));
  TEST_ASSERT_FALSE(valid(ns.makeMatrix(2, 2, MatrixEnv::Plain, cells, 3)));
  TEST_ASSERT_FALSE(valid(ns.makeMatrix(0, 4, MatrixEnv::Plain, cells, 4)));
}

static void test_cell_span_is_the_single_source_of_truth() {
  // Every matrix node the factory produces must satisfy
  // cells.count == rows * cols computed without truncation. Layout is entitled
  // to rely on exactly one of them; this pins them equal so that either
  // choice is safe.
  Arena a(g_buf, sizeof g_buf);
  // 2048 nodes is 32 KB and 2048 child handles 4 KB, which fits g_buf. Asking
  // for 4096 nodes needed 72 KB, so the store silently failed to allocate and
  // every factory refused -- the test then failed for that reason rather than
  // the defect it names.
  NodeStore ns(a, 2048, 2048);
  TEST_ASSERT_TRUE(ns.ok());
  for (u16 rows = 1; rows <= 8; ++rows) {
    for (u16 cols = 1; cols <= 8; ++cols) {
      const u16 count = static_cast<u16>(rows * cols);
      Handle cells[64];
      for (u16 i = 0; i < count; ++i) cells[i] = ns.makeChar(U'a');
      const Handle m =
          ns.makeMatrix(rows, cols, MatrixEnv::Plain, cells, count);
      TEST_ASSERT_TRUE(valid(m));
      const Node& n = ns.get(m);
      const unsigned product =
          static_cast<unsigned>(n.matrix.rows) * n.matrix.cols;
      TEST_ASSERT_EQUAL_UINT(product, n.matrix.cells.count);
    }
  }
}

static void test_parser_produced_matrices_keep_the_invariant() {
  // The same invariant, end to end, for everything in the corpus. This is the
  // regression guard once layout is changed to read cells.count.
  for (const Case& c : corpusWith(Feature::Matrix)) {
    if (c.expect != Expect::Renders) continue;
    const RenderOutput r = renderStatex(tex32(c.tex));
    TEST_ASSERT_TRUE(r.ok());
  }
}

static void test_zero_capacity_stores_refuse_rather_than_index() {
  // Adjacent hardening: a store with no room must refuse every factory call.
  // Already true, and asserted here because the overflow fix touches the same
  // guards.
  Arena a(g_buf, sizeof g_buf);
  NodeStore ns(a, 0, 0);
  TEST_ASSERT_FALSE(valid(ns.makeChar(U'x')));
  Handle none[1] = {NO_NODE};
  TEST_ASSERT_FALSE(valid(ns.makeRow(none, 0)));
  TEST_ASSERT_FALSE(valid(ns.makeMatrix(0, 0, MatrixEnv::Plain, none, 0)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_matrix_dimension_product_cannot_overflow_the_guard);
  RUN_TEST(test_other_overflowing_dimension_pairs_are_refused);
  RUN_TEST(test_well_formed_matrices_are_still_accepted);
  RUN_TEST(test_mismatched_count_is_refused);
  RUN_TEST(test_cell_span_is_the_single_source_of_truth);
  RUN_TEST(test_parser_produced_matrices_keep_the_invariant);
  RUN_TEST(test_zero_capacity_stores_refuse_rather_than_index);
  return UNITY_END();
}
