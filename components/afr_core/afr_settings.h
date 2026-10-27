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

#define SETTINGS_SCHEMA 4

typedef enum { UNIT_LAMBDA, UNIT_AFR } unit_t;

/* Race-mode defaults for a naturally aspirated petrol engine at full
 * throttle: best power around 0.85-0.88, caution from about 0.90, real
 * concern from 0.93-0.95 (see README, Race mode). */
#define RACE_TARGET_DEFAULT  0.86f
#define RACE_LEAN_DEFAULT    0.92f
#define RACE_LEAN_MS_DEFAULT 15u
#define STREET_TARGET        1.00f
#define TARGET_WINDOW        0.02f  /* +/- around the target: on target */

typedef struct {
  uint8_t  fuel;            /* fuel_t */
  float    fuel_custom;     /* stoich for FUEL_CUSTOM, 5.0-20.0 */
  uint8_t  primary_unit;    /* unit_t */
  bool     show_afr;        /* add the AFR line below the unit */

  bool     race;            /* race mode: double-tap on the gauge */
  float    lean;            /* street: lambda 0.85-1.30, lean warning limit */
  uint16_t lean_ms;         /* street: 100-3000 */
  float    race_lean;       /* race: lambda 0.80-1.00 */
  uint16_t race_lean_ms;    /* race: 0-500 */
  float    race_target;     /* race: lambda 0.75-0.95, the tune's full-throttle target */
  float    rich;            /* both modes: lambda 0.68-0.90 */
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
/* The warning limits for the selected mode. */
alarm_cfg_t settings_alarm_cfg(const afr_settings_t *s);
/* Target lambda for the selected mode: 1.00 in street, race_target in race. */
float settings_target(const afr_settings_t *s);

#ifdef __cplusplus
}
#endif
