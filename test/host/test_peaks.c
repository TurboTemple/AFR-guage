#include "unity.h"

#include <math.h>

#include "peaks.h"

void setUp(void) {}
void tearDown(void) {}

static void test_peaks_ready_only(void)
{
  peaks_t p;
  peaks_reset(&p);
  TEST_ASSERT_FLOAT_IS_NAN(p.rich);
  peaks_update(&p, ST_READY, 0.95f, true);
  peaks_update(&p, ST_READY, 0.82f, true);
  peaks_update(&p, ST_READY, 1.08f, true);
  peaks_update(&p, ST_WARMING, 0.70f, true);
  peaks_update(&p, ST_LEAN_RAIL, 1.36f, true);
  peaks_update(&p, ST_RICH_RAIL, 0.68f, true);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.82, p.rich);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.08, p.lean);
  TEST_ASSERT_TRUE(p.rich_rail);
  TEST_ASSERT_TRUE(p.lean_rail);
  peaks_reset(&p);
  TEST_ASSERT_FLOAT_IS_NAN(p.lean);
  TEST_ASSERT_FALSE(p.lean_rail);
}

static void test_peaks_only_when_counting(void)
{
  /* Off load (lift, idle, fuel cut) does not count. */
  peaks_t p;
  peaks_reset(&p);
  peaks_update(&p, ST_READY, 1.02f, false);
  peaks_update(&p, ST_LEAN_RAIL, 1.36f, false);
  TEST_ASSERT_FLOAT_IS_NAN(p.lean);
  TEST_ASSERT_FALSE(p.lean_rail);
  peaks_update(&p, ST_READY, 0.84f, true);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.84, p.lean);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_peaks_ready_only);
  RUN_TEST(test_peaks_only_when_counting);
  return UNITY_END();
}
