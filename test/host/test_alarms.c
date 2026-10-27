#include "unity.h"

#include <math.h>

#include "alarms.h"

void setUp(void) {}
void tearDown(void) {}

static alarms_t a;
static alarm_cfg_t cfg;
static uint32_t t;

static void reset(void)
{
  alarms_reset(&a);
  cfg = alarms_default_cfg(); /* lean 1.05/300 ms, rich 0.75/300 ms */
  t = 0;
}

/* Hold a state and lambda for dur ms at about 215 Hz. Returns the first
 * time (from the start) the warning became `want`, or UINT32_MAX. */
static uint32_t hold(uint32_t dur, gauge_state_t st, float lam, alarm_t want)
{
  uint32_t start = t, first = UINT32_MAX;
  for (uint32_t end = t + dur; t < end; t += 5) {
    if (alarms_update(&a, &cfg, st, lam, t) == want && first == UINT32_MAX) first = t - start;
  }
  return first;
}

static alarm_t now(void) { return a.live; }

static void test_defaults(void)
{
  reset();
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.05, cfg.lean);
  TEST_ASSERT_EQUAL_UINT(300, cfg.lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75, cfg.rich);
  TEST_ASSERT_EQUAL_UINT(300, cfg.rich_ms);
}

static void test_lean_after_hold(void)
{
  reset();
  hold(1000, ST_READY, 0.86f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  TEST_ASSERT_UINT_WITHIN(5, 300, hold(1000, ST_READY, 1.08f, ALARM_LEAN));
}

static void test_lean_rail_warns(void)
{
  /* No throttle signal: the lean rail warns, fuel cut included. */
  reset();
  TEST_ASSERT_UINT_WITHIN(5, 300, hold(1000, ST_LEAN_RAIL, 1.36f, ALARM_LEAN));
  hold(10, ST_READY, 1.00f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_closed_loop_quiet(void)
{
  /* Idle and cruise: closed loop swinging 0.97-1.03 never warns. */
  reset();
  for (int i = 0; i < 40; i++) {
    hold(250, ST_READY, 0.97f, ALARM_NONE);
    hold(250, ST_READY, 1.03f, ALARM_NONE);
  }
  /* A full-throttle tune at 0.82-0.88 never warns either. */
  for (int i = 0; i < 20; i++) {
    hold(500, ST_READY, 0.82f, ALARM_NONE);
    hold(500, ST_READY, 0.88f, ALARM_NONE);
  }
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_short_blip_ignored(void)
{
  reset();
  for (int i = 0; i < 20; i++) {
    hold(250, ST_READY, 1.10f, ALARM_NONE); /* under the 300 ms hold */
    hold(50, ST_READY, 0.95f, ALARM_NONE);
  }
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_clear_hysteresis(void)
{
  reset();
  hold(400, ST_READY, 1.10f, ALARM_LEAN);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, now());
  hold(50, ST_READY, 1.035f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, now()); /* 1.035 is not 0.02 inside */
  hold(5, ST_READY, 1.025f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_rich(void)
{
  reset();
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(2000, ST_READY, 0.77f, ALARM_RICH));
  TEST_ASSERT_UINT_WITHIN(5, 300, hold(1000, ST_READY, 0.73f, ALARM_RICH));
  hold(10, ST_READY, 0.78f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  TEST_ASSERT_UINT_WITHIN(5, 300, hold(1000, ST_RICH_RAIL, 0.68f, ALARM_RICH));
}

static void test_rich_to_lean(void)
{
  reset();
  hold(400, ST_READY, 0.72f, ALARM_RICH);
  TEST_ASSERT_EQUAL_INT(ALARM_RICH, now());
  /* Rich clears at once, lean after its hold. */
  TEST_ASSERT_UINT_WITHIN(5, 300, hold(600, ST_READY, 1.10f, ALARM_LEAN));
}

static void test_undefined_lambda_clears(void)
{
  reset();
  hold(400, ST_READY, 1.10f, ALARM_LEAN);
  hold(5, ST_WARMING, NAN, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  /* And a warm-up never warns. */
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(2000, ST_WARMING, NAN, ALARM_LEAN));
}

static void test_custom_limits(void)
{
  reset();
  cfg.lean = 0.92f; /* a full-throttle limit for the strip */
  cfg.lean_ms = 100;
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(1000, ST_READY, 0.88f, ALARM_LEAN));
  TEST_ASSERT_UINT_WITHIN(5, 100, hold(500, ST_READY, 0.95f, ALARM_LEAN));
}

static void test_chars(void)
{
  TEST_ASSERT_EQUAL_INT('L', alarm_char(ALARM_LEAN));
  TEST_ASSERT_EQUAL_INT('R', alarm_char(ALARM_RICH));
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, alarm_parse_char('L'));
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, alarm_parse_char('0'));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_lean_after_hold);
  RUN_TEST(test_lean_rail_warns);
  RUN_TEST(test_closed_loop_quiet);
  RUN_TEST(test_short_blip_ignored);
  RUN_TEST(test_clear_hysteresis);
  RUN_TEST(test_rich);
  RUN_TEST(test_rich_to_lean);
  RUN_TEST(test_undefined_lambda_clears);
  RUN_TEST(test_custom_limits);
  RUN_TEST(test_chars);
  return UNITY_END();
}
