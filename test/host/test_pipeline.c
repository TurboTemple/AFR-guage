/* End to end through afr_pipeline: a simulated Spartan power-up, sequencer,
 * warm-up and drive, sampled the way the firmware samples it. */
#include "unity.h"

#include <math.h>

#include "pipeline.h"
#include "sim.h"

void setUp(void) { sim_srand(99); }
void tearDown(void) {}

static afr_pipeline_t p;
static reading_t r;
static uint32_t t_ms;
static unsigned tick;

/* The simulated car. */
static float chain_a, chain_b;  /* true divider/ADC calibration */
static float spartan_v;         /* linear output */
static uint32_t led_period, led_on;
static float led_lit_v = 3.0f;
static bool adc_fail;
static unsigned shown_outside_ready;

static afr_config_t config(void)
{
  afr_config_t c = {
    .range = conv_range_default(),
    .stoich = 14.7f,
    .smooth_ms = 100,
    .lean = { 1.10f, 1000 },
    .rich = { 0.75f, 2000 },
  };
  return c;
}

static void start(const cal_coeffs_t *stored)
{
  afr_config_t c = config();
  afr_pipeline_init(&p, &c, stored, false, AFR_SAMPLE_MS, 0);
  afr_pipeline_start(&p, 0);
  t_ms = 0;
  tick = 0;
  chain_a = CAL_A_NOMINAL;
  chain_b = 0.0f;
  spartan_v = 0.0f;
  led_period = 0;
  led_on = 0;
  adc_fail = false;
  shown_outside_ready = 0;
}

/* Run dur ms of 5 ms sampler ticks: AIN0 every tick, AIN1 every 4th. */
static void run(uint32_t dur)
{
  for (uint32_t end = t_ms + dur; t_ms < end; t_ms += AFR_SAMPLE_MS, tick++) {
    afr_raw_t in = { .t_ms = t_ms };
    in.lam_ok = !adc_fail;
    in.lam_code = sim_code(spartan_v, chain_a, chain_b, 0.0005f);
    if (tick % 4 == 0 && !adc_fail) {
      in.led_new = true;
      in.led_code = sim_adc_code(sim_blink(t_ms, led_period, led_on) ? led_lit_v : 0.0f);
    }
    afr_pipeline_step_raw(&p, &in, &r);
    /* No number ever outside READY (rails carry their end point). */
    if (!gs_has_lambda(r.state) && !isnan(r.lambda)) shown_outside_ready++;
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

static void test_cold_start_to_ready(void)
{
  start(NULL);
  run(1000);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_NOMINAL);

  chain_a = CAL_A_NOMINAL * 1.008f; /* realistic chain errors */
  chain_b = 0.012f;
  sequencer();
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, r.state);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_SPARTAN_UP);

  /* Warm-up: output undocumented, LED slow. */
  spartan_v = 1.2f;
  run(20);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  TEST_ASSERT_TRUE(r.flags & FLAG_CAL_FRESH);
  TEST_ASSERT_TRUE(afr_pipeline_take_events(&p) & AFR_EVT_CAL_ACCEPTED);
  TEST_ASSERT_FLOAT_WITHIN(chain_a * 0.001, chain_a, p.cal.a);
  TEST_ASSERT_FLOAT_WITHIN(0.002, chain_b, p.cal.b);
  run(20000);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  TEST_ASSERT_FLOAT_IS_NAN(r.lambda);

  /* Operating temperature. */
  led_period = 0;
  led_on = 1;
  spartan_v = sim_v_for_lambda(1.00f);
  run(1400);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  run(200);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  run(500);
  TEST_ASSERT_FLOAT_WITHIN(0.005, 1.00, r.lambda);
  TEST_ASSERT_FLOAT_WITHIN(0.005, 1.00, r.lambda_disp);
  TEST_ASSERT_FLOAT_WITHIN(0.08, 14.7, r.afr);
  TEST_ASSERT_EQUAL_UINT(0, shown_outside_ready);
}

/* Acceptance: displayed lambda within 0.005 from 0.5 V to 4.5 V. */
static void test_accuracy_across_range(void)
{
  start(NULL);
  chain_a = CAL_A_NOMINAL * 0.992f;
  chain_b = -0.015f;
  sequencer();
  led_period = 0;
  led_on = 1;
  for (float v = 0.5f; v <= 4.501f; v += 0.25f) {
    spartan_v = v;
    run(2000);
    TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
    float truth = LAMBDA_0V_DEFAULT + 0.136f * v;
    TEST_ASSERT_FLOAT_WITHIN(0.005, truth, r.lambda_disp);
  }
}

