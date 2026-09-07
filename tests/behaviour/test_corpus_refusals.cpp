// Corpus-wide behavioural audit: every case in tests/support/stx_corpus.cpp
// either renders or refuses, as the corpus says it should.
//
// Cases carrying an `openDefect` tag are skipped here and named in the output;
// the red suite that owns each defect asserts them instead. That keeps this
// suite green and makes the deferred list impossible to lose track of.

#include <unity.h>

#include <cstdio>
#include <set>
#include <string>

#include "stx_compare.h"
#include "stx_corpus.h"
#include "stx_render.h"

using namespace stxtest;

void setUp() {}
void tearDown() {}

static void test_every_rendering_case_renders() {
  int checked = 0, deferred = 0;
  for (const Case& c : corpus()) {
    if (c.expect != Expect::Renders) continue;
    if (!isSettled(c)) {
      deferred++;
      std::printf("  deferred to %s: %s\n", c.openDefect, c.id);
      continue;
    }
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (!r.ok()) {
      std::printf("  case '%s' (%s) refused with code %d\n", c.id, c.tex,
                  static_cast<int>(r.error));
    }
    TEST_ASSERT_TRUE(r.ok());
    checked++;
  }
  std::printf("  %d rendering cases checked, %d deferred\n", checked, deferred);
  TEST_ASSERT_GREATER_THAN_INT(20, checked);
}

static void test_every_refusal_case_refuses() {
  int checked = 0, deferred = 0;
  for (const Case& c : corpus()) {
    if (c.expect != Expect::Refuses) continue;
    if (!isSettled(c)) {
      deferred++;
      std::printf("  deferred to %s: %s (%s)\n", c.openDefect, c.id, c.note);
      continue;
    }
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (r.ok()) {
      std::printf("  case '%s' (%s) rendered but should refuse: %s\n", c.id,
                  c.tex, c.note);
    }
    TEST_ASSERT_FALSE(r.ok());
    checked++;
  }
  std::printf("  %d refusal cases checked, %d deferred\n", checked, deferred);
  TEST_ASSERT_GREATER_THAN_INT(5, checked);
}

static void test_a_rendering_case_actually_puts_ink_down() {
  // "Rendered" must mean pixels, not just a non-error return. This is the
  // property the glyph-coverage defect violates, so it is asserted here for
  // the settled cases and in the red suite for the rest.
  for (const Case& c : corpus()) {
    if (c.expect != Expect::Renders || !isSettled(c)) continue;
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (!r.ok()) continue;
    const stximg::InkStats st = stximg::inkStats(r.image);
    if (!st.any()) {
      std::printf("  case '%s' (%s) reported Ok but drew nothing\n", c.id,
                  c.tex);
    }
    TEST_ASSERT_TRUE(st.any());
  }
}

static void test_refusal_leaves_no_residue() {
  // STX-API-02: a refused request must not affect the next one.
  const RenderOutput good1 = renderStatex(tex32("x^2"));
  TEST_ASSERT_TRUE(good1.ok());

  for (const Case& c : corpus()) {
    if (c.expect != Expect::Refuses || !isSettled(c)) continue;
    (void)renderStatex(tex32(c.tex));
  }

  const RenderOutput good2 = renderStatex(tex32("x^2"));
  TEST_ASSERT_TRUE(good2.ok());
  const stximg::Similarity s = stximg::compare(good1.image, good2.image);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
  TEST_ASSERT_EQUAL_UINT(good1.stats.highWater, good2.stats.highWater);
}

static void test_renders_are_deterministic() {
  for (const Case& c : corpus()) {
    if (c.expect != Expect::Renders || !isSettled(c)) continue;
    const RenderOutput a = renderStatex(tex32(c.tex));
    const RenderOutput b = renderStatex(tex32(c.tex));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(a.error), static_cast<int>(b.error));
    if (!a.ok()) continue;
    const stximg::Similarity s = stximg::compare(a.image, b.image);
    if (s.rmse > 0.0) std::printf("  case '%s' is not deterministic\n", c.id);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, s.rmse);
  }
}

static void test_corpus_ids_are_unique() {
  std::set<std::string> seen;
  for (const Case& c : corpus()) {
    const bool fresh = seen.insert(c.id).second;
    if (!fresh) std::printf("  duplicate corpus id: %s\n", c.id);
    TEST_ASSERT_TRUE(fresh);
  }
}

static void test_memory_budget_holds_across_the_corpus() {
  // The capability bound is only meaningful if it is measured. 96 KB is what
  // src/main.cpp reserves; every corpus case must fit inside it.
  const statex::u32 kDeviceScratch = 96u * 1024u;
  statex::u32 worst = 0;
  const char* worstId = "";
  for (const Case& c : corpus()) {
    const RenderOutput r = renderStatex(tex32(c.tex));
    if (!r.ok()) continue;
    if (r.stats.highWater > worst) {
      worst = r.stats.highWater;
      worstId = c.id;
    }
  }
  std::printf("  peak scratch %u bytes on '%s' (budget %u)\n", worst, worstId,
              kDeviceScratch);
  TEST_ASSERT_LESS_THAN_UINT(kDeviceScratch, worst);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_rendering_case_renders);
  RUN_TEST(test_every_refusal_case_refuses);
  RUN_TEST(test_a_rendering_case_actually_puts_ink_down);
  RUN_TEST(test_refusal_leaves_no_residue);
  RUN_TEST(test_renders_are_deterministic);
  RUN_TEST(test_corpus_ids_are_unique);
  RUN_TEST(test_memory_budget_holds_across_the_corpus);
  return UNITY_END();
}
