#include "peaks.h"

#include <math.h>

void peaks_reset(peaks_t *p)
{
  p->rich = NAN;
  p->lean = NAN;
  p->rich_rail = false;
  p->lean_rail = false;
}

void peaks_update(peaks_t *p, gauge_state_t st, float lambda, bool counts)
{
  if (!counts) return;
  if (st == ST_RICH_RAIL) p->rich_rail = true;
  if (st == ST_LEAN_RAIL) p->lean_rail = true;
  if (st != ST_READY || isnan(lambda)) return;
  if (isnan(p->rich) || lambda < p->rich) p->rich = lambda;
  if (isnan(p->lean) || lambda > p->lean) p->lean = lambda;
}
