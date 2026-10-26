#include "smoothing.h"

#include <math.h>

void decim_init(decim_t *d, int32_t spike_codes)
{
  d->n = 0;
  d->spike_codes = spike_codes;
  d->spikes = 0;
}

void decim_reset(decim_t *d)
{
  d->n = 0;
}

bool decim_feed(decim_t *d, int16_t code, uint64_t t_us, float *mean, uint64_t *t_mid_us)
{
  d->codes[d->n] = code;
  d->t_us[d->n] = t_us;
  if (++d->n < DECIM_N) return false;
  d->n = 0;

  int32_t sum = 0, lo = d->codes[0], hi = d->codes[0];
  for (int i = 0; i < DECIM_N; i++) {
    int32_t c = d->codes[i];
    sum += c;
    if (c < lo) lo = c;
    if (c > hi) hi = c;
  }
  if (hi - lo > d->spike_codes) {
    d->spikes++;
    *mean = (float)(sum - lo - hi) / (float)(DECIM_N - 2);
  } else {
    *mean = (float)sum / (float)DECIM_N;
  }
  /* Midpoint of the first and last conversion times. */
  *t_mid_us = d->t_us[0] + (d->t_us[DECIM_N - 1] - d->t_us[0]) / 2u;
  return true;
}

void smoother_reset(smoother_t *s)
{
  s->primed = false;
  s->y = NAN;
}

float smoother_feed(smoother_t *s, float x, uint32_t t_ms, uint16_t tau_ms)
{
  if (isnan(x)) {
    smoother_reset(s);
    return NAN;
  }
  if (!s->primed || tau_ms == 0) {
    s->y = x;
  } else {
    float dt = (float)(t_ms - s->last_ms);
    s->y += (x - s->y) * (1.0f - expf(-dt / (float)tau_ms));
  }
  s->primed = true;
  s->last_ms = t_ms;
  return s->y;
}
