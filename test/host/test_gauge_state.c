#include "unity.h"

#include <math.h>

#include "gauge_state.h"

void setUp(void) {}
void tearDown(void) {}

static gauge_sm_t sm;
static gs_input_t in;

static void begin(gauge_state_t at)
{
  gs_init(&sm, 0);
  gs_init_complete(&sm, 0);
  in = (gs_input_t){ .t_ms = 0, .adc_ok = true, .v_out = 0.0f, .led = LED_OFF,
                     .supply_v = NAN, .cal_complete = false };
  /* Force the starting state directly. */
  sm.state = at;
  sm.since_ms = 0;
}

static gauge_state_t step(uint32_t dt)
{
  in.t_ms += dt;
  return gs_update(&sm, &in);
}

static void test_boot_to_no_spartan(void)
{
  gs_init(&sm, 0);
  TEST_ASSERT_EQUAL_INT(ST_BOOT, sm.state);
  in = (gs_input_t){ .t_ms = 5, .adc_ok = true, .v_out = 2.0f, .supply_v = NAN };
  TEST_ASSERT_EQUAL_INT(ST_BOOT, gs_update(&sm, &in));
  gs_init_complete(&sm, 5);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, sm.state);
}

static void test_no_spartan_to_sequencer_by_output(void)
{
  begin(ST_NO_SPARTAN);
  in.v_out = 0.9f;
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, step(5));
  in.v_out = 1.1f;
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, step(5));
}

static void test_no_spartan_to_sequencer_by_supply(void)
{
  begin(ST_NO_SPARTAN);
  in.supply_v = 8.9f;
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, step(5));
  in.supply_v = 9.1f;
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, step(5));
}

static void test_sequencer_to_warming(void)
{
  begin(ST_SEQUENCER);
  in.v_out = 1.666f;
  in.led = LED_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, step(5000));
  in.cal_complete = true;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));

  begin(ST_SEQUENCER);
  in.v_out = 2.0f;
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, step(14995));
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
}

static void test_warming_wait_exhaust(void)
{
  begin(ST_WARMING);
  in.v_out = 2.0f;
  in.led = LED_VERY_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_WAIT_EXHAUST, step(5));
  in.led = LED_UNKNOWN;
  TEST_ASSERT_EQUAL_INT(ST_WAIT_EXHAUST, step(5));
  in.led = LED_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
}

static void test_warming_to_ready_and_back(void)
{
  begin(ST_WARMING);
  in.v_out = 2.35f;
  in.led = LED_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
  in.led = LED_STEADY;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  in.led = LED_UNKNOWN; /* falling edge, not yet classified: hold */
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  in.led = LED_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
}

static void test_rails(void)
{
  begin(ST_READY);
  in.led = LED_STEADY;
  in.v_out = 0.019f;
  TEST_ASSERT_EQUAL_INT(ST_RICH_RAIL, step(5));
  in.v_out = 0.02f;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  in.v_out = 4.981f;
  TEST_ASSERT_EQUAL_INT(ST_LEAN_RAIL, step(5));
  in.v_out = 4.98f;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  /* The LED still rules on a rail. */
  in.v_out = 5.0f;
  TEST_ASSERT_EQUAL_INT(ST_LEAN_RAIL, step(5));
  in.led = LED_SLOW;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
}

static void test_too_hot(void)
{
  begin(ST_READY);
  in.led = LED_FAST;
  in.v_out = 2.0f;
  TEST_ASSERT_EQUAL_INT(ST_TOO_HOT, step(5));
  in.led = LED_UNKNOWN;
  TEST_ASSERT_EQUAL_INT(ST_TOO_HOT, step(5));
  in.led = LED_STEADY;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
}

static void test_loss_by_supply(void)
{
  begin(ST_READY);
  in.led = LED_STEADY;
  in.v_out = 2.0f;
  in.supply_v = 13.8f;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  in.supply_v = 7.9f;
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, step(5));
  /* Output above 1 V cannot restart it while the supply reads off. */
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, step(5));
}

