#include "unity.h"

#include "calibration.h"
#include "conversion.h"
#include "sim.h"

void setUp(void) { sim_srand(1); }
void tearDown(void) {}

static cal_detector_t det;

typedef struct {
  uint32_t lead_ms;  /* before plateau 1, at 0 V */
  uint32_t p1_ms;
  uint32_t gap_ms;   /* between plateaus, at 2.5 V */
  uint32_t p2_ms;
  uint32_t tail_ms;  /* after plateau 2, at 2.35 V */
  float    a, b;     /* true chain calibration */
  float    noise_v;
} seq_t;

static seq_t seq_default(void)
{
  seq_t s = { 300, 5000, 0, 5000, 1000, CAL_A_NOMINAL, 0.0f, 0.0015f };
  return s;
}

static float seq_volts(const seq_t *s, uint32_t t)
{
  if (t < s->lead_ms) return 0.0f;
  t -= s->lead_ms;
  if (t < s->p1_ms) return CAL_PLATEAU1_V;
  t -= s->p1_ms;
  if (t < s->gap_ms) return 2.5f;
  t -= s->gap_ms;
  if (t < s->p2_ms) return CAL_PLATEAU2_V;
  return 2.35f;
}

/* Run the sequence through the detector at 200 Hz. Returns true if both
 * plateaus were captured; *t_done is when. */
static bool run_seq(const seq_t *s, float *c1, float *c2, uint32_t *t_done)
{
  cal_detector_init(&det, 5);
  uint32_t end = s->lead_ms + s->p1_ms + s->gap_ms + s->p2_ms + s->tail_ms;
  for (uint32_t t = 0; t < end; t += 5) {
    int16_t code = sim_code(seq_volts(s, t), s->a, s->b, s->noise_v);
    if (cal_detector_feed(&det, t, CAL_A_NOMINAL * (float)code)) {
      float m1, m2;
      TEST_ASSERT_TRUE(cal_detector_captured(&det, &m1, &m2));
      *c1 = m1 / CAL_A_NOMINAL;
      *c2 = m2 / CAL_A_NOMINAL;
      if (t_done) *t_done = t;
      return true;
    }
  }
  return false;
}

static void test_nominal_chain(void)
{
  seq_t s = seq_default();
  float c1, c2;
  uint32_t t_done;
  TEST_ASSERT_TRUE(run_seq(&s, &c1, &c2, &t_done));
  /* Captured just after plateau 2 ends. */
  TEST_ASSERT_UINT_WITHIN(10, 10305, t_done);
  cal_coeffs_t c;
  TEST_ASSERT_TRUE(cal_fit(c1, c2, &c));
  TEST_ASSERT_FLOAT_WITHIN(CAL_A_NOMINAL * 0.001, CAL_A_NOMINAL, c.a);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.0, c.b);
}

static void test_recovers_gain_and_offset(void)
{
  seq_t s = seq_default();
  s.a = CAL_A_NOMINAL * 1.01f; /* +1 % chain gain error */
  s.b = 0.020f;                /* 20 mV ground offset */
  float c1, c2;
  TEST_ASSERT_TRUE(run_seq(&s, &c1, &c2, NULL));
  cal_coeffs_t c;
  TEST_ASSERT_TRUE(cal_fit(c1, c2, &c));
  TEST_ASSERT_FLOAT_WITHIN(s.a * 0.001, s.a, c.a);
  TEST_ASSERT_FLOAT_WITHIN(0.001, s.b, c.b);
  /* The fit maps codes back to true volts across the range. */
  for (float v = 0.5f; v <= 4.5f; v += 0.5f) {
    int16_t code = sim_code(v, s.a, s.b, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0005, v, conv_code_to_volts(c.a, c.b, code));
  }
}

static void test_gain_acceptance_limits(void)
{
  /* Plateau detection (+/-60 mV) already limits the gain error it can see to
   * under 2 %, so test the +/-3 % acceptance on cal_fit directly. */
  cal_coeffs_t c;
  float a = CAL_A_NOMINAL * 1.025f;
  TEST_ASSERT_TRUE(cal_fit(CAL_PLATEAU1_V / a, CAL_PLATEAU2_V / a, &c));
  a = CAL_A_NOMINAL * 0.975f;
  TEST_ASSERT_TRUE(cal_fit(CAL_PLATEAU1_V / a, CAL_PLATEAU2_V / a, &c));
  a = CAL_A_NOMINAL * 1.035f;
  TEST_ASSERT_FALSE(cal_fit(CAL_PLATEAU1_V / a, CAL_PLATEAU2_V / a, &c));
  TEST_ASSERT_FLOAT_WITHIN(a * 1e-4, a, c.a); /* the fit is still reported */
  a = CAL_A_NOMINAL * 0.965f;
  TEST_ASSERT_FALSE(cal_fit(CAL_PLATEAU1_V / a, CAL_PLATEAU2_V / a, &c));
}

