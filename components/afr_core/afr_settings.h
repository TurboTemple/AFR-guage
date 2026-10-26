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

#define SETTINGS_SCHEMA 2

typedef enum { UNIT_LAMBDA, UNIT_AFR } unit_t;

typedef struct {
  uint8_t  fuel;            /* fuel_t */
  float    fuel_custom;     /* stoich for FUEL_CUSTOM, 5.0-20.0 */
  uint8_t  primary_unit;    /* unit_t */
  bool     show_afr;        /* add the AFR line below the unit */

  float    lean_load;       /* lambda 0.75-1.05: danger limit under load (0.92) */
  uint16_t lean_load_ms;    /* 20-1000 */
  float    lean_part;       /* lambda 1.00-1.30: limit with the throttle open */
  uint16_t lean_part_ms;    /* 100-3000 */
  float    rich;            /* lambda 0.68-0.85 */
  uint16_t rich_ms;         /* 100-3000 */
  uint8_t  load_pct;        /* 10-90: under load from this throttle % */
  uint8_t  closed_pct;      /* 0-20: closed throttle (fuel cut) up to this % */
  float    tps_closed_v;    /* learnt TPS volts; NAN if not learnt */
  float    tps_wot_v;

  uint16_t smooth_ms;       /* 0-500: display only; alarms and logs are unsmoothed */
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
/* Clamp every field into range. Drops a stored calibration that is not
 * plausible and a TPS learn whose two ends are too close. Returns true if
 * anything changed. */
bool settings_validate(afr_settings_t *s);
/* Stoich for the selected fuel. */
float settings_stoich(const afr_settings_t *s);
alarm_cfg_t settings_alarm_cfg(const afr_settings_t *s);

#ifdef __cplusplus
}
#endif
