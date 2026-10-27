/* Lean and rich warnings, on lambda alone and always armed.
 *
 *   LEAN   lambda above `lean`, or on the lean rail, for lean_ms
 *   RICH   lambda below `rich`, or on the rich rail, for rich_ms
 *
 * Street and race mode differ only in the limits they pass in (race: a
 * tighter lean limit and a hold of a few samples). The gauge has no
 * throttle signal, so LEAN also comes on during fuel cut, and in race mode
 * at idle and cruise.
 *
 * A warning clears as soon as lambda is back ALARM_HYSTERESIS inside its
 * limit, or when lambda becomes undefined.
 *
 * Lean ring: when a lean warning clears back to a safe reading, the ring
 * stays on for RING_MS so a driver who was looking at the track still sees
 * it. Only a lean-out counts, not the normal lean readings around it:
 *  - it must start from a safe reading (not from idle in race mode, or
 *    straight after power-up or a mode change),
 *  - it must never reach the lean rail (that is fuel cut on a lift),
 *  - it must last under RING_MAX_EPISODE_MS (longer is idle or cruise). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_HYSTERESIS    0.02f    /* lambda, to clear */
#define RING_MS             300000u  /* 5 minutes */
#define RING_MAX_EPISODE_MS 30000u

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

/* A lean warning that ended back at a safe reading and lit the ring. */
typedef struct {
  float    peak;      /* leanest lambda during it */
  uint32_t start_ms;
  uint32_t dur_ms;    /* from the warning to back to safe */
} lean_event_t;

typedef struct {
  alarm_track_t lean, rich;
  alarm_t       live;

  bool          safe_seen;   /* a safe reading since the last re-arm */
  bool          ep_active;   /* a lean warning is on */
  bool          ep_from_safe;
  bool          ep_rail;     /* it reached the lean rail */
  uint32_t      ep_start_ms;
  float         ep_peak;

  bool          ring;
  uint32_t      ring_since_ms;
  lean_event_t  last;        /* the event that lit the ring */
  bool          new_event;   /* set when the ring is lit; cleared by take */
} alarms_t;

alarm_cfg_t alarms_default_cfg(void);
void alarms_reset(alarms_t *a);
/* The limits changed (mode switch): start the warnings again, keep the ring.
 * A warning that is on afterwards does not light the ring. */
void alarms_rearm(alarms_t *a);
/* Returns the live warning. */
alarm_t alarms_update(alarms_t *a, const alarm_cfg_t *cfg, gauge_state_t st, float lambda,
                      uint32_t t_ms);
/* Is the lean ring on at t_ms? */
bool alarms_ring(const alarms_t *a, uint32_t t_ms);
void alarms_clear_ring(alarms_t *a);
/* The lean event that just lit the ring, once. */
bool alarms_take_event(alarms_t *a, lean_event_t *out);

const char *alarm_name(alarm_t a);
/* Log column: '0', 'R' or 'L'. */
char alarm_char(alarm_t a);
alarm_t alarm_parse_char(char c);

#ifdef __cplusplus
}
#endif
