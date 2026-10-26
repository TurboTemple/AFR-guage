#include "peaks.h"

#include <math.h>

void peaks_reset(peaks_t *p)
{
  p->rich = NAN;
  p->lean = NAN;
  p->rich_rail = false;
  p->lean_rail = false;
}

void peaks_update(peaks_t *p, gauge_state_t st, float lambda)
{
  if (st == ST_RICH_RAIL) p->rich_rail = true;
  if (st == ST_LEAN_RAIL) p->lean_rail = true;
  if (st != ST_READY || isnan(lambda)) return;
  if (isnan(p->rich) || lambda < p->rich) p->rich = lambda;
  if (isnan(p->lean) || lambda > p->lean) p->lean = lambda;
}

void warnings_reset(warnings_t *w)
{
  w->lean.active = w->lean.timing = false;
  w->rich.active = w->rich.timing = false;
}

static void track(warn_track_t *w, bool cond, bool clear, uint32_t hold_ms, uint32_t t_ms)
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

warn_t warnings_update(warnings_t *w, const warn_cfg_t *lean, const warn_cfg_t *rich,
                       gauge_state_t st, float lambda, uint32_t t_ms)
{
  bool undefined = isnan(lambda);

  bool lean_cond = st == ST_READY && !undefined && lambda > lean->lambda;
  bool lean_clear = undefined || lambda <= lean->lambda - WARN_HYSTERESIS;
  track(&w->lean, lean_cond, lean_clear, lean->hold_ms, t_ms);

  bool rich_cond = (st == ST_READY || st == ST_RICH_RAIL) && !undefined && lambda < rich->lambda;
  bool rich_clear = undefined || lambda >= rich->lambda + WARN_HYSTERESIS;
  track(&w->rich, rich_cond, rich_clear, rich->hold_ms, t_ms);

  if (w->rich.active) return WARN_RICH;
  if (w->lean.active) return WARN_LEAN;
  return WARN_NONE;
}
