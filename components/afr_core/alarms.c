#include "alarms.h"

#include <math.h>
#include <string.h>

alarm_cfg_t alarms_default_cfg(void)
{
  alarm_cfg_t c = {
    /* Street: closed-loop idle and cruise sit at 1.00 +/- 0.03, so 1.05
     * stays quiet there and still catches a real lean condition. 0.75 is
     * richer than any sensible petrol tune at full throttle. */
    .lean = 1.05f, .lean_ms = 300,
    .rich = 0.75f, .rich_ms = 300,
  };
  return c;
}

void alarms_reset(alarms_t *a)
{
  memset(a, 0, sizeof(*a));
  a->live = ALARM_NONE;
}

void alarms_rearm(alarms_t *a)
{
  memset(&a->lean, 0, sizeof a->lean);
  memset(&a->rich, 0, sizeof a->rich);
  a->live = ALARM_NONE;
  a->safe_seen = false;
  a->ep_active = false;
}

static void track(alarm_track_t *w, bool cond, bool clear, uint32_t hold_ms, uint32_t t_ms)
{
  if (w->active) {
    if (clear) w->active = false;
  } else if (cond) {
    if (!w->timing) {
      w->timing = true;
      w->since_ms = t_ms;
    }
    if (t_ms - w->since_ms >= hold_ms) w->active = true;
  }
  if (!cond) w->timing = false;
}

/* Follow a lean warning from start to end, and light the ring for a
 * lean-out that came back to safe. */
static void episode(alarms_t *a, bool defined, bool lean_rail, float lambda, uint32_t t)
{
  if (a->lean.active) {
    if (!a->ep_active) {
      a->ep_active = true;
      a->ep_from_safe = a->safe_seen;
      a->ep_rail = false;
      a->ep_start_ms = a->lean.since_ms;
      a->ep_peak = lambda;
    }
    if (lean_rail) a->ep_rail = true;
    else if (lambda > a->ep_peak) a->ep_peak = lambda;
    return;
  }
  if (!a->ep_active) return;
  a->ep_active = false;
  uint32_t dur = t - a->ep_start_ms;
  if (defined && a->ep_from_safe && !a->ep_rail && dur < RING_MAX_EPISODE_MS) {
    a->ring = true;
    a->ring_since_ms = t;
    a->last.peak = a->ep_peak;
    a->last.start_ms = a->ep_start_ms;
    a->last.dur_ms = dur;
    a->new_event = true;
  }
}

alarm_t alarms_update(alarms_t *a, const alarm_cfg_t *cfg, gauge_state_t st, float lambda,
                      uint32_t t)
{
  bool defined = !isnan(lambda) &&
                 (st == ST_READY || st == ST_RICH_RAIL || st == ST_LEAN_RAIL);
  bool lean_rail = st == ST_LEAN_RAIL, rich_rail = st == ST_RICH_RAIL;

  bool l_cond = defined && (lean_rail || lambda > cfg->lean);
  bool l_clear = !defined || (!lean_rail && lambda <= cfg->lean - ALARM_HYSTERESIS);
  track(&a->lean, l_cond, l_clear, cfg->lean_ms, t);
  episode(a, defined, lean_rail, lambda, t);
  if (!defined) a->safe_seen = false;
  else if (!l_cond && !a->lean.active) a->safe_seen = true;

  bool r_cond = defined && (rich_rail || lambda < cfg->rich);
  bool r_clear = !defined || (!rich_rail && lambda >= cfg->rich + ALARM_HYSTERESIS);
  track(&a->rich, r_cond, r_clear, cfg->rich_ms, t);

  a->live = a->lean.active ? ALARM_LEAN : a->rich.active ? ALARM_RICH : ALARM_NONE;
  return a->live;
}

bool alarms_ring(const alarms_t *a, uint32_t t)
{
  return a->ring && t - a->ring_since_ms < RING_MS;
}

void alarms_clear_ring(alarms_t *a)
{
  a->ring = false;
  a->new_event = false;
}

bool alarms_take_event(alarms_t *a, lean_event_t *out)
{
  if (!a->new_event) return false;
  a->new_event = false;
  *out = a->last;
  return true;
}

static const char *const k_names[ALARM_COUNT] = { "NONE", "RICH", "LEAN" };
static const char k_chars[ALARM_COUNT] = { '0', 'R', 'L' };

const char *alarm_name(alarm_t a)
{
  return (unsigned)a < ALARM_COUNT ? k_names[a] : "?";
}

char alarm_char(alarm_t a)
{
  return (unsigned)a < ALARM_COUNT ? k_chars[a] : '?';
}

alarm_t alarm_parse_char(char c)
{
  for (int i = 0; i < ALARM_COUNT; i++) {
    if (k_chars[i] == c) return (alarm_t)i;
  }
  return ALARM_NONE;
}
