// Phase 4 — parser: string -> atom tree, iterative work-stack, refusals.
// STX-EXE-01/02/03, STX-ERR-02/03, STX-LNG-02/03.
#include <unity.h>
#include <statex_parser.h>
#include <statex_serialize.h>

using namespace statex;

static u8 g_buf[32768];

void setUp() {}
void tearDown() {}

// Parse `src` with generous limits; assert it serializes to `expect`.
static void ok(const c32* src, int len, const char* expect) {
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store);
  ParseResult r = p.parse(src, len);
  TEST_ASSERT_EQUAL_INT_MESSAGE((int)ParseError::Ok, (int)r.error, "expected Ok");
  TEST_ASSERT_TRUE(valid(r.root));
  char out[512];
  int n = serialize(store, r.root, out, sizeof(out));
  TEST_ASSERT_TRUE(n >= 0);
  TEST_ASSERT_EQUAL_STRING(expect, out);
}

// helper to get length of a char32 literal
template <int N>
static int slen(const c32 (&)[N]) { return N - 1; }

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

// ---- happy paths ----

static void test_single_char() { OK(U"x", "x"); }

static void test_row() { OK(U"xy", "[x y]"); }

static void test_whitespace_ignored() { OK(U"x   y", "[x y]"); }

static void test_superscript() { OK(U"x^2", "(scr x ^2 _.)"); }

static void test_subscript() { OK(U"x_i", "(scr x ^. _i)"); }

static void test_combined_scripts() { OK(U"x^2_i", "(scr x ^2 _i)"); }

static void test_combined_scripts_reversed() { OK(U"x_i^2", "(scr x ^2 _i)"); }

static void test_group() { OK(U"{xy}z", "[[x y] z]"); }

static void test_group_script() { OK(U"{ab}^2", "(scr [a b] ^2 _.)"); }

static void test_frac_braced() { OK(U"\\frac{a}{b}", "(frac a b)"); }

static void test_frac_single_token() { OK(U"\\frac ab", "(frac a b)"); }

static void test_frac_nested() {
  OK(U"\\frac{x^2}{y}", "(frac (scr x ^2 _.) y)");
}

static void test_sqrt() { OK(U"\\sqrt{x}", "(sqrt x)"); }

static void test_sqrt_index() { OK(U"\\sqrt[3]{x}", "(sqrt[3] x)"); }

static void test_sqrt_of_frac() {
  OK(U"\\sqrt{\\frac{a}{b}}", "(sqrt (frac a b))");
}

static void test_symbol() { OK(U"\\alpha", "<U+03B1>"); }

static void test_symbol_in_row() { OK(U"a\\cdot b", "[a <U+22C5> b]"); }

static void test_script_on_command_result() {
  OK(U"\\frac{a}{b}^2", "(scr (frac a b) ^2 _.)");
}

static void test_empty_group() { OK(U"x^{}", "(scr x ^[] _.)"); }

static void test_deeply_nested_ok() {
  OK(U"{{{{x}}}}", "x");  // groups of one collapse to the single atom
}

// ---- refusals (defined behavior, no UB) ----

static void test_empty_input() { REFUSE(U"", ParseError::Empty); }

static void test_unbalanced_open() { REFUSE(U"{x", ParseError::UnbalancedBrace); }

static void test_unbalanced_close() {
  REFUSE(U"x}", ParseError::UnbalancedBrace);
}

static void test_unknown_command() {
  REFUSE(U"\\bogus", ParseError::UnknownCommand);
}

static void test_lone_backslash() {
  REFUSE(U"\\", ParseError::UnexpectedChar);
}

static void test_script_no_base() { REFUSE(U"^2", ParseError::BadScript); }

static void test_double_superscript() {
  REFUSE(U"x^2^3", ParseError::BadScript);
}

static void test_script_missing_arg() { REFUSE(U"x^", ParseError::MissingArg); }

static void test_frac_missing_second_arg() {
  REFUSE(U"\\frac{a}", ParseError::MissingArg);
}

