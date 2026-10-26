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
  cfg = alarms_default_cfg(); /* load 0.90/100 ms, part 1.10/500 ms, rich 0.72/500 ms */
  t = 0;
}

/* Hold a state, lambda and throttle for dur ms at about 215 Hz. Returns the
 * first time (from the start) the alarm became `want`, or UINT32_MAX. */
static uint32_t hold(uint32_t dur, gauge_state_t st, float lam, float tps, alarm_t want)
{
  uint32_t start = t, first = UINT32_MAX;
  for (uint32_t end = t + dur; t < end; t += 5) {
    if (alarms_update(&a, &cfg, st, lam, tps, t) == want && first == UINT32_MAX) first = t - start;
  }
  return first;
}

static alarm_t now(void) { return a.live; }

static void test_lean_at_load_trips_fast(void)
{
  reset();
  hold(1000, ST_READY, 0.84f, 95.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  uint32_t at = hold(500, ST_READY, 0.93f, 95.0f, ALARM_LEAN_LOAD);
  TEST_ASSERT_UINT_WITHIN(5, 100, at);
}

static void test_lean_rail_at_load_is_danger(void)
{
  reset();
  hold(500, ST_READY, 0.84f, 100.0f, ALARM_NONE);
  uint32_t at = hold(300, ST_LEAN_RAIL, 1.36f, 100.0f, ALARM_LEAN_LOAD);
  TEST_ASSERT_UINT_WITHIN(5, 100, at);
}

static void test_fuel_cut_never_alarms(void)
{
  /* Lift after a pass: closed throttle, lean rail for seconds. */
  reset();
  hold(500, ST_READY, 0.84f, 100.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(8000, ST_LEAN_RAIL, 1.36f, 0.0f, ALARM_LEAN));
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(10, ST_LEAN_RAIL, 1.36f, 0.0f, ALARM_LEAN_LOAD));
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_lift_through_transition(void)
{
  /* Throttle closing from 100 % to 0 % in 60 ms while the mixture leans:
   * lean at load must not trip in that window. */
  reset();
  hold(500, ST_READY, 0.84f, 100.0f, ALARM_NONE);
  for (int i = 0; i < 12; i++) {
    float tps = 100.0f - (float)i * 9.0f;
    alarms_update(&a, &cfg, ST_READY, 0.84f + 0.02f * (float)i, tps, t);
    t += 5;
  }
  hold(3000, ST_LEAN_RAIL, 1.36f, 0.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_part_throttle_lean(void)
{
  reset();
  /* 20 % throttle, under the load threshold: the part-throttle limit. */
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(2000, ST_READY, 1.05f, 20.0f, ALARM_LEAN));
  uint32_t at = hold(1000, ST_READY, 1.15f, 20.0f, ALARM_LEAN);
  TEST_ASSERT_UINT_WITHIN(5, 500, at);
  /* Throttle closes: fuel cut territory, the alarm clears. */
  hold(10, ST_LEAN_RAIL, 1.36f, 0.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_load_hysteresis(void)
{
  reset();
  hold(100, ST_READY, 0.85f, 31.0f, ALARM_NONE);
  TEST_ASSERT_TRUE(alarms_under_load(&a));
  hold(100, ST_READY, 0.85f, 26.0f, ALARM_NONE);
  TEST_ASSERT_TRUE(alarms_under_load(&a)); /* inside the 5 % band */
  hold(10, ST_READY, 0.85f, 24.0f, ALARM_NONE);
  TEST_ASSERT_FALSE(alarms_under_load(&a));
}

static void test_clear_hysteresis(void)
{
  reset();
  hold(200, ST_READY, 0.95f, 90.0f, ALARM_LEAN_LOAD);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN_LOAD, now());
  hold(50, ST_READY, 0.885f, 90.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN_LOAD, now()); /* 0.885 is not 0.02 inside */
  hold(5, ST_READY, 0.875f, 90.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_short_blip_ignored(void)
{
  reset();
  for (int i = 0; i < 20; i++) {
    hold(80, ST_READY, 0.95f, 90.0f, ALARM_NONE); /* under the 100 ms hold */
    hold(20, ST_READY, 0.85f, 90.0f, ALARM_NONE);
  }
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_rich(void)
{
  reset();
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(2000, ST_READY, 0.74f, 90.0f, ALARM_RICH));
  TEST_ASSERT_UINT_WITHIN(5, 500, hold(1000, ST_READY, 0.70f, 90.0f, ALARM_RICH));
  hold(10, ST_READY, 0.75f, 90.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  TEST_ASSERT_UINT_WITHIN(5, 500, hold(1000, ST_RICH_RAIL, 0.68f, 0.0f, ALARM_RICH));
}

static void test_lean_beats_rich(void)
{
  reset();
  hold(600, ST_READY, 0.70f, 90.0f, ALARM_RICH);
  TEST_ASSERT_EQUAL_INT(ALARM_RICH, now());
  /* A snap to lean under load: rich clears, lean at load trips. */
  TEST_ASSERT_UINT_WITHIN(5, 100, hold(300, ST_READY, 0.95f, 90.0f, ALARM_LEAN_LOAD));
}

static void test_without_tps(void)
{
  reset();
  /* No throttle signal: no load alarm, lean rail ignored (may be fuel
   * cut), part-throttle limit still applies on lambda. */
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(3000, ST_READY, 0.95f, NAN, ALARM_LEAN_LOAD));
  TEST_ASSERT_EQUAL_UINT(UINT32_MAX, hold(5000, ST_LEAN_RAIL, 1.36f, NAN, ALARM_LEAN));
  TEST_ASSERT_UINT_WITHIN(5, 500, hold(1000, ST_READY, 1.15f, NAN, ALARM_LEAN));
}

static void test_undefined_lambda_clears(void)
{
  reset();
  hold(200, ST_READY, 0.95f, 90.0f, ALARM_LEAN_LOAD);
  hold(5, ST_WARMING, NAN, 90.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_event_latched_until_ack(void)
{
  reset();
  hold(500, ST_READY, 0.84f, 95.0f, ALARM_NONE);
  hold(150, ST_READY, 0.93f, 95.0f, ALARM_NONE);
  hold(200, ST_READY, 0.97f, 95.0f, ALARM_NONE);
  hold(100, ST_READY, 0.85f, 95.0f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  alarm_event_t ev;
  TEST_ASSERT_TRUE(alarms_event(&a, &ev));
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN_LOAD, ev.type);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.97, ev.lambda);  /* the worst */
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 95.0, ev.tps);
  TEST_ASSERT_UINT_WITHIN(10, 250, ev.dur_ms);       /* 0.93 from 100 ms, then 0.97 */
  /* A later, milder alarm does not replace it. */
  hold(600, ST_READY, 0.70f, 95.0f, ALARM_RICH);
  hold(10, ST_READY, 0.85f, 95.0f, ALARM_NONE);
  TEST_ASSERT_TRUE(alarms_event(&a, &ev));
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN_LOAD, ev.type);
  alarms_ack(&a);
  TEST_ASSERT_FALSE(alarms_event(&a, &ev));
}

static void test_event_in_progress_shown(void)
{
  reset();
  hold(200, ST_READY, 0.95f, 90.0f, ALARM_NONE);
  alarm_event_t ev;
  TEST_ASSERT_TRUE(alarms_event(&a, &ev));
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN_LOAD, ev.type);
  alarms_ack(&a); /* acknowledging does not hide a live alarm */
  TEST_ASSERT_TRUE(alarms_event(&a, &ev));
}

static void test_chars(void)
{
  TEST_ASSERT_EQUAL_INT('D', alarm_char(ALARM_LEAN_LOAD));
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, alarm_parse_char('L'));
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, alarm_parse_char('0'));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_lean_at_load_trips_fast);
  RUN_TEST(test_lean_rail_at_load_is_danger);
  RUN_TEST(test_fuel_cut_never_alarms);
  RUN_TEST(test_lift_through_transition);
  RUN_TEST(test_part_throttle_lean);
  RUN_TEST(test_load_hysteresis);
  RUN_TEST(test_clear_hysteresis);
  RUN_TEST(test_short_blip_ignored);
  RUN_TEST(test_rich);
  RUN_TEST(test_lean_beats_rich);
  RUN_TEST(test_without_tps);
  RUN_TEST(test_undefined_lambda_clears);
  RUN_TEST(test_event_latched_until_ack);
  RUN_TEST(test_event_in_progress_shown);
  RUN_TEST(test_chars);
  return UNITY_END();
}
