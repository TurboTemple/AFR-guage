#include "smoothing.h"

#include <math.h>

void median3_reset(median3_t *m)
{
  m->n = 0;
  m->head = 0;
}

float median3_feed(median3_t *m, float x)
{
  m->v[m->head] = x;
  m->head = (uint8_t)((m->head + 1) % 3);
  if (m->n < 3) m->n++;
  if (m->n < 3) return x;
  float a = m->v[0], b = m->v[1], c = m->v[2];
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return a > b ? a : b;
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
