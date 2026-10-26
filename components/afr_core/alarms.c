#include "alarms.h"

#include <math.h>
#include <string.h>

alarm_cfg_t alarms_default_cfg(void)
{
  alarm_cfg_t c = {
    .lean_load = 0.90f, .lean_load_ms = 100,
    .lean_part = 1.10f, .lean_part_ms = 500,
    .rich = 0.72f, .rich_ms = 500,
    .load_pct = 30.0f, .closed_pct = 3.0f,
  };
  return c;
}

void alarms_reset(alarms_t *a)
{
  memset(a, 0, sizeof(*a));
  a->live = ALARM_NONE;
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

static bool worse(alarm_t type, float a, float b)
{
  return type == ALARM_RICH ? a < b : a > b;
}

/* Keep the event record: open, escalate, track the worst, close. */
static void record(alarms_t *a, alarm_t live, float lambda, float tps, uint32_t t)
{
  alarm_event_t *c = &a->current;
  if (live != ALARM_NONE) {
    if (c->type == ALARM_NONE) {
      c->type = (uint8_t)live;
      c->lambda = lambda;
      c->tps = tps;
      c->start_ms = t;
    } else if (live > c->type) {
      c->type = (uint8_t)live;
      c->lambda = lambda;
    } else if (live == c->type && worse(live, lambda, c->lambda)) {
      c->lambda = lambda;
    }
    c->dur_ms = t - c->start_ms;
    return;
  }
  if (c->type != ALARM_NONE) {
    c->dur_ms = t - c->start_ms;
    if (!a->has_pending || c->type >= a->pending.type) {
      a->pending = *c;
      a->has_pending = true;
    }
    memset(c, 0, sizeof(*c));
  }
}

alarm_t alarms_update(alarms_t *a, const alarm_cfg_t *cfg, gauge_state_t st, float lambda,
                      float tps, uint32_t t)
{
  bool defined = !isnan(lambda) &&
                 (st == ST_READY || st == ST_RICH_RAIL || st == ST_LEAN_RAIL);
  bool tps_ok = !isnan(tps);
  bool lean_rail = st == ST_LEAN_RAIL;

  if (!tps_ok) a->under_load = false;
  else if (tps >= cfg->load_pct) a->under_load = true;
  else if (tps < cfg->load_pct - LOAD_HYSTERESIS) a->under_load = false;
  bool closed = tps_ok && tps <= cfg->closed_pct;

  /* Lean under load: the dangerous one. */
  bool ll_cond = defined && a->under_load && (lean_rail || lambda > cfg->lean_load);
  bool ll_clear = !defined || !a->under_load ||
                  (!lean_rail && lambda <= cfg->lean_load - ALARM_HYSTERESIS);
  track(&a->lean_load, ll_cond, ll_clear, cfg->lean_load_ms, t);

  /* Lean with the throttle open. Without TPS the rail may be fuel cut. */
  bool open = tps_ok ? !closed : true;
  bool rail_counts = tps_ok && lean_rail;
  bool lp_cond = defined && open && (rail_counts || (!lean_rail && lambda > cfg->lean_part));
  bool lp_clear = !defined || !open || (!rail_counts && lambda <= cfg->lean_part - ALARM_HYSTERESIS);
  track(&a->lean_part, lp_cond, lp_clear, cfg->lean_part_ms, t);

  bool r_cond = defined && (st == ST_RICH_RAIL || lambda < cfg->rich);
  bool r_clear = !defined || (st != ST_RICH_RAIL && lambda >= cfg->rich + ALARM_HYSTERESIS);
  track(&a->rich, r_cond, r_clear, cfg->rich_ms, t);

  alarm_t live = a->lean_load.active ? ALARM_LEAN_LOAD :
                 a->lean_part.active ? ALARM_LEAN :
                 a->rich.active ? ALARM_RICH : ALARM_NONE;
  a->live = live;
  record(a, live, lambda, tps, t);
  return live;
}

bool alarms_under_load(const alarms_t *a)
{
  return a->under_load;
}

bool alarms_event(const alarms_t *a, alarm_event_t *out)
{
  if (a->current.type != ALARM_NONE &&
      (!a->has_pending || a->current.type >= a->pending.type)) {
    *out = a->current;
    return true;
  }
  if (a->has_pending) {
    *out = a->pending;
    return true;
  }
  return false;
}

void alarms_ack(alarms_t *a)
{
  a->has_pending = false;
  memset(&a->pending, 0, sizeof(a->pending));
}

static const char *const k_names[ALARM_COUNT] = { "NONE", "RICH", "LEAN", "LEAN AT LOAD" };
static const char k_chars[ALARM_COUNT] = { '0', 'R', 'L', 'D' };

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
