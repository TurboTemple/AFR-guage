/* User settings: defaults and allowed ranges. Stored in NVS by the settings
 * component; kept here so the validation is host-tested. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "conversion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SCHEMA 1

typedef enum { UNIT_LAMBDA, UNIT_AFR } unit_t;

typedef struct {
  uint8_t  fuel;            /* fuel_t */
  float    fuel_custom;     /* stoich for FUEL_CUSTOM, 5.0-20.0 */
  uint8_t  primary_unit;    /* unit_t */
  bool     show_afr;        /* add the AFR line below the unit */
  float    warn_lean;       /* lambda 1.00-1.30 */
  uint16_t warn_lean_ms;    /* 200-5000 */
  float    warn_rich;       /* lambda 0.68-0.95 */
  uint16_t warn_rich_ms;    /* 200-5000 */
  uint16_t smooth_ms;       /* 0-500 */
  uint8_t  log_rate_hz;     /* 10, 20 or 50 */
  uint8_t  bright_day;      /* 5-100 % */
  uint8_t  bright_night;    /* 5-100 % */
  bool     night;           /* night brightness selected */
  bool     cal_valid;
  float    cal_a;
  float    cal_b;
  uint32_t cal_time;        /* unix seconds from the RTC; 0 if unknown */
} afr_settings_t;

void settings_defaults(afr_settings_t *s);
/* Clamp every field into range (log rate to the nearest allowed value).
 * Drops a stored calibration that is not plausible. Returns true if
 * anything changed. */
bool settings_validate(afr_settings_t *s);
/* Stoich for the selected fuel. */
float settings_stoich(const afr_settings_t *s);

#ifdef __cplusplus
}
#endif