static void test_loss_by_led_and_output(void)
{
  begin(ST_WARMING);
  in.led = LED_OFF;
  in.v_out = 0.04f;
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(5));
  TEST_ASSERT_EQUAL_INT(ST_WARMING, step(9990));
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, step(10));
}

static void test_loss_needs_both(void)
{
  begin(ST_READY);
  in.led = LED_OFF;      /* LED circuit open */
  in.v_out = 2.0f;       /* but output is live */
  TEST_ASSERT_EQUAL_INT(ST_READY, step(11000));
}

static void test_i2c_fault_and_recovery(void)
{
  begin(ST_READY);
  in.led = LED_STEADY;
  in.v_out = 2.0f;
  in.adc_ok = false;
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(5));
  TEST_ASSERT_EQUAL_INT(FAULT_I2C, sm.fault);
  in.adc_ok = true;
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(5));
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(500));
  in.adc_ok = false; /* a failure restarts the 1 s of good reads */
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(5));
  in.adc_ok = true;
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(5));
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(995));
  TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  TEST_ASSERT_EQUAL_INT(FAULT_NONE, sm.fault);
}

static void test_two_failures_are_not_a_fault(void)
{
  begin(ST_READY);
  in.led = LED_STEADY;
  in.v_out = 2.0f;
  for (int i = 0; i < 10; i++) {
    in.adc_ok = false;
    step(5);
    step(5);
    in.adc_ok = true;
    TEST_ASSERT_EQUAL_INT(ST_READY, step(5));
  }
}

static void test_adc_not_found_is_permanent(void)
{
  gs_init(&sm, 0);
  gs_force_fault(&sm, FAULT_ADC_NOT_FOUND, 0);
  in = (gs_input_t){ .t_ms = 0, .adc_ok = true, .v_out = 2.0f, .led = LED_STEADY, .supply_v = NAN };
  TEST_ASSERT_EQUAL_INT(ST_FAULT, step(5000));
}

static void test_since_ms(void)
{
  begin(ST_WARMING);
  in.v_out = 2.0f;
  in.led = LED_SLOW;
  step(100);
  TEST_ASSERT_EQUAL_UINT(0, sm.since_ms);
  in.led = LED_STEADY;
  step(5);
  TEST_ASSERT_EQUAL_UINT(105, sm.since_ms);
}

static void test_names(void)
{
  for (int s = 0; s < ST_COUNT; s++) {
    TEST_ASSERT_EQUAL_INT(s, gs_state_parse(gs_state_name((gauge_state_t)s)));
  }
  TEST_ASSERT_EQUAL_STRING("LEAN_RAIL", gs_state_name(ST_LEAN_RAIL));
  TEST_ASSERT_EQUAL_INT(ST_COUNT, gs_state_parse("bogus"));
  TEST_ASSERT_TRUE(gs_has_lambda(ST_RICH_RAIL));
  TEST_ASSERT_FALSE(gs_has_lambda(ST_WARMING));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_boot_to_no_spartan);
  RUN_TEST(test_no_spartan_to_sequencer_by_output);
  RUN_TEST(test_no_spartan_to_sequencer_by_supply);
  RUN_TEST(test_sequencer_to_warming);
  RUN_TEST(test_warming_wait_exhaust);
  RUN_TEST(test_warming_to_ready_and_back);
  RUN_TEST(test_rails);
  RUN_TEST(test_too_hot);
  RUN_TEST(test_loss_by_supply);
  RUN_TEST(test_loss_by_led_and_output);
  RUN_TEST(test_loss_needs_both);
  RUN_TEST(test_i2c_fault_and_recovery);
  RUN_TEST(test_two_failures_are_not_a_fault);
  RUN_TEST(test_adc_not_found_is_permanent);
  RUN_TEST(test_since_ms);
  RUN_TEST(test_names);
  return UNITY_END();
}
