/* Volts to lambda/AFR, and the fuel table. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADS1115 at +/-4.096 V full scale: 125 uV per code. */
#define ADS_LSB_V          125e-6f
/* Every 0-5 V Spartan signal passes a 10k/15k divider: x0.6 at the ADC. */
#define DIVIDER_RATIO      0.6f
/* Nominal calibration: Spartan volts per ADC code, and offset. */
#define CAL_A_NOMINAL      (ADS_LSB_V / DIVIDER_RATIO)
#define CAL_B_NOMINAL      0.0f
/* Optional supply-sense divider on AIN2: 47k over 10k. */
#define SUPPLY_DIVIDER     (10.0f / 57.0f)

/* Lambda ADC: continuous at 860 SPS, decimated by 4 to about 215 Hz. */
#define LAMBDA_RAW_HZ      860u
/* A block of 4 raw codes spread wider than this holds a spike: about
 * 31 mV at the Spartan output, 0.004 lambda. */
#define SPIKE_CODES        150

/* Rails: the Spartan output is clipped beyond these. */
#define RAIL_RICH_V        0.02f
#define RAIL_LEAN_V        4.98f

/* Factory linear-output end points and the programmable limits. */
#define LAMBDA_0V_DEFAULT  0.68f
#define LAMBDA_5V_DEFAULT  1.36f
#define LAMBDA_END_MIN     0.60f
#define LAMBDA_END_MAX     3.40f

typedef enum { FUEL_PETROL, FUEL_E10, FUEL_E85, FUEL_CUSTOM, FUEL_COUNT } fuel_t;

typedef struct {
  float lam0; /* lambda at 0 V */
  float lam5; /* lambda at 5 V */
} lambda_range_t;

/* Stoichiometric AFR for a fuel; custom uses custom_stoich. */
float conv_stoich(fuel_t fuel, float custom_stoich);
/* Short label for the unit line: "Petrol", "E10", "E85", "Custom". */
const char *conv_fuel_name(fuel_t fuel);

/* ADC code to Spartan output volts: v = a * code + b. */
float conv_code_to_volts(float a, float b, int32_t code);
/* ADC code to volts at the ADC pin. */
float conv_code_to_adc_volts(int32_t code);
/* ADC code on AIN2 to Spartan supply volts. */
float conv_code_to_supply_v(int32_t code);

/* lambda = lam0 + (lam5 - lam0) * v / 5 */
float conv_lambda(const lambda_range_t *r, float v_out);
/* True if both end points are within 0.60-3.40 and lam5 > lam0. */
bool conv_range_valid(float lam0, float lam5);
lambda_range_t conv_range_default(void);

#ifdef __cplusplus
}
#endif
