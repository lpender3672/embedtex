// Phase 2 — POD nodes, NodeStore, serializer (STX-DAT-01/02).
#include <unity.h>
#include <statex_node.h>
#include <statex_serialize.h>
#include <type_traits>
#include <cstring>

using namespace statex;

// STX-DAT-01: nodes must be trivially destructible (so arena reset is O(1) and
// safe) and trivially copyable.
static_assert(std::is_trivially_destructible<Node>::value,
              "Node must be trivially destructible");
static_assert(std::is_trivially_copyable<Node>::value,
              "Node must be trivially copyable");

static u8 g_buf[8192];

void setUp() {}
void tearDown() {}

static void dumpEq(NodeStore& s, Handle h, const char* expect) {
  char out[256];
  int n = serialize(s, h, out, sizeof(out));
  TEST_ASSERT_TRUE_MESSAGE(n >= 0, "serialize overflowed");
  TEST_ASSERT_EQUAL_STRING(expect, out);
}

static void test_make_char() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  TEST_ASSERT_TRUE(s.ok());
  Handle x = s.makeChar('x');
  TEST_ASSERT_TRUE(valid(x));
  TEST_ASSERT_EQUAL_INT((int)Kind::Char, (int)s.get(x).kind);
  TEST_ASSERT_EQUAL_UINT('x', s.get(x).ch);
  dumpEq(s, x, "x");
}

static void test_make_row() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  Handle items[3] = {s.makeChar('a'), s.makeChar('b'), s.makeChar('c')};
  Handle row = s.makeRow(items, 3);
  TEST_ASSERT_TRUE(valid(row));
  TEST_ASSERT_EQUAL_UINT('b', s.get(s.child(row, 1)).ch);
  dumpEq(s, row, "[a b c]");
}

static void test_make_frac_and_atop() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  Handle f = s.makeFrac(s.makeChar('a'), s.makeChar('b'));
  dumpEq(s, f, "(frac a b)");
  Handle g = s.makeFrac(s.makeChar('a'), s.makeChar('b'), /*rule=*/false);
  dumpEq(s, g, "(atop a b)");
}

static void test_make_script_variants() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  Handle sup = s.makeScript(s.makeChar('x'), s.makeChar('2'), NO_NODE);
  dumpEq(s, sup, "(scr x ^2 _.)");
  Handle both = s.makeScript(s.makeChar('x'), s.makeChar('2'), s.makeChar('i'));
  dumpEq(s, both, "(scr x ^2 _i)");
}

static void test_make_sqrt() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  dumpEq(s, s.makeSqrt(s.makeChar('x')), "(sqrt x)");
  dumpEq(s, s.makeSqrt(s.makeChar('x'), s.makeChar('3')), "(sqrt[3] x)");
}

static void test_nested_tree() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  // \frac{x^2}{y}
  Handle xsq = s.makeScript(s.makeChar('x'), s.makeChar('2'), NO_NODE);
  Handle f = s.makeFrac(xsq, s.makeChar('y'));
  dumpEq(s, f, "(frac (scr x ^2 _.) y)");
}

static void test_node_exhaustion_refuses() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 2, 8);  // only 2 nodes
  TEST_ASSERT_TRUE(valid(s.makeChar('a')));
  TEST_ASSERT_TRUE(valid(s.makeChar('b')));
  TEST_ASSERT_FALSE(valid(s.makeChar('c')));  // third refused, no crash
}

static void test_composite_propagates_refusal() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 1, 8);  // room for exactly one node
  Handle x = s.makeChar('x');  // consumes the only slot
  TEST_ASSERT_TRUE(valid(x));
  Handle y = s.makeChar('y');  // refused -> NO_NODE
  TEST_ASSERT_FALSE(valid(y));
  // frac with a NO_NODE child must refuse rather than build a broken node.
  TEST_ASSERT_FALSE(valid(s.makeFrac(x, y)));
}

static void test_store_alloc_failure_when_arena_too_small() {
  u8 tiny[8];
  Arena a(tiny, sizeof(tiny));
  NodeStore s(a, 64, 64);  // cannot fit arrays
  TEST_ASSERT_FALSE(s.ok());
  TEST_ASSERT_FALSE(valid(s.makeChar('x')));
}

static void test_serialize_overflow_returns_negative() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore s(a, 64, 64);
  Handle f = s.makeFrac(s.makeChar('a'), s.makeChar('b'));
  char tiny[4];
  TEST_ASSERT_EQUAL_INT(-1, serialize(s, f, tiny, sizeof(tiny)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_make_char);
  RUN_TEST(test_make_row);
  RUN_TEST(test_make_frac_and_atop);
  RUN_TEST(test_make_script_variants);
  RUN_TEST(test_make_sqrt);
  RUN_TEST(test_nested_tree);
  RUN_TEST(test_node_exhaustion_refuses);
  RUN_TEST(test_composite_propagates_refusal);
  RUN_TEST(test_store_alloc_failure_when_arena_too_small);
  RUN_TEST(test_serialize_overflow_returns_negative);
  return UNITY_END();
}
