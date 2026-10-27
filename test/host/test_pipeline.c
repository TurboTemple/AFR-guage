/* End to end through afr_pipeline, sampled the way the firmware samples:
 * the lambda ADC at 860 SPS through the decimator, the LED at 50 Hz from
 * the second ADC. */
#include "unity.h"

#include <math.h>

#include "pipeline.h"
#include "sim.h"

void setUp(void) { sim_srand(99); }
void tearDown(void) {}

#define RAW_US 1163u /* 860 SPS */

static afr_pipeline_t p;
static decim_t dec;
static reading_t r;
static uint64_t t_us;
static uint64_t next_led_us;

/* The simulated car. */
static float chain_a, chain_b;  /* true divider/ADC calibration */
static float spartan_v;         /* linear output */
static uint32_t led_period, led_on;
static bool adc_fail;
static unsigned shown_outside_ready, steps;

static afr_config_t config(void)
{
  afr_config_t c = {
    .range = conv_range_default(),
    .stoich = 14.7f,
    .smooth_ms = 50,
    .alarm = alarms_default_cfg(),
  };
  return c;
}

static void start(const cal_coeffs_t *stored)
{
  afr_config_t c = config();
  afr_pipeline_init(&p, &c, stored, false, AFR_SAMPLE_MS, 0);
  afr_pipeline_start(&p, 0);
  decim_init(&dec, SPIKE_CODES);
  t_us = next_led_us = 0;
  chain_a = CAL_A_NOMINAL;
  chain_b = 0.0f;
  spartan_v = 0.0f;
  led_period = 0;
  led_on = 0;
  adc_fail = false;
  shown_outside_ready = steps = 0;
}

static void run(uint32_t dur_ms)
{
  uint64_t end = t_us + (uint64_t)dur_ms * 1000u;
  for (; t_us < end; t_us += RAW_US) {
    uint32_t t_ms = (uint32_t)(t_us / 1000u);
    if (t_us >= next_led_us) {
      next_led_us += 20000;
      afr_pipeline_led_sample(&p, t_ms, sim_adc_code(sim_blink(t_ms, led_period, led_on) ? 3.0f : 0.0f));
    }
    float mean;
    uint64_t t_mid;
    if (decim_feed(&dec, sim_code(spartan_v, chain_a, chain_b, 0.0015f), t_us, &mean, &t_mid)) {
      afr_pipeline_step_raw(&p, (uint32_t)(t_mid / 1000u), !adc_fail, mean, &r);
      steps++;
      if (!gs_has_lambda(r.state) && !isnan(r.lambda)) shown_outside_ready++;
    }
  }
}

static void sequencer(void)
{
  spartan_v = CAL_PLATEAU1_V;
  led_period = 2000;
  led_on = 250;
  run(5000);
  spartan_v = CAL_PLATEAU2_V;
  run(5000);
}

static void get_ready(void)
{
  start(NULL);
  sequencer();
  led_period = 0;
  led_on = 1;
  spartan_v = sim_v_for_lambda(1.0f);
  run(2000);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  afr_pipeline_take_events(&p);
}

static void test_rate_is_215hz(void)
{
  start(NULL);
  run(10000);
  TEST_ASSERT_UINT_WITHIN(3, 2150, steps);
}

static void test_cold_start_to_ready(void)
{
  start(NULL);
  run(1000);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_NOMINAL);

  chain_a = CAL_A_NOMINAL * 1.008f;
  chain_b = 0.012f;
  sequencer();
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, r.state);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_SPARTAN_UP);

  spartan_v = 1.2f;
  run(20);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_FRESH);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_CAL_ACCEPTED);
  TEST_ASSERT_FLOAT_WITHIN(chain_a * 0.0005, chain_a, p.cal.a);
  TEST_ASSERT_FLOAT_WITHIN(0.001, chain_b, p.cal.b);
  run(20000);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  TEST_ASSERT_FLOAT_IS_NAN(r.lambda);

  led_period = 0;
  led_on = 1;
  spartan_v = sim_v_for_lambda(1.00f);
  run(1400);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  run(200);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  run(500);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 1.00, r.lambda);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 1.00, r.lambda_disp);
  TEST_ASSERT_EQUAL_UINT(0, shown_outside_ready);
}