static void test_rejects_large_offset(void)
{
  seq_t s = seq_default();
  s.b = 0.055f; /* inside the 60 mV detection window, outside |b| < 50 mV */
  float c1, c2;
  TEST_ASSERT_TRUE(run_seq(&s, &c1, &c2, NULL));
  cal_coeffs_t c;
  TEST_ASSERT_FALSE(cal_fit(c1, c2, &c));
}

static void test_rejects_inverted_plateaus(void)
{
  cal_coeffs_t c;
  TEST_ASSERT_FALSE(cal_fit(16000.0f, 8000.0f, &c));
}

static void test_short_plateau_ignored(void)
{
  seq_t s = seq_default();
  s.p1_ms = 1500;
  float c1, c2;
  TEST_ASSERT_FALSE(run_seq(&s, &c1, &c2, NULL));
}

static void test_plateau2_too_late(void)
{
  seq_t s = seq_default();
  s.gap_ms = 2500;
  float c1, c2;
  TEST_ASSERT_FALSE(run_seq(&s, &c1, &c2, NULL));
}

static void test_plateau2_after_short_gap(void)
{
  seq_t s = seq_default();
  s.gap_ms = 1000;
  float c1, c2;
  TEST_ASSERT_TRUE(run_seq(&s, &c1, &c2, NULL));
}

static void test_noisy_plateau_ignored(void)
{
  seq_t s = seq_default();
  s.noise_v = 0.008f; /* sd about 4.6 mV */
  float c1, c2;
  TEST_ASSERT_FALSE(run_seq(&s, &c1, &c2, NULL));
}

static void test_shorter_plateaus_detected(void)
{
  /* Timings vary between generations: 3 s plateaus still work. */
  seq_t s = seq_default();
  s.p1_ms = 3000;
  s.p2_ms = 3000;
  float c1, c2;
  TEST_ASSERT_TRUE(run_seq(&s, &c1, &c2, NULL));
}

static void test_finish_closes_running_plateau(void)
{
  seq_t s = seq_default();
  cal_detector_init(&det, 5);
  /* Stop feeding 3 s into plateau 2. */
  uint32_t end = s.lead_ms + s.p1_ms + 3000;
  for (uint32_t t = 0; t < end; t += 5) {
    int16_t code = sim_code(seq_volts(&s, t), s.a, s.b, s.noise_v);
    TEST_ASSERT_FALSE(cal_detector_feed(&det, t, CAL_A_NOMINAL * (float)code));
  }
  TEST_ASSERT_TRUE(cal_detector_finish(&det));
  TEST_ASSERT_TRUE(cal_detector_captured(&det, NULL, NULL));
}

static void test_low_rate_window(void)
{
  /* Replay feeds logged volts at 20 Hz. */
  seq_t s = seq_default();
  s.noise_v = 0.0005f;
  cal_detector_init(&det, 50);
  bool done = false;
  for (uint32_t t = 0; t < 12000 && !done; t += 50) {
    done = cal_detector_feed(&det, t, seq_volts(&s, t) + s.noise_v * sim_noise());
  }
  TEST_ASSERT_TRUE(done);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_nominal_chain);
  RUN_TEST(test_recovers_gain_and_offset);
  RUN_TEST(test_gain_acceptance_limits);
  RUN_TEST(test_rejects_large_offset);
  RUN_TEST(test_rejects_inverted_plateaus);
  RUN_TEST(test_short_plateau_ignored);
  RUN_TEST(test_plateau2_too_late);
  RUN_TEST(test_plateau2_after_short_gap);
  RUN_TEST(test_noisy_plateau_ignored);
  RUN_TEST(test_shorter_plateaus_detected);
  RUN_TEST(test_finish_closes_running_plateau);
  RUN_TEST(test_low_rate_window);
  return UNITY_END();
}
