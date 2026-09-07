// Matrix support (STX-DAT-04): bounded grid, ragged/overflow refusals, layout.
#include <unity.h>
#include <statex_parser.h>
#include <statex_layout.h>
#include <statex_serialize.h>

using namespace statex;

static u8 g_buf[65536];

void setUp() {}
void tearDown() {}

template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

static void ok(const c32* src, int len, const char* expect) {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store);
  ParseResult r = p.parse(src, len);
  TEST_ASSERT_EQUAL_INT_MESSAGE((int)ParseError::Ok, (int)r.error, "expected Ok");
  char out[512];
  TEST_ASSERT_TRUE(serialize(store, r.root, out, sizeof(out)) >= 0);
  TEST_ASSERT_EQUAL_STRING(expect, out);
}
#define OK(LIT, EXPECT) ok(LIT, slen(LIT), EXPECT)

static void refuse(const c32* src, int len, ParseError expect) {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store);
  ParseResult r = p.parse(src, len);
  TEST_ASSERT_EQUAL_INT((int)expect, (int)r.error);
  TEST_ASSERT_FALSE(valid(r.root));
}
#define REFUSE(LIT, ERR) refuse(LIT, slen(LIT), ERR)

static void test_basic_2x2() {
  OK(U"\\begin{matrix}a&b\\\\c&d\\end{matrix}", "(mat[2x2] a b c d)");
}

static void test_single_row() {
  OK(U"\\begin{matrix}a&b&c\\end{matrix}", "(mat[1x3] a b c)");
}

static void test_single_cell() {
  OK(U"\\begin{matrix}x\\end{matrix}", "(mat[1x1] x)");
}

static void test_trailing_rowsep_ignored() {
  OK(U"\\begin{matrix}a&b\\\\c&d\\\\\\end{matrix}", "(mat[2x2] a b c d)");
}

static void test_bmatrix() {
  OK(U"\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}", "(bmat[2x2] a b c d)");
}

static void test_pmatrix() {
  OK(U"\\begin{pmatrix}1\\\\0\\end{pmatrix}", "(pmat[2x1] 1 0)");
}

static void test_cells_with_structure() {
  OK(U"\\begin{matrix}x^2&y\\\\1&\\alpha\\end{matrix}",
     "(mat[2x2] (scr x ^2 _.) y 1 <U+03B1>)");
}

static void test_matrix_in_expression() {
  OK(U"A=\\begin{bmatrix}a\\\\b\\end{bmatrix}",
     "[A = (bmat[2x1] a b)]");
}

static void test_ragged_refused() {
  REFUSE(U"\\begin{matrix}a&b\\\\c\\end{matrix}", ParseError::InvalidMatrix);
}

static void test_column_sep_outside_matrix() {
  REFUSE(U"a&b", ParseError::InvalidMatrix);
}

static void test_unclosed_environment() {
  REFUSE(U"\\begin{matrix}a&b", ParseError::InvalidMatrix);
}

static void test_mismatched_end() {
  REFUSE(U"\\begin{matrix}a\\end{bmatrix}", ParseError::InvalidMatrix);
}

static void test_unknown_environment() {
  REFUSE(U"\\begin{foo}a\\end{foo}", ParseError::UnknownCommand);
}

static void test_column_overflow_refused() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store, 64, 256, /*maxRows=*/8, /*maxCols=*/2);
  const c32 src[] = U"\\begin{matrix}a&b&c\\end{matrix}";
  ParseResult r = p.parse(src, slen(src));
  TEST_ASSERT_EQUAL_INT((int)ParseError::InvalidMatrix, (int)r.error);
}

static void test_matrix_layout_structure() {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store);
  const c32 src[] = U"\\begin{bmatrix}a&b\\\\c&d\\end{bmatrix}";
  ParseResult r = p.parse(src, slen(src));
  TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)r.error);
  BoxStore boxes(a, 512, 512);
  Layout lay(a, store, boxes, 20.0f);
  LayoutResult lr = lay.run(r.root);
  TEST_ASSERT_TRUE(lr.ok);
  char out[256];
  TEST_ASSERT_TRUE(serializeBox(boxes, lr.rootBox, out, sizeof(out)) >= 0);
  // [ delimiter, VList of two HList rows, ] delimiter. The `_` entries are
  // struts: they carry the inter-column gap and the pad that centres a cell
  // narrower than its column. Row 0 needs none before 'a' because 'a' is the
  // widest in its column; 'c' is narrower, so it gets one.
  TEST_ASSERT_EQUAL_STRING("(H [ (V (H a _ b) (H _ c _ d)) ])", out);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_basic_2x2);
  RUN_TEST(test_single_row);
  RUN_TEST(test_single_cell);
  RUN_TEST(test_trailing_rowsep_ignored);
  RUN_TEST(test_bmatrix);
  RUN_TEST(test_pmatrix);
  RUN_TEST(test_cells_with_structure);
  RUN_TEST(test_matrix_in_expression);
  RUN_TEST(test_ragged_refused);
  RUN_TEST(test_column_sep_outside_matrix);
  RUN_TEST(test_unclosed_environment);
  RUN_TEST(test_mismatched_end);
  RUN_TEST(test_unknown_environment);
  RUN_TEST(test_column_overflow_refused);
  RUN_TEST(test_matrix_layout_structure);
  return UNITY_END();
}
