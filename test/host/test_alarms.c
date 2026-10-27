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

static void race_cfg(void)
{
  cfg.lean = 0.92f;
  cfg.lean_ms = 15;
}

static void test_race_trips_in_15ms(void)
{
  reset();
  race_cfg();
  hold(1000, ST_READY, 0.86f, ALARM_NONE);
  uint32_t at = hold(200, ST_READY, 0.93f, ALARM_LEAN);
  TEST_ASSERT_UINT_WITHIN(5, 15, at);
  /* A healthy pass at 0.84-0.89 never trips. */
  reset();
  race_cfg();
  for (int i = 0; i < 50; i++) {
    hold(100, ST_READY, 0.84f, ALARM_NONE);
    hold(100, ST_READY, 0.89f, ALARM_NONE);
  }
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
}

static void test_ring_after_lean_out(void)
{
  reset();
  race_cfg();
  hold(2000, ST_READY, 0.86f, ALARM_NONE);
  hold(400, ST_READY, 0.97f, ALARM_LEAN);  /* lean-out mid pass */
  hold(300, ST_READY, 0.99f, ALARM_LEAN);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));  /* not while it is live */
  hold(5, ST_READY, 0.87f, ALARM_NONE);   /* back to safe */
  TEST_ASSERT_TRUE(alarms_ring(&a, t));
  lean_event_t ev;
  TEST_ASSERT_TRUE(alarms_take_event(&a, &ev));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.99, ev.peak);
  TEST_ASSERT_UINT_WITHIN(10, 700, ev.dur_ms);
  TEST_ASSERT_FALSE(alarms_take_event(&a, &ev));
  /* On for 5 minutes, then off. */
  uint32_t lit = t;
  TEST_ASSERT_TRUE(alarms_ring(&a, lit + RING_MS - 1000));
  TEST_ASSERT_FALSE(alarms_ring(&a, lit + RING_MS + 1000));
  /* A long press clears it early. */
  alarms_clear_ring(&a);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
}

static void test_ring_street(void)
{
  reset();
  hold(2000, ST_READY, 1.00f, ALARM_NONE);
  hold(800, ST_READY, 1.12f, ALARM_LEAN);
  hold(5, ST_READY, 1.00f, ALARM_NONE);
  TEST_ASSERT_TRUE(alarms_ring(&a, t));
}

static void test_no_ring_for_fuel_cut(void)
{
  /* A lift: rich, through the lean limit to the rail, back to idle. */
  reset();
  hold(2000, ST_READY, 1.00f, ALARM_NONE);
  hold(150, ST_READY, 1.20f, ALARM_NONE);
  hold(3000, ST_LEAN_RAIL, 1.36f, ALARM_LEAN);
  hold(500, ST_READY, 1.00f, ALARM_NONE);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, now());
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
  /* Race mode: the pass ends with a lift; idle stays above the limit. */
  reset();
  race_cfg();
  hold(9000, ST_READY, 0.86f, ALARM_NONE);
  hold(200, ST_READY, 1.05f, ALARM_LEAN);
  hold(4000, ST_LEAN_RAIL, 1.36f, ALARM_LEAN);
  hold(60000, ST_READY, 1.00f, ALARM_LEAN);
  hold(500, ST_READY, 0.86f, ALARM_NONE); /* the next launch */
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
}

static void test_no_ring_for_idle_or_cruise(void)
{
  /* Race mode switched on at idle: lean from the start, then a launch. */
  reset();
  race_cfg();
  hold(20000, ST_READY, 1.00f, ALARM_LEAN);
  hold(500, ST_READY, 0.86f, ALARM_NONE);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
  /* Eased off after a pass and cruised for a minute: too long to be a
   * lean-out. */
  hold(1000, ST_READY, 1.00f, ALARM_LEAN);
  hold(60000, ST_READY, 1.00f, ALARM_LEAN);
  hold(500, ST_READY, 0.86f, ALARM_NONE);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
}

static void test_rearm_on_mode_change(void)
{
  reset();
  hold(2000, ST_READY, 1.00f, ALARM_NONE);
  race_cfg();
  alarms_rearm(&a);
  /* Idle in race mode right after the switch: warns, but never a ring. */
  TEST_ASSERT_UINT_WITHIN(5, 15, hold(3000, ST_READY, 1.00f, ALARM_LEAN));
  hold(500, ST_READY, 0.86f, ALARM_NONE);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
  /* A ring lit before a switch stays. */
  hold(300, ST_READY, 0.96f, ALARM_LEAN);
  hold(5, ST_READY, 0.86f, ALARM_NONE);
  TEST_ASSERT_TRUE(alarms_ring(&a, t));
  cfg = alarms_default_cfg();
  alarms_rearm(&a);
  TEST_ASSERT_TRUE(alarms_ring(&a, t));
}

static void test_no_ring_when_sensor_drops_out(void)
{
  reset();
  hold(2000, ST_READY, 1.00f, ALARM_NONE);
  hold(800, ST_READY, 1.12f, ALARM_LEAN);
  hold(5, ST_WARMING, NAN, ALARM_NONE);
  TEST_ASSERT_FALSE(alarms_ring(&a, t));
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
  RUN_TEST(test_race_trips_in_15ms);
  RUN_TEST(test_ring_after_lean_out);
  RUN_TEST(test_ring_street);
  RUN_TEST(test_no_ring_for_fuel_cut);
  RUN_TEST(test_no_ring_for_idle_or_cruise);
  RUN_TEST(test_rearm_on_mode_change);
  RUN_TEST(test_no_ring_when_sensor_drops_out);
  RUN_TEST(test_chars);
  return UNITY_END();
}
