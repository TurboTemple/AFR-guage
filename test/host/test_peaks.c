#include "unity.h"

#include <math.h>

#include "peaks.h"

void setUp(void) {}
void tearDown(void) {}

static const warn_cfg_t lean = { 1.10f, 1000 };
static const warn_cfg_t rich = { 0.75f, 2000 };

static void test_peaks_ready_only(void)
{
  peaks_t p;
  peaks_reset(&p);
  TEST_ASSERT_FLOAT_IS_NAN(p.rich);
  peaks_update(&p, ST_READY, 0.95f);
  peaks_update(&p, ST_READY, 0.82f);
  peaks_update(&p, ST_READY, 1.08f);
  peaks_update(&p, ST_WARMING, 0.70f);
  peaks_update(&p, ST_LEAN_RAIL, 1.36f);
  peaks_update(&p, ST_RICH_RAIL, 0.68f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.82, p.rich);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.08, p.lean);
  TEST_ASSERT_TRUE(p.rich_rail);
  TEST_ASSERT_TRUE(p.lean_rail);
  peaks_reset(&p);
  TEST_ASSERT_FLOAT_IS_NAN(p.lean);
  TEST_ASSERT_FALSE(p.lean_rail);
}

/* Feed a constant lambda in a state for dur ms at 5 ms steps. */
static warn_t run(warnings_t *w, uint32_t *t, uint32_t dur, gauge_state_t st, float lam)
{
  warn_t r = WARN_NONE;
  for (uint32_t end = *t + dur; *t < end; *t += 5) r = warnings_update(w, &lean, &rich, st, lam, *t);
  return r;
}

static void test_lean_needs_duration(void)
{
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 900, ST_READY, 1.12f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 50, ST_READY, 1.05f)); /* broken */
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 995, ST_READY, 1.12f));
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 10, ST_READY, 1.12f));
}

static void test_lean_hysteresis(void)
{
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 1100, ST_READY, 1.15f));
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 100, ST_READY, 1.09f));
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 100, ST_READY, 1.081f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 5, ST_READY, 1.079f));
}

static void test_lean_never_on_lean_rail(void)
{
  /* Overrun fuel cut sits on the lean rail. */
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 10000, ST_LEAN_RAIL, 1.36f));
  /* Returning from the rail starts the timer afresh. */
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 500, ST_READY, 1.20f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 1000, ST_READY, 1.00f));
}

static void test_rich_in_ready_and_rich_rail(void)
{
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 1000, ST_READY, 0.72f));
  TEST_ASSERT_EQUAL_INT(WARN_RICH, run(&w, &t, 1005, ST_RICH_RAIL, 0.68f));
  TEST_ASSERT_EQUAL_INT(WARN_RICH, run(&w, &t, 100, ST_READY, 0.765f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 5, ST_READY, 0.771f));
}

static void test_cleared_when_lambda_undefined(void)
{
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_RICH, run(&w, &t, 2100, ST_READY, 0.70f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 5, ST_WARMING, NAN));
}

static void test_lean_stays_on_rail_once_active(void)
{
  /* "Clears only when lambda comes back inside": the rail is not inside. */
  warnings_t w;
  warnings_reset(&w);
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 1100, ST_READY, 1.15f));
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, run(&w, &t, 500, ST_LEAN_RAIL, 1.36f));
  TEST_ASSERT_EQUAL_INT(WARN_NONE, run(&w, &t, 5, ST_READY, 1.0f));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_peaks_ready_only);
  RUN_TEST(test_lean_needs_duration);
  RUN_TEST(test_lean_hysteresis);
  RUN_TEST(test_lean_never_on_lean_rail);
  RUN_TEST(test_rich_in_ready_and_rich_rail);
  RUN_TEST(test_cleared_when_lambda_undefined);
  RUN_TEST(test_lean_stays_on_rail_once_active);
  return UNITY_END();
}
