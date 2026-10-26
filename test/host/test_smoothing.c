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

static void test_decim_mean(void)
{
  decim_t d;
  decim_init(&d, 150);
  float m;
  uint64_t tm;
  TEST_ASSERT_FALSE(decim_feed(&d, 1000, 0, &m, &tm));
  TEST_ASSERT_FALSE(decim_feed(&d, 1002, 1163, &m, &tm));
  TEST_ASSERT_FALSE(decim_feed(&d, 1004, 2326, &m, &tm));
  TEST_ASSERT_TRUE(decim_feed(&d, 1006, 3489, &m, &tm));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1003.0, m);
  TEST_ASSERT_EQUAL_UINT(1744, tm);
  /* Past 2^32 us (71.6 minutes) time keeps rising. */
  uint64_t base = 5000000000ULL;
  for (int i = 0; i < 4; i++) decim_feed(&d, 1000, base + (uint64_t)i * 1163u, &m, &tm);
  TEST_ASSERT_TRUE(tm > base);
  TEST_ASSERT_EQUAL_UINT(0, d.spikes);
}

static void test_decim_rejects_spike(void)
{
  decim_t d;
  decim_init(&d, 150);
  float m;
  uint64_t tm;
  decim_feed(&d, 1000, 0, &m, &tm);
  decim_feed(&d, 9000, 1, &m, &tm); /* ignition spike */
  decim_feed(&d, 1002, 2, &m, &tm);
  TEST_ASSERT_TRUE(decim_feed(&d, 1004, 3, &m, &tm));
  /* Drops 9000 and 1000, keeps 1002 and 1004. */
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1003.0, m);
  TEST_ASSERT_EQUAL_UINT(1, d.spikes);
}

static void test_decim_ramp_unbiased(void)
{
  /* A fast real ramp spreads wider than the spike limit; trimming a linear
   * ramp still gives its mean. */
  decim_t d;
  decim_init(&d, 150);
  float m;
  uint64_t tm;
  int16_t c[4] = { 10000, 10200, 10400, 10600 };
  for (int i = 0; i < 3; i++) decim_feed(&d, c[i], (uint64_t)i, &m, &tm);
  TEST_ASSERT_TRUE(decim_feed(&d, c[3], 3, &m, &tm));
  TEST_ASSERT_FLOAT_WITHIN(1e-3, 10300.0, m);
}

static void test_decim_noise_reduction(void)
{
  /* Averaging 4 halves the noise. */
  decim_t d;
  decim_init(&d, 150);
  uint32_t seed = 7;
  double ss_raw = 0, ss_out = 0;
  int n_raw = 0, n_out = 0;
  for (int i = 0; i < 40000; i++) {
    seed = seed * 1664525u + 1013904223u;
    int16_t noise = (int16_t)((int32_t)(seed >> 26) - 32); /* uniform +/-32 */
    ss_raw += (double)noise * noise;
    n_raw++;
    float m;
    uint64_t tm;
    if (decim_feed(&d, (int16_t)(10000 + noise), (uint64_t)i, &m, &tm)) {
      ss_out += ((double)m - 10000.0) * ((double)m - 10000.0);
      n_out++;
    }
  }
  double ratio = sqrt(ss_out / n_out) / sqrt(ss_raw / n_raw);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 0.5, ratio);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_step_response);
  RUN_TEST(test_rate_independent);
  RUN_TEST(test_zero_tau_passes_through);
  RUN_TEST(test_nan_resets);
  RUN_TEST(test_decim_mean);
  RUN_TEST(test_decim_rejects_spike);
  RUN_TEST(test_decim_ramp_unbiased);
  RUN_TEST(test_decim_noise_reduction);
  return UNITY_END();
}
