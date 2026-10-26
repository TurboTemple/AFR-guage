#include "conversion.h"

#include <math.h>

/* E10 and E85 from AA1Car. The Spartan scales to petrol at 14.7, so lambda
 * is the true reading whatever the fuel. */
static const float k_stoich[FUEL_CUSTOM] = { 14.7f, 14.08f, 9.7f };
static const char *const k_names[FUEL_COUNT] = { "Petrol", "E10", "E85", "Custom" };

float conv_stoich(fuel_t fuel, float custom_stoich)
{
  if (fuel == FUEL_CUSTOM) return custom_stoich;
  if ((unsigned)fuel < FUEL_CUSTOM) return k_stoich[fuel];
  return k_stoich[FUEL_PETROL];
}

const char *conv_fuel_name(fuel_t fuel)
{
  return (unsigned)fuel < FUEL_COUNT ? k_names[fuel] : k_names[FUEL_PETROL];
}

float conv_code_to_volts(float a, float b, int32_t code)
{
  return a * (float)code + b;
}

float conv_code_to_adc_volts(int32_t code)
{
  return ADS_LSB_V * (float)code;
}

float conv_code_to_supply_v(int32_t code)
{
  return conv_code_to_adc_volts(code) / SUPPLY_DIVIDER;
}

bool conv_tps_learnt(float v_closed, float v_wot)
{
  return !isnan(v_closed) && !isnan(v_wot) && fabsf(v_wot - v_closed) >= TPS_MIN_SPAN_V;
}

float conv_tps_pct(float v, float v_closed, float v_wot)
{
  if (!conv_tps_learnt(v_closed, v_wot) || isnan(v) || v < TPS_V_MIN || v > TPS_V_MAX) return NAN;
  float pct = 100.0f * (v - v_closed) / (v_wot - v_closed);
  return pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct;
}

float conv_lambda(const lambda_range_t *r, float v_out)
{
  return r->lam0 + (r->lam5 - r->lam0) * v_out / 5.0f;
}

bool conv_range_valid(float lam0, float lam5)
{
  return lam0 >= LAMBDA_END_MIN && lam0 <= LAMBDA_END_MAX &&
         lam5 >= LAMBDA_END_MIN && lam5 <= LAMBDA_END_MAX && lam5 > lam0;
}

lambda_range_t conv_range_default(void)
{
  lambda_range_t r = { LAMBDA_0V_DEFAULT, LAMBDA_5V_DEFAULT };
  return r;
}