/* Every sample, not just the display: the logged lambda within 0.002 of
 * the truth from 0.5 V to 4.5 V with realistic chain errors and noise. */
static void test_accuracy_across_range(void)
{
  start(NULL);
  chain_a = CAL_A_NOMINAL * 0.992f;
  chain_b = -0.015f;
  sequencer();
  led_period = 0;
  led_on = 1;
  spartan_v = 2.5f;
  run(2000);
  for (float v = 0.5f; v <= 4.501f; v += 0.25f) {
    spartan_v = v;
    run(200);
    TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
    float truth = LAMBDA_0V_DEFAULT + 0.136f * v;
    for (int i = 0; i < 40; i++) {
      run(5);
      TEST_ASSERT_FLOAT_WITHIN(0.002, truth, r.lambda);
    }
  }
}

static void test_step_latency(void)
{
  /* A 0.85 -> 1.00 step: the sample seen 10 ms later has settled. */
  get_ready();
  spartan_v = sim_v_for_lambda(0.85f);
  run(100);
  spartan_v = sim_v_for_lambda(1.00f);
  run(10);
  TEST_ASSERT_FLOAT_WITHIN(0.003, 1.00, r.lambda);
}

static void test_uncalibrated_error_is_removed(void)
{
  start(NULL);
  chain_a = CAL_A_NOMINAL * 1.015f;
  spartan_v = 2.0f; /* booted after the Spartan: no plateaus */
  led_period = 0;
  led_on = 1;
  run(17000);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_NOMINAL);
  float truth = LAMBDA_0V_DEFAULT + 0.136f * 2.0f;
  TEST_ASSERT_TRUE(fabsf(r.lambda - truth) > 0.003f);

  start(NULL);
  chain_a = CAL_A_NOMINAL * 1.015f;
  sequencer();
  led_period = 0;
  led_on = 1;
  spartan_v = 2.0f;
  run(3000);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  TEST_ASSERT_FLOAT_WITHIN(0.001, truth, r.lambda);
}

static void test_stored_calibration_used_when_no_plateaus(void)
{
  cal_coeffs_t stored = { CAL_A_NOMINAL * 1.01f, 0.01f };
  start(&stored);
  TEST_ASSERT_EQUAL_INT(CAL_SRC_STORED, p.cal_src);
  chain_a = stored.a;
  chain_b = stored.b;
  spartan_v = 2.0f;
  led_period = 0;
  led_on = 1;
  run(17000);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_STORED);
  TEST_ASSERT_FLOAT_WITHIN(0.0005, LAMBDA_0V_DEFAULT + 0.136f * 2.0f, r.lambda);
}

static void test_rejected_fit_keeps_last_good(void)
{
  cal_coeffs_t stored = { CAL_A_NOMINAL, 0.0f };
  start(&stored);
  chain_b = 0.056f; /* detected, but |b| over 50 mV */
  sequencer();
  spartan_v = 1.2f;
  run(20);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_REJECTED);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_STORED);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_CAL_REJECTED);
  TEST_ASSERT_FLOAT_WITHIN(1e-9, CAL_A_NOMINAL, p.cal.a);
}

/* A pass: launch rich and steady; then a lean-out; then the lift and fuel
 * cut, which warns too (there is no throttle signal). */
static void test_pass_lean_out_warns(void)
{
  get_ready();
  spartan_v = sim_v_for_lambda(0.82f);
  run(3000);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, r.alarm);

  spartan_v = sim_v_for_lambda(1.10f);
  run(295);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, r.alarm);
  run(15);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, r.alarm);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_ALARM);

  spartan_v = sim_v_for_lambda(0.84f);
  run(10);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, r.alarm);

  spartan_v = 5.0f;
  run(5000);
  TEST_ASSERT_EQUAL_INT(ST_LEAN_RAIL, r.state);
  TEST_ASSERT_EQUAL_INT(ALARM_LEAN, r.alarm);
  TEST_ASSERT_FLOAT_WITHIN(0.003, 1.10, r.peak_lean);
  TEST_ASSERT_FLOAT_WITHIN(0.003, 0.82, r.peak_rich);
  TEST_ASSERT_TRUE(r.flags & FLAG_LEAN_RAIL_SEEN);
}

