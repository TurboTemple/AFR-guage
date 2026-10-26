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
  TEST_ASSERT_FALSE(s.show_afr);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.10, s.warn_lean);
  TEST_ASSERT_EQUAL_UINT(1000, s.warn_lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75, s.warn_rich);
  TEST_ASSERT_EQUAL_UINT(2000, s.warn_rich_ms);
  TEST_ASSERT_EQUAL_UINT(100, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(20, s.log_rate_hz);
  TEST_ASSERT_EQUAL_UINT(80, s.bright_day);
  TEST_ASSERT_EQUAL_UINT(25, s.bright_night);
  TEST_ASSERT_FALSE(s.cal_valid);
}

static void test_clamps(void)
{
  afr_settings_t s;
  settings_defaults(&s);
  s.fuel = 9;
  s.warn_lean = 1.5f;
  s.warn_lean_ms = 50;
  s.warn_rich = 0.5f;
  s.warn_rich_ms = 9000;
  s.smooth_ms = 900;
  s.bright_day = 0;
  s.bright_night = 200;
  s.fuel_custom = NAN;
  TEST_ASSERT_TRUE(settings_validate(&s));
  TEST_ASSERT_EQUAL_INT(FUEL_PETROL, s.fuel);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.30, s.warn_lean);
  TEST_ASSERT_EQUAL_UINT(200, s.warn_lean_ms);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.68, s.warn_rich);
  TEST_ASSERT_EQUAL_UINT(5000, s.warn_rich_ms);
  TEST_ASSERT_EQUAL_UINT(500, s.smooth_ms);
  TEST_ASSERT_EQUAL_UINT(5, s.bright_day);
  TEST_ASSERT_EQUAL_UINT(100, s.bright_night);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 14.7, s.fuel_custom);
  TEST_ASSERT_FALSE(settings_validate(&s));
}

static void test_log_rate_snaps(void)
{
  static const struct { uint8_t in, out; } cases[] = {
    { 0, 10 }, { 12, 10 }, { 20, 20 }, { 30, 20 }, { 40, 50 }, { 200, 50 },
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    afr_settings_t s;
    settings_defaults(&s);
    s.log_rate_hz = cases[i].in;
    settings_validate(&s);
    TEST_ASSERT_EQUAL_UINT(cases[i].out, s.log_rate_hz);
  }
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
  RUN_TEST(test_log_rate_snaps);
  RUN_TEST(test_implausible_stored_cal_dropped);
  RUN_TEST(test_stoich);
  return UNITY_END();
}