static void test_too_deep_refused() {
  // Tight depth limit; deep nesting must refuse cleanly, not overflow.
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 1024, 1024);
  Parser p(a, store, /*maxDepth=*/4, /*maxOperands=*/256);
  const c32 src[] = U"{{{{{{x}}}}}}";
  ParseResult r = p.parse(src, (int)(sizeof(src) / sizeof(c32)) - 1);
  TEST_ASSERT_EQUAL_INT((int)ParseError::TooDeep, (int)r.error);
  TEST_ASSERT_FALSE(valid(r.root));
}

static void test_node_exhaustion_refused() {
  // Tiny node store: a long row must refuse with OutOfMemory.
  Arena a(g_buf, sizeof(g_buf));
  NodeStore store(a, 4, 16);
  Parser p(a, store);
  const c32 src[] = U"abcdefgh";
  ParseResult r = p.parse(src, (int)(sizeof(src) / sizeof(c32)) - 1);
  TEST_ASSERT_EQUAL_INT((int)ParseError::OutOfMemory, (int)r.error);
  TEST_ASSERT_FALSE(valid(r.root));
}

// ---- face / style selection (STX-FNT-05) ----

static void test_mathbf_group() { OK(U"\\mathbf{AB}", "[A!b B!b]"); }

static void test_mathit_single_token() { OK(U"\\mathit x", "x!i"); }

static void test_mathbb() { OK(U"\\mathbb R", "R!bb"); }

static void test_style_then_script() {
  OK(U"\\mathbf{x}^2", "(scr x!b ^2 _.)");
}

static void test_nested_style_restores() {
  // inner \mathit overrides, then face restores to bold for the rest
  OK(U"\\mathbf{a\\mathit{b}c}", "[a!b b!i c!b]");
}

static void test_style_missing_arg() {
  REFUSE(U"\\mathbf", ParseError::MissingArg);
}

static void test_reuse_after_refusal() {
  // STX-MEM-03: after a refusal + reset, the next parse succeeds cleanly.
  Arena a(g_buf, sizeof(g_buf));
  {
    NodeStore store(a, 1024, 1024);
    Parser p(a, store);
    ParseResult bad = p.parse(U"{x", 2);
    TEST_ASSERT_FALSE(valid(bad.root));
  }
  a.reset();
  {
    NodeStore store(a, 1024, 1024);
    Parser p(a, store);
    ParseResult good = p.parse(U"x^2", 3);
    TEST_ASSERT_EQUAL_INT((int)ParseError::Ok, (int)good.error);
    char out[64];
    serialize(store, good.root, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("(scr x ^2 _.)", out);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_char);
  RUN_TEST(test_row);
  RUN_TEST(test_whitespace_ignored);
  RUN_TEST(test_superscript);
  RUN_TEST(test_subscript);
  RUN_TEST(test_combined_scripts);
  RUN_TEST(test_combined_scripts_reversed);
  RUN_TEST(test_group);
  RUN_TEST(test_group_script);
  RUN_TEST(test_frac_braced);
  RUN_TEST(test_frac_single_token);
  RUN_TEST(test_frac_nested);
  RUN_TEST(test_sqrt);
  RUN_TEST(test_sqrt_index);
  RUN_TEST(test_sqrt_of_frac);
  RUN_TEST(test_symbol);
  RUN_TEST(test_symbol_in_row);
  RUN_TEST(test_script_on_command_result);
  RUN_TEST(test_empty_group);
  RUN_TEST(test_deeply_nested_ok);
  RUN_TEST(test_empty_input);
  RUN_TEST(test_unbalanced_open);
  RUN_TEST(test_unbalanced_close);
  RUN_TEST(test_unknown_command);
  RUN_TEST(test_lone_backslash);
  RUN_TEST(test_script_no_base);
  RUN_TEST(test_double_superscript);
  RUN_TEST(test_script_missing_arg);
  RUN_TEST(test_frac_missing_second_arg);
  RUN_TEST(test_mathbf_group);
  RUN_TEST(test_mathit_single_token);
  RUN_TEST(test_mathbb);
  RUN_TEST(test_style_then_script);
  RUN_TEST(test_nested_style_restores);
  RUN_TEST(test_style_missing_arg);
  RUN_TEST(test_too_deep_refused);
  RUN_TEST(test_node_exhaustion_refused);
  RUN_TEST(test_reuse_after_refusal);
  return UNITY_END();
}
