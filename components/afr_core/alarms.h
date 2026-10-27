/* Lean and rich warnings, on lambda alone and always armed.
 *
 *   LEAN   lambda above `lean`, or on the lean rail, for lean_ms
 *   RICH   lambda below `rich`, or on the rich rail, for rich_ms
 *
 * The gauge has no throttle signal, so it cannot tell a lean-out from fuel
 * cut on a lift: LEAN also comes on during fuel cut. That is expected.
 *
 * A warning clears as soon as lambda is back ALARM_HYSTERESIS inside its
 * limit, or when lambda becomes undefined. Nothing is latched. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_HYSTERESIS 0.02f /* lambda, to clear */

typedef struct {
  float    lean;      /* lambda limit */
  uint32_t lean_ms;
  float    rich;      /* lambda limit */
  uint32_t rich_ms;
} alarm_cfg_t;

typedef struct {
  bool     active;
  bool     timing;
  uint32_t since_ms;
} alarm_track_t;

typedef struct {
  alarm_track_t lean, rich;
  alarm_t       live;
} alarms_t;

alarm_cfg_t alarms_default_cfg(void);
void alarms_reset(alarms_t *a);
/* Returns the live warning. */
alarm_t alarms_update(alarms_t *a, const alarm_cfg_t *cfg, gauge_state_t st, float lambda,
                      uint32_t t_ms);

const char *alarm_name(alarm_t a);
/* Log column: '0', 'R' or 'L'. */
char alarm_char(alarm_t a);
alarm_t alarm_parse_char(char c);

#ifdef __cplusplus
}
#endif
