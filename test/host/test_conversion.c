#include "unity.h"

#include "conversion.h"

void setUp(void) {}
void tearDown(void) {}

static void test_factory_end_points(void)
{
  lambda_range_t r = conv_range_default();
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.68, conv_lambda(&r, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.02, conv_lambda(&r, 2.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.36, conv_lambda(&r, 5.0f));
  /* lambda = 0.68 + 0.136 V; AFR = 10 + 2 V on petrol */
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.68 + 0.136 * 1.7, conv_lambda(&r, 1.7f));
  /* The spec rounds 0.68 * 14.7 = 9.996 to 10. */
  TEST_ASSERT_FLOAT_WITHIN(0.01, 10.0 + 2.0 * 1.7, conv_lambda(&r, 1.7f) * conv_stoich(FUEL_PETROL, 0));
}

static void test_sequencer_levels(void)
{
  lambda_range_t r = conv_range_default();
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.9066, conv_lambda(&r, 1.666f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.1333, conv_lambda(&r, 3.333f));
}

static void test_custom_end_points(void)
{
  lambda_range_t r = { 0.60f, 3.40f };
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.60, conv_lambda(&r, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.00, conv_lambda(&r, 2.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 3.40, conv_lambda(&r, 5.0f));
  TEST_ASSERT_TRUE(conv_range_valid(0.60f, 3.40f));
  TEST_ASSERT_TRUE(conv_range_valid(0.68f, 1.36f));
  TEST_ASSERT_FALSE(conv_range_valid(0.59f, 1.36f));
  TEST_ASSERT_FALSE(conv_range_valid(0.68f, 3.41f));
  TEST_ASSERT_FALSE(conv_range_valid(1.36f, 0.68f));
}

static void test_fuels(void)
{
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 14.7, conv_stoich(FUEL_PETROL, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 14.08, conv_stoich(FUEL_E10, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 9.7, conv_stoich(FUEL_E85, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 12.5, conv_stoich(FUEL_CUSTOM, 12.5f));
  TEST_ASSERT_EQUAL_STRING("E10", conv_fuel_name(FUEL_E10));
}

static void test_codes(void)
{
  /* 5.00 V -> 3.00 V at the ADC -> 24000 codes at 125 uV. */
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 5.0, conv_code_to_volts(CAL_A_NOMINAL, 0, 24000));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 3.0, conv_code_to_adc_volts(24000));
  /* 16 V through 47k/10k gives 2.81 V at the ADC. */
  TEST_ASSERT_FLOAT_WITHIN(0.01, 16.0, conv_code_to_supply_v((int32_t)(2.807f / ADS_LSB_V)));
  /* One LSB is about 0.00003 lambda. */
  lambda_range_t r = conv_range_default();
  float d = conv_lambda(&r, CAL_A_NOMINAL) - conv_lambda(&r, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.000005, 0.0000283, d);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_factory_end_points);
  RUN_TEST(test_sequencer_levels);
  RUN_TEST(test_custom_end_points);
  RUN_TEST(test_fuels);
  RUN_TEST(test_codes);
  return UNITY_END();
}