static void test_clean_session_no_warning(void)
{
  get_ready();
  for (int pass = 0; pass < 5; pass++) {
    spartan_v = sim_v_for_lambda(0.80f + 0.01f * (float)pass);
    run(9000);
    spartan_v = sim_v_for_lambda(1.0f);
    run(20000);
  }
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, r.alarm);
  TEST_ASSERT_FALSE(afr_pipeline_take_events(&p) & AFR_EVT_ALARM);
}

/* Race mode: from the Spartan's output stepping lean to the warning,
 * through the 860 SPS decimator and the hold. */
static void test_race_warning_latency(void)
{
  get_ready();
  afr_config_t c = config();
  c.race = true;
  c.alarm.lean = 0.92f;
  c.alarm.lean_ms = 15;
  afr_pipeline_set_config(&p, &c);
  spartan_v = sim_v_for_lambda(0.86f);
  run(2000);
  TEST_ASSERT_EQUAL_INT(ALARM_NONE, r.alarm);
  TEST_ASSERT_TRUE(r.flags & FLAG_RACE);
  spartan_v = sim_v_for_lambda(0.95f);
  uint32_t ms = 0;
  while (r.alarm != ALARM_LEAN && ms < 200) {
    run(1);
    ms++;
  }
  /* About 2 blocks to see it (4.65 ms each) plus the 15 ms hold. */
  TEST_ASSERT_UINT_WITHIN(6, 22, ms);
}

static void test_spartan_power_loss(void)
{
  get_ready();
  spartan_v = 0.0f;
  led_on = 0;
  run(9900);
  TEST_ASSERT_TRUE(r.state != ST_NO_SPARTAN);
  run(500);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, r.state);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_SPARTAN_DOWN);
  TEST_ASSERT_EQUAL_UINT(0, shown_outside_ready);
}

static void test_i2c_fault(void)
{
  get_ready();
  adc_fail = true;
  run(20);
  TEST_ASSERT_EQUAL_INT(ST_FAULT, r.state);
  TEST_ASSERT_EQUAL_INT(FAULT_I2C, r.fault);
  TEST_ASSERT_FLOAT_IS_NAN(r.lambda);
  adc_fail = false;
  run(1100);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
}

static void test_volts_mode_replays_states(void)
{
  afr_config_t c = config();
  afr_pipeline_init(&p, &c, NULL, false, 5, 0);
  afr_pipeline_start(&p, 0);
  uint32_t t = 0;
  for (; t < 1000; t += 5) afr_pipeline_step_volts(&p, t, 0.0f, LED_OFF, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, r.state);
  for (; t < 6000; t += 5) afr_pipeline_step_volts(&p, t, 1.666f, LED_SLOW, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, r.state);
  for (; t < 11000; t += 5) afr_pipeline_step_volts(&p, t, 3.333f, LED_SLOW, NAN, &r);
  afr_pipeline_step_volts(&p, t, 1.0f, LED_SLOW, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  t += 5;
  afr_pipeline_step_volts(&p, t, 2.0f, LED_STEADY, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.68 + 0.136 * 2.0, r.lambda);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_rate_is_215hz);
  RUN_TEST(test_cold_start_to_ready);
  RUN_TEST(test_accuracy_across_range);
  RUN_TEST(test_step_latency);
  RUN_TEST(test_uncalibrated_error_is_removed);
  RUN_TEST(test_stored_calibration_used_when_no_plateaus);
  RUN_TEST(test_rejected_fit_keeps_last_good);
  RUN_TEST(test_pass_lean_out_warns);
  RUN_TEST(test_clean_session_no_warning);
  RUN_TEST(test_race_warning_latency);
  RUN_TEST(test_spartan_power_loss);
  RUN_TEST(test_i2c_fault);
  RUN_TEST(test_volts_mode_replays_states);
  return UNITY_END();
}
