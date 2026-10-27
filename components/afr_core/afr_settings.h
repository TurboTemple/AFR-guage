/* User settings: defaults and allowed ranges. Stored in NVS by the settings
 * component; kept here so the validation is host-tested. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alarms.h"
#include "conversion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_SCHEMA 3

typedef enum { UNIT_LAMBDA, UNIT_AFR } unit_t;

typedef struct {
  uint8_t  fuel;            /* fuel_t */
  float    fuel_custom;     /* stoich for FUEL_CUSTOM, 5.0-20.0 */
  uint8_t  primary_unit;    /* unit_t */
  bool     show_afr;        /* add the AFR line below the unit */

  float    lean;            /* lambda 0.85-1.30: lean warning limit */
  uint16_t lean_ms;         /* 100-3000 */
  float    rich;            /* lambda 0.68-0.90: rich warning limit */
  uint16_t rich_ms;         /* 100-3000 */

  uint16_t smooth_ms;       /* 0-500: display only; warnings and logs are unsmoothed */
  uint8_t  spartan_perf;    /* 0: 20 ms, 1: 10 ms, 2: lean; enforced at each power-up */
  uint8_t  bright_day;      /* 5-100 % */
  uint8_t  bright_night;    /* 5-100 % */
  bool     night;           /* night brightness selected */

  bool     cal_valid;
  float    cal_a;
  float    cal_b;
  uint32_t cal_time;        /* unix seconds from the RTC; 0 if unknown */
} afr_settings_t;

void settings_defaults(afr_settings_t *s);
/* Clamp every field into range, and drop a stored calibration that is not
 * plausible. Returns true if anything changed. */
bool settings_validate(afr_settings_t *s);
/* Stoich for the selected fuel. */
float settings_stoich(const afr_settings_t *s);
alarm_cfg_t settings_alarm_cfg(const afr_settings_t *s);

#ifdef __cplusplus
}
#endif
