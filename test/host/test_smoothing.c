#include "unity.h"

#include <math.h>

#include "smoothing.h"

void setUp(void) {}
void tearDown(void) {}

static void test_step_response(void)
{
  smoother_t s;
  smoother_reset(&s);
  smoother_feed(&s, 0.80f, 0, 100);
  float y = 0.0f;
  for (uint32_t t = 5; t <= 100; t += 5) y = smoother_feed(&s, 1.00f, t, 100);
  /* One time constant: 63.2 % of the step. */
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.80 + 0.20 * (1.0 - exp(-1.0)), y);
  for (uint32_t t = 105; t <= 500; t += 5) y = smoother_feed(&s, 1.00f, t, 100);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 1.00, y);
}

static void test_rate_independent(void)
{
  /* Same tau, different sample rates, same answer. */
  smoother_t a, b;
  smoother_reset(&a);
  smoother_reset(&b);
  smoother_feed(&a, 0.0f, 0, 200);
  smoother_feed(&b, 0.0f, 0, 200);
  float ya = 0, yb = 0;
  for (uint32_t t = 5; t <= 300; t += 5) ya = smoother_feed(&a, 1.0f, t, 200);
  for (uint32_t t = 50; t <= 300; t += 50) yb = smoother_feed(&b, 1.0f, t, 200);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, ya, yb);
}

static void test_zero_tau_passes_through(void)
{
  smoother_t s;
  smoother_reset(&s);
  smoother_feed(&s, 0.8f, 0, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.1, smoother_feed(&s, 1.1f, 5, 0));
}

static void test_nan_resets(void)
{
  smoother_t s;
  smoother_reset(&s);
  smoother_feed(&s, 0.8f, 0, 100);
  TEST_ASSERT_FLOAT_IS_NAN(smoother_feed(&s, NAN, 5, 100));
  /* The next value starts the filter afresh rather than ramping. */
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.2, smoother_feed(&s, 1.2f, 10, 100));
}

static void test_median_removes_spike(void)
{
  median3_t m;
  median3_reset(&m);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.0, median3_feed(&m, 2.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.1, median3_feed(&m, 2.1f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.1, median3_feed(&m, 4.9f)); /* spike */
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.2, median3_feed(&m, 2.2f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.3, median3_feed(&m, 2.3f));
  /* A real step passes after one sample of delay. */
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.3, median3_feed(&m, 3.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 3.0, median3_feed(&m, 3.0f));
}

static void test_median_all_orders(void)
{
  static const float v[6][3] = { {1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1} };
  for (int i = 0; i < 6; i++) {
    median3_t m;
    median3_reset(&m);
    median3_feed(&m, v[i][0]);
    median3_feed(&m, v[i][1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.0, median3_feed(&m, v[i][2]));
  }
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_step_response);
  RUN_TEST(test_rate_independent);
  RUN_TEST(test_zero_tau_passes_through);
  RUN_TEST(test_nan_resets);
  RUN_TEST(test_median_removes_spike);
  RUN_TEST(test_median_all_orders);
  return UNITY_END();
}