static void test_uncalibrated_error_is_removed(void)
{
  /* Without the sequencer, a 1.5 % chain error would show. */
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

static void test_overrun_never_lean_warns(void)
{
  get_ready();
  spartan_v = 5.0f; /* fuel cut */
  run(8000);
  TEST_ASSERT_EQUAL_INT(ST_LEAN_RAIL, r.state);
  TEST_ASSERT_EQUAL_INT(WARN_NONE, r.warn);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.36, r.lambda);
  TEST_ASSERT_TRUE(r.flags & FLAG_LEAN_RAIL_SEEN);
  spartan_v = sim_v_for_lambda(1.0f);
  run(100);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  /* Rail time is not a lean peak. */
  TEST_ASSERT_TRUE(r.peak_lean < 1.01f);
}

static void test_lean_and_rich_warnings(void)
{
  get_ready();
  spartan_v = sim_v_for_lambda(1.15f);
  run(900);
  TEST_ASSERT_EQUAL_INT(WARN_NONE, r.warn);
  run(300);
  TEST_ASSERT_EQUAL_INT(WARN_LEAN, r.warn);
  spartan_v = sim_v_for_lambda(1.0f);
  run(100);
  TEST_ASSERT_EQUAL_INT(WARN_NONE, r.warn);
  spartan_v = 0.0f; /* rich rail */
  run(2200);
  TEST_ASSERT_EQUAL_INT(ST_RICH_RAIL, r.state);
  TEST_ASSERT_EQUAL_INT(WARN_RICH, r.warn);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 1.15, r.peak_lean);
}

static void test_peak_reset(void)
{
  get_ready();
  spartan_v = sim_v_for_lambda(0.85f);
  run(500);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 0.85, r.peak_rich);
  afr_pipeline_reset_peaks(&p);
  run(5);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 0.85, r.peak_rich);
  spartan_v = sim_v_for_lambda(0.95f);
  run(500);
  /* The median filter needs a sample to follow the step. */
  TEST_ASSERT_FLOAT_WITHIN(0.002, 0.85, r.peak_rich);
  afr_pipeline_reset_peaks(&p);
  run(5);
  TEST_ASSERT_FLOAT_WITHIN(0.002, 0.95, r.peak_rich);
}

static void test_spartan_power_loss(void)
{
  get_ready();
  spartan_v = 0.0f;
  led_on = 0; /* LED off */
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
  run(15);
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
  afr_pipeline_init(&p, &c, NULL, false, 50, 0);
  afr_pipeline_start(&p, 0);
  uint32_t t = 0;
  for (; t < 1000; t += 50) afr_pipeline_step_volts(&p, t, 0.0f, LED_OFF, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_NO_SPARTAN, r.state);
  for (; t < 6000; t += 50) afr_pipeline_step_volts(&p, t, 1.666f, LED_SLOW, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_SEQUENCER, r.state);
  for (; t < 11000; t += 50) afr_pipeline_step_volts(&p, t, 3.333f, LED_SLOW, NAN, &r);
  afr_pipeline_step_volts(&p, t, 1.0f, LED_SLOW, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_WARMING, r.state);
  t += 50;
  afr_pipeline_step_volts(&p, t, 2.0f, LED_STEADY, NAN, &r);
  TEST_ASSERT_EQUAL_INT(ST_READY, r.state);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.68 + 0.136 * 2.0, r.lambda);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_cold_start_to_ready);
  RUN_TEST(test_accuracy_across_range);
  RUN_TEST(test_uncalibrated_error_is_removed);
  RUN_TEST(test_stored_calibration_used_when_no_plateaus);
  RUN_TEST(test_rejected_fit_keeps_last_good);
  RUN_TEST(test_overrun_never_lean_warns);
  RUN_TEST(test_lean_and_rich_warnings);
  RUN_TEST(test_peak_reset);
  RUN_TEST(test_spartan_power_loss);
  RUN_TEST(test_i2c_fault);
  RUN_TEST(test_volts_mode_replays_states);
  return UNITY_END();
}
