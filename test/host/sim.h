/* Signal generators shared by the host tests. */
#pragma once

#include <math.h>
#include <stdint.h>

#include "conversion.h"

/* Deterministic noise: uniform in [-1, 1). */
static uint32_t sim_seed_ = 12345u;
static inline void sim_srand(uint32_t s) { sim_seed_ = s; }
static inline float sim_noise(void)
{
  sim_seed_ = sim_seed_ * 1664525u + 1013904223u;
  return (float)(sim_seed_ >> 8) / 8388608.0f - 1.0f;
}

/* ADC code for a Spartan output voltage seen through a chain whose true
 * calibration is v = a * code + b, plus noise_v of uniform noise. */
static inline int16_t sim_code(float v, float a, float b, float noise_v)
{
  float c = (v + noise_v * sim_noise() - b) / a;
  if (c < 0.0f) c = 0.0f;
  if (c > 32767.0f) c = 32767.0f;
  return (int16_t)lrintf(c);
}

/* Code for a voltage at the ADC pin (AIN1). */
static inline int16_t sim_adc_code(float adc_v)
{
  float c = adc_v / ADS_LSB_V;
  if (c < 0.0f) c = 0.0f;
  if (c > 32767.0f) c = 32767.0f;
  return (int16_t)lrintf(c);
}

/* Blink waveform: on for on_ms at the start of each period_ms. period 0 is
 * steady at `on`; on_ms 0 is off. */
static inline int sim_blink(uint32_t t_ms, uint32_t period_ms, uint32_t on_ms)
{
  if (on_ms == 0) return 0;
  if (period_ms == 0) return 1;
  return (t_ms % period_ms) < on_ms;
}

/* Spartan output volts for a lambda with factory end points. */
static inline float sim_v_for_lambda(float lambda)
{
  return (lambda - LAMBDA_0V_DEFAULT) / (LAMBDA_5V_DEFAULT - LAMBDA_0V_DEFAULT) * 5.0f;
}
