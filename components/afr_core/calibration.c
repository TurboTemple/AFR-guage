#include "calibration.h"

#include <math.h>
#include <string.h>

#include "conversion.h"

cal_coeffs_t cal_nominal(void)
{
  cal_coeffs_t c = { CAL_A_NOMINAL, CAL_B_NOMINAL };
  return c;
}

void cal_detector_init(cal_detector_t *d, uint16_t sample_period_ms)
{
  memset(d, 0, sizeof(*d));
  uint32_t n = sample_period_ms ? CAL_WINDOW_MS / sample_period_ms : CAL_WINDOW_MAX;
  if (n < 3) n = 3;
  if (n > CAL_WINDOW_MAX) n = CAL_WINDOW_MAX;
  d->win_n = (uint16_t)n;
  cal_detector_reset(d);
}

void cal_detector_reset(cal_detector_t *d)
{
  d->win_head = 0;
  d->win_count = 0;
  d->phase = CAL_SEARCH_1;
  d->run_n = 0;
  d->m1 = NAN;
  d->m2 = NAN;
}

/* Index of the i-th oldest window sample. */
static uint16_t win_index(const cal_detector_t *d, uint16_t i)
{
  return (uint16_t)((d->win_head + d->win_n - d->win_count + i) % d->win_n);
}

static void window_stats(const cal_detector_t *d, float *mean, float *sd)
{
  /* Deviations from the oldest sample keep float sums precise. */
  float ref = d->win[win_index(d, 0)];
  float sum = 0.0f;
  for (uint16_t i = 0; i < d->win_count; i++) sum += d->win[win_index(d, i)] - ref;
  float m = sum / (float)d->win_count;
  float ss = 0.0f;
  for (uint16_t i = 0; i < d->win_count; i++) {
    float e = d->win[win_index(d, i)] - ref - m;
    ss += e * e;
  }
  *mean = ref + m;
  *sd = sqrtf(ss / (float)d->win_count);
}

static void run_start(cal_detector_t *d)
{
  d->run_n = 0;
  d->run_start_ms = d->win_t[win_index(d, 0)];
  for (uint16_t i = 0; i < d->win_count && d->run_n < CAL_RUN_MAX; i++) {
    d->run[d->run_n++] = d->win[win_index(d, i)];
  }
  d->run_last_ms = d->win_t[win_index(d, (uint16_t)(d->win_count - 1))];
}

static void run_append(cal_detector_t *d, uint32_t t_ms, float v)
{
  if (d->run_n < CAL_RUN_MAX) d->run[d->run_n++] = v;
  d->run_last_ms = t_ms;
}

static bool run_long_enough(const cal_detector_t *d)
{
  return d->run_last_ms - d->run_start_ms >= CAL_MIN_RUN_MS;
}

/* Mean of the middle 60 % of the run. */
static float run_trimmed_mean(const cal_detector_t *d)
{
  uint16_t cut = (uint16_t)((float)d->run_n * CAL_TRIM);
  uint16_t lo = cut, hi = (uint16_t)(d->run_n - cut);
  if (hi <= lo) { lo = 0; hi = d->run_n; }
  float ref = d->run[lo];
  float sum = 0.0f;
  for (uint16_t i = lo; i < hi; i++) sum += d->run[i] - ref;
  return ref + sum / (float)(hi - lo);
}

bool cal_detector_feed(cal_detector_t *d, uint32_t t_ms, float v)
{
  if (d->phase == CAL_CAPTURED) return false;

  d->win[d->win_head] = v;
  d->win_t[d->win_head] = t_ms;
  d->win_head = (uint16_t)((d->win_head + 1) % d->win_n);
  if (d->win_count < d->win_n) d->win_count++;

  bool full = d->win_count == d->win_n;
  float mean = 0.0f, sd = INFINITY;
  if (full) window_stats(d, &mean, &sd);
  bool second = d->phase == CAL_SEARCH_2 || d->phase == CAL_IN_2;
  float target = second ? CAL_PLATEAU2_V : CAL_PLATEAU1_V;
  bool flat = full && sd < CAL_STD_MAX_V && fabsf(mean - target) < CAL_MEAN_TOL_V;

  switch (d->phase) {
  case CAL_SEARCH_1:
    if (flat) {
      run_start(d);
      d->phase = CAL_IN_1;
    }
    break;

  case CAL_IN_1:
    if (flat) {
      run_append(d, t_ms, v);
    } else if (run_long_enough(d)) {
      d->m1 = run_trimmed_mean(d);
      d->p1_end_ms = d->run_last_ms;
      d->phase = CAL_SEARCH_2;
    } else {
      d->phase = CAL_SEARCH_1;
    }
    break;

  case CAL_SEARCH_2:
    if (flat) {
      if (d->win_t[win_index(d, 0)] - d->p1_end_ms <= CAL_MAX_GAP_MS) {
        run_start(d);
        d->phase = CAL_IN_2;
      } else {
        d->phase = CAL_SEARCH_1;
      }
    } else if (t_ms - d->p1_end_ms > CAL_MAX_GAP_MS + CAL_WINDOW_MS) {
      /* No second plateau in time: plateau 1 was not the sequencer. */
      d->phase = CAL_SEARCH_1;
    }
    break;

  case CAL_IN_2:
    if (flat) {
      run_append(d, t_ms, v);
    } else if (run_long_enough(d)) {
      d->m2 = run_trimmed_mean(d);
      d->phase = CAL_CAPTURED;
      return true;
    } else {
      d->phase = CAL_SEARCH_1;
    }
    break;

  case CAL_CAPTURED:
    break;
  }
  return false;
}

bool cal_detector_finish(cal_detector_t *d)
{
  if (d->phase == CAL_IN_2 && run_long_enough(d)) {
    d->m2 = run_trimmed_mean(d);
    d->phase = CAL_CAPTURED;
    return true;
  }
  return false;
}

bool cal_detector_captured(const cal_detector_t *d, float *m1, float *m2)
{
  if (d->phase != CAL_CAPTURED) return false;
  if (m1) *m1 = d->m1;
  if (m2) *m2 = d->m2;
  return true;
}

bool cal_plausible(const cal_coeffs_t *c)
{
  return isfinite(c->a) && isfinite(c->b) &&
         fabsf(c->a / CAL_A_NOMINAL - 1.0f) <= CAL_A_TOL &&
         fabsf(c->b) < CAL_B_MAX_V;
}

bool cal_fit(float c1, float c2, cal_coeffs_t *out)
{
  if (!(c2 > c1)) {
    out->a = NAN;
    out->b = NAN;
    return false;
  }
  out->a = (CAL_PLATEAU2_V - CAL_PLATEAU1_V) / (c2 - c1);
  out->b = CAL_PLATEAU1_V - out->a * c1;
  return cal_plausible(out);
}
