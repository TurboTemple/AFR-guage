#include "unity.h"

#include "led_decoder.h"
#include "sim.h"

void setUp(void) { sim_srand(7); }
void tearDown(void) {}

static led_decoder_t d;
static uint32_t t_now;

/* Feed a blink pattern at 50 Hz for dur_ms. Returns the first time the
 * decoder reported `want` (relative to the start), or UINT32_MAX. */
static uint32_t feed(uint32_t dur_ms, uint32_t period, uint32_t on, float lit_v, led_pattern_t want)
{
  uint32_t start = t_now, first = UINT32_MAX;
  for (uint32_t end = t_now + dur_ms; t_now < end; t_now += 20) {
    float v = sim_blink(t_now - start, period, on) ? lit_v : 0.0f;
    v += 0.02f * sim_noise();
    if (led_decoder_feed(&d, t_now, v) == want && first == UINT32_MAX) first = t_now - start;
  }
  return first;
}

static void reset(void)
{
  led_decoder_init(&d);
  t_now = 100000;
}

static void test_steady(void)
{
  reset();
  uint32_t t = feed(3000, 0, 1, 3.0f, LED_STEADY);
  TEST_ASSERT_UINT_WITHIN(40, 1500, t);
  TEST_ASSERT_EQUAL_INT(LED_STEADY, d.pattern);
}

static void test_slow(void)
{
  reset();
  feed(1000, 0, 0, 3.0f, LED_SLOW);
  TEST_ASSERT_TRUE(feed(6000, 2000, 250, 3.0f, LED_SLOW) != UINT32_MAX);
  TEST_ASSERT_EQUAL_INT(LED_SLOW, d.pattern);
}

static void test_fast(void)
{
  reset();
  TEST_ASSERT_TRUE(feed(3000, 500, 250, 3.0f, LED_FAST) != UINT32_MAX);
  TEST_ASSERT_EQUAL_INT(LED_FAST, d.pattern);
}

static void test_very_slow(void)
{
  reset();
  TEST_ASSERT_TRUE(feed(20000, 8000, 250, 3.0f, LED_VERY_SLOW) != UINT32_MAX);
  TEST_ASSERT_EQUAL_INT(LED_VERY_SLOW, d.pattern);
}

static void test_off(void)
{
  reset();
  feed(3000, 0, 1, 3.0f, LED_STEADY);
  uint32_t t = feed(12000, 0, 0, 3.0f, LED_OFF);
  TEST_ASSERT_UINT_WITHIN(40, 10000, t);
}

static void test_low_lit_voltage(void)
{
  /* The lit voltage is undocumented: a dim 1.0 V still decodes. */
  reset();
  TEST_ASSERT_TRUE(feed(6000, 2000, 300, 1.0f, LED_SLOW) != UINT32_MAX);
  TEST_ASSERT_TRUE(feed(3000, 0, 1, 1.0f, LED_STEADY) != UINT32_MAX);
}

static void test_below_floor_is_off(void)
{
  /* Under the 0.5 V floor nothing is ever on. */
  reset();
  TEST_ASSERT_TRUE(feed(12000, 0, 1, 0.4f, LED_OFF) != UINT32_MAX);
}

static void test_threshold_tracks_max(void)
{
  reset();
  feed(2000, 0, 1, 3.0f, LED_STEADY);
  TEST_ASSERT_FLOAT_WITHIN(0.05, 1.5, led_decoder_threshold(&d, t_now));
  feed(11000, 0, 0, 3.0f, LED_OFF);
  TEST_ASSERT_FLOAT_WITHIN(0.001, LED_FLOOR_V, led_decoder_threshold(&d, t_now));
}

/* Acceptance: the right state within 3 s of every pattern change. */
static void test_changes_within_3s(void)
{
  static const struct { uint32_t period, on; led_pattern_t p; } pats[] = {
    { 0, 1, LED_STEADY }, { 2000, 250, LED_SLOW }, { 500, 250, LED_FAST },
    { 2000, 1000, LED_SLOW }, { 0, 1, LED_STEADY }, { 500, 100, LED_FAST },
    { 0, 1, LED_STEADY }, { 2000, 500, LED_SLOW }, { 500, 250, LED_FAST },
    { 2000, 250, LED_SLOW },
  };
  const unsigned n = sizeof(pats) / sizeof(pats[0]);
  for (unsigned i = 0; i < n; i++) {
    for (unsigned j = 0; j < n; j++) {
      if (pats[i].p == pats[j].p) continue;
      reset();
      feed(6000, pats[i].period, pats[i].on, 3.0f, pats[i].p);
      TEST_ASSERT_EQUAL_INT(pats[i].p, d.pattern);
      uint32_t t = feed(6000, pats[j].period, pats[j].on, 3.0f, pats[j].p);
      char msg[64];
      snprintf(msg, sizeof msg, "pattern %u -> %u took %lu ms", i, j, (unsigned long)t);
      TEST_ASSERT_TRUE_MESSAGE(t <= 3000, msg);
    }
  }
}

static void test_names(void)
{
  TEST_ASSERT_EQUAL_STRING("VERY_SLOW", led_pattern_name(LED_VERY_SLOW));
  TEST_ASSERT_EQUAL_INT(LED_FAST, led_pattern_parse("FAST"));
  TEST_ASSERT_EQUAL_INT(LED_UNKNOWN, led_pattern_parse("nonsense"));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_steady);
  RUN_TEST(test_slow);
  RUN_TEST(test_fast);
  RUN_TEST(test_very_slow);
  RUN_TEST(test_off);
  RUN_TEST(test_low_lit_voltage);
  RUN_TEST(test_below_floor_is_off);
  RUN_TEST(test_threshold_tracks_max);
  RUN_TEST(test_changes_within_3s);
  RUN_TEST(test_names);
  return UNITY_END();
}
