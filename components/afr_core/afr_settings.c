#include "afr_settings.h"

#include <math.h>

#include "calibration.h"
#include "spartan_proto.h"

void settings_defaults(afr_settings_t *s)
{
  alarm_cfg_t a = alarms_default_cfg();
  s->fuel = FUEL_PETROL;
  s->fuel_custom = 14.7f;
  s->primary_unit = UNIT_LAMBDA;
  s->show_afr = false;
  s->lean = a.lean;
  s->lean_ms = (uint16_t)a.lean_ms;
  s->rich = a.rich;
  s->rich_ms = (uint16_t)a.rich_ms;
  s->smooth_ms = 50;
  s->spartan_perf = SP_PERF_10MS;
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
  return *v != old;
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
  ch |= clampf(&s->lean, 0.85f, 1.30f, 1.05f);
  CLAMP_U(lean_ms, 100, 3000);
  ch |= clampf(&s->rich, 0.68f, 0.90f, 0.75f);
  CLAMP_U(rich_ms, 100, 3000);
  if (s->rich + 0.05f > s->lean) {
    /* Keep a gap so the two can never be on together. */
    s->rich = s->lean - 0.05f;
    ch = true;
  }
  CLAMP_U(smooth_ms, 0, 500);
  CLAMP_U(spartan_perf, SP_PERF_20MS, SP_PERF_LEAN);
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

alarm_cfg_t settings_alarm_cfg(const afr_settings_t *s)
{
  alarm_cfg_t a = {
    .lean = s->lean, .lean_ms = s->lean_ms,
    .rich = s->rich, .rich_ms = s->rich_ms,
  };
  return a;
}
