/* Alarms for dangerous mixtures, always armed.
 *
 * Lambda alone cannot tell a lean-out at full throttle from fuel cut on a
 * lift (both go full lean), so the lean alarms read the throttle position:
 *
 *   LEAN_LOAD (danger)  throttle at or above load_pct, and lambda above
 *                       lean_load or on the lean rail, for lean_load_ms
 *   LEAN (warning)      throttle above closed_pct, and lambda above
 *                       lean_part (or on the lean rail), for lean_part_ms
 *   RICH (warning)      lambda below rich, or on the rich rail, for rich_ms
 *
 * With the throttle closed, lean is fuel cut and never alarms. Without a
 * throttle signal the load alarm is off, and the lean rail is ignored
 * (it may be fuel cut); LEAN still runs on lambda alone.
 *
 * An alarm clears when lambda is back WARN_HYSTERESIS inside its limit, when
 * its throttle condition ends, or when lambda becomes undefined. The worst
 * alarm is latched as an event until acknowledged. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_HYSTERESIS   0.02f  /* lambda, to clear */
#define LOAD_HYSTERESIS    5.0f   /* throttle %, to leave "under load" */

typedef struct {
  float    lean_load;     /* lambda limit under load */
  uint32_t lean_load_ms;
  float    lean_part;     /* lambda limit with the throttle open */
  uint32_t lean_part_ms;
  float    rich;          /* lambda limit, any throttle */
  uint32_t rich_ms;
  float    load_pct;      /* under load from this throttle % */
  float    closed_pct;    /* closed throttle at or below this % */
} alarm_cfg_t;

typedef struct {
  bool     active;
  bool     timing;
  uint32_t since_ms;
} alarm_track_t;

typedef struct {
  alarm_track_t lean_load, lean_part, rich;
  bool          under_load;
  alarm_t       live;
  alarm_event_t current;   /* the alarm in progress */
  alarm_event_t pending;   /* the worst finished one, unacknowledged */
  bool          has_pending;
} alarms_t;

alarm_cfg_t alarms_default_cfg(void);
void alarms_reset(alarms_t *a);
/* tps: throttle %, NAN if unknown. Returns the live alarm. */
alarm_t alarms_update(alarms_t *a, const alarm_cfg_t *cfg, gauge_state_t st, float lambda,
                      float tps, uint32_t t_ms);
bool alarms_under_load(const alarms_t *a);
/* The event to show: the one in progress, else the pending one. */
bool alarms_event(const alarms_t *a, alarm_event_t *out);
/* The driver has seen it. An alarm still in progress stays. */
void alarms_ack(alarms_t *a);

const char *alarm_name(alarm_t a);
/* Log column: '0', 'R', 'L' or 'D'. */
char alarm_char(alarm_t a);
alarm_t alarm_parse_char(char c);

#ifdef __cplusplus
}
#endif
