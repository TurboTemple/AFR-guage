#include "afr_settings.h"

#include <math.h>

#include "calibration.h"

void settings_defaults(afr_settings_t *s)
{
  s->fuel = FUEL_PETROL;
  s->fuel_custom = 14.7f;
  s->primary_unit = UNIT_LAMBDA;
  s->show_afr = false;
  s->warn_lean = 1.10f;
  s->warn_lean_ms = 1000;
  s->warn_rich = 0.75f;
  s->warn_rich_ms = 2000;
  s->smooth_ms = 100;
  s->log_rate_hz = 20;
  s->bright_day = 80;
  s->bright_night = 25;
  s->night = false;
  s->cal_valid = false;
  s->cal_a = NAN;
  s->cal_b = NAN;
  s->cal_time = 0;
}

static bool clampf(float *v, float lo, float hi, float dflt)
{
  float old = *v;
  if (isnan(*v)) *v = dflt;
  else if (*v < lo) *v = lo;
  else if (*v > hi) *v = hi;
  return *v != old || isnan(old);
}

static bool clampu(uint32_t v, uint32_t lo, uint32_t hi, uint32_t *out)
{
  uint32_t r = v < lo ? lo : v > hi ? hi : v;
  *out = r;
  return r != v;
}

#define CLAMP_U(field, lo, hi) do { uint32_t t_; \
    if (clampu((uint32_t)s->field, lo, hi, &t_)) { s->field = (__typeof__(s->field))t_; ch = true; } \
  } while (0)

bool settings_validate(afr_settings_t *s)
{
  bool ch = false;
  if (s->fuel >= FUEL_COUNT) { s->fuel = FUEL_PETROL; ch = true; }
  ch |= clampf(&s->fuel_custom, 5.0f, 20.0f, 14.7f);
  if (s->primary_unit > UNIT_AFR) { s->primary_unit = UNIT_LAMBDA; ch = true; }
  ch |= clampf(&s->warn_lean, 1.00f, 1.30f, 1.10f);
  CLAMP_U(warn_lean_ms, 200, 5000);
  ch |= clampf(&s->warn_rich, 0.68f, 0.95f, 0.75f);
  CLAMP_U(warn_rich_ms, 200, 5000);
  CLAMP_U(smooth_ms, 0, 500);
  if (s->log_rate_hz != 10 && s->log_rate_hz != 20 && s->log_rate_hz != 50) {
    s->log_rate_hz = s->log_rate_hz < 15 ? 10 : s->log_rate_hz < 35 ? 20 : 50;
    ch = true;
  }
  CLAMP_U(bright_day, 5, 100);
  CLAMP_U(bright_night, 5, 100);
  if (s->cal_valid) {
    cal_coeffs_t c = { s->cal_a, s->cal_b };
    if (!cal_plausible(&c)) {
      s->cal_valid = false;
      ch = true;
    }
  }
  return ch;
}

float settings_stoich(const afr_settings_t *s)
{
  return conv_stoich((fuel_t)s->fuel, s->fuel_custom);
}
