#include "unity.h"

#include <math.h>

#include "afr_settings.h"
#include "calibration.h"

void setUp(void) {}
void tearDown(void) {}

static void test_defaults_are_valid(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  TEST_ASSERT_FALSE(settings_validate(&s));
  TEST_ASSERT_EQUAL_INT(FUEL_PETROL, s.fuel);
  TEST_ASSERT_EQUAL_INT(UNIT_LAMBDA, s.primary_unit);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.92, s.lean_load);
  TEST_ASSERT_EQUAL_UINT(100, s.lean_load_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.10, s.lean_part);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.72, s.rich);
  TEST_ASSERT_EQUAL_UINT(30, s.load_pct);
  TEST_ASSERT_EQUAL_UINT(3, s.closed_pct);
  TEST_ASSERT_FLOAT_IS_NAN(s.tps_closed_v);
  TEST_ASSERT_EQUAL_UINT(50, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(1, s.spartan_perf); /* 10 ms response */
  TEST_ASSERT_FALSE(s.cal_valid);
}

static void test_clamps(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.fuel = 9;
  s.lean_load = 1.5f;
  s.lean_load_ms = 5;
  s.lean_part = 0.9f;
  s.rich = 0.5f;
  s.rich_ms = 9000;
  s.load_pct = 95;
  s.smooth_ms = 900;
  s.spartan_perf = 7;
  s.bright_day = 0;
  s.fuel_custom = NAN;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_EQUAL_INT(FUEL_PETROL, s.fuel);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.05, s.lean_load);
  TEST_ASSERT_EQUAL_UINT(20, s.lean_load_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.00, s.lean_part);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.68, s.rich);
  TEST_ASSERT_EQUAL_UINT(3000, s.rich_ms);
  TEST_ASSERT_EQUAL_UINT(90, s.load_pct);
  TEST_ASSERT_EQUAL_UINT(500, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(2, s.spartan_perf);
  TEST_ASSERT_EQUAL_UINT(5, s.bright_day);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 14.7, s.fuel_custom);
  TEST_ASSERT_FALSE(settings_validate(&s));
}

static void test_closed_below_load(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.load_pct = 10;
  s.closed_pct = 20;
  settings_validate(&s);
  TEST_ASSERT_EQUAL_UINT(5, s.closed_pct);
}

static void test_tps_learn(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.tps_closed_v = 0.62f; /* half learnt: kept */
  TEST_ASSERT_FALSE(settings_validate(&s));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.62, s.tps_closed_v);
  s.tps_wot_v = 4.41f;
  TEST_ASSERT_FALSE(settings_validate(&s));
  TEST_ASSERT_TRUE(conv_tps_learnt(s.tps_closed_v, s.tps_wot_v));
  alarm_cfg_t a = settings_alarm_cfg(&s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 30.0, a.load_pct);
  s.tps_wot_v = 9.0f; /* implausible: dropped */
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_FLOAT_IS_NAN(s.tps_wot_v);
}

static void test_tps_pct(void)
{
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0, conv_tps_pct(0.60f, 0.60f, 4.40f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 50.0, conv_tps_pct(2.50f, 0.60f, 4.40f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 100.0, conv_tps_pct(4.60f, 0.60f, 4.40f)); /* clamped */
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 25.0, conv_tps_pct(3.45f, 4.40f, 0.60f)); /* falling sensor */
  TEST_ASSERT_FLOAT_IS_NAN(conv_tps_pct(0.02f, 0.60f, 4.40f));  /* open wire */
  TEST_ASSERT_FLOAT_IS_NAN(conv_tps_pct(2.0f, NAN, 4.40f));     /* not learnt */
  TEST_ASSERT_FLOAT_IS_NAN(conv_tps_pct(2.0f, 2.0f, 2.3f));     /* too narrow */
}

static void test_implausible_stored_cal_dropped(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.cal_valid = true;
  s.cal_a = CAL_A_NOMINAL * 1.01f;
  s.cal_b = 0.01f;
  TEST_ASSERT_FALSE(settings_validate(&s));
  TEST_ASSERT_TRUE(s.cal_valid);
  s.cal_a = CAL_A_NOMINAL * 1.2f;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_FALSE(s.cal_valid);
}

static void test_stoich(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.fuel = FUEL_E85;
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 9.7, settings_stoich(&s));
  s.fuel = FUEL_CUSTOM;
  s.fuel_custom = 6.4f;
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 6.4, settings_stoich(&s));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_valid);
  RUN_TEST(test_clamps);
  RUN_TEST(test_closed_below_load);
  RUN_TEST(test_tps_learn);
  RUN_TEST(test_tps_pct);
  RUN_TEST(test_implausible_stored_cal_dropped);
  RUN_TEST(test_stoich);
  return UNITY_END();
}
