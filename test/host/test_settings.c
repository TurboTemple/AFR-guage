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
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.05, s.lean);
  TEST_ASSERT_EQUAL_UINT(300, s.lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75, s.rich);
  TEST_ASSERT_EQUAL_UINT(300, s.rich_ms);
  TEST_ASSERT_FALSE(s.race);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.92, s.race_lean);
  TEST_ASSERT_EQUAL_UINT(15, s.race_lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.86, s.race_target);
  TEST_ASSERT_EQUAL_UINT(50, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(1, s.spartan_perf); /* 10 ms response */
  TEST_ASSERT_FALSE(s.cal_valid);
}

static void test_clamps(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.fuel = 9;
  s.lean = 1.5f;
  s.lean_ms = 5;
  s.rich = 0.5f;
  s.rich_ms = 9000;
  s.smooth_ms = 900;
  s.spartan_perf = 7;
  s.bright_day = 0;
  s.fuel_custom = NAN;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_EQUAL_INT(FUEL_PETROL, s.fuel);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.30, s.lean);
  TEST_ASSERT_EQUAL_UINT(100, s.lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.68, s.rich);
  TEST_ASSERT_EQUAL_UINT(3000, s.rich_ms);
  TEST_ASSERT_EQUAL_UINT(500, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(2, s.spartan_perf);
  TEST_ASSERT_EQUAL_UINT(5, s.bright_day);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 14.7, s.fuel_custom);
  TEST_ASSERT_FALSE(settings_validate(&s));
}

static void test_rich_kept_below_lean(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.lean = 0.90f;
  s.rich = 0.88f;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.85, s.rich);
  alarm_cfg_t a = settings_alarm_cfg(&s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.90, a.lean);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.85, a.rich);
  TEST_ASSERT_EQUAL_UINT(300, a.lean_ms);
}

static void test_race_mode(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.00, settings_target(&s));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.05, settings_alarm_cfg(&s).lean);
  s.race = true;
  alarm_cfg_t a = settings_alarm_cfg(&s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.92, a.lean);
  TEST_ASSERT_EQUAL_UINT(15, a.lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75, a.rich);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.86, settings_target(&s));
  /* The target is kept on the safe side of the limit. */
  s.race_lean = 0.88f;
  s.race_target = 0.90f;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.85, s.race_target);
  s.race_lean_ms = 9000;
  settings_validate(&s);
  TEST_ASSERT_EQUAL_UINT(500, s.race_lean_ms);
  /* And rich stays clear of the race limit too. */
  s.race_lean = 0.80f;
  s.rich = 0.78f;
  settings_validate(&s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75, s.rich);
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
  RUN_TEST(test_rich_kept_below_lean);
  RUN_TEST(test_race_mode);
  RUN_TEST(test_implausible_stored_cal_dropped);
  RUN_TEST(test_stoich);
  return UNITY_END();
}
