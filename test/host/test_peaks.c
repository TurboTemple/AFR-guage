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

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_peaks_ready_only);
  return UNITY_END();
}
