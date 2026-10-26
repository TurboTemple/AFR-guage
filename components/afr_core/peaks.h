/* Peak hold, and lean/rich warnings with hysteresis. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WARN_HYSTERESIS 0.02f

typedef struct {
  float    rich;        /* lowest lambda in READY; NAN if none */
  float    lean;        /* highest lambda in READY; NAN if none */
  bool     rich_rail;   /* rich rail seen */
  bool     lean_rail;   /* lean rail seen */
} peaks_t;

void peaks_reset(peaks_t *p);
void peaks_update(peaks_t *p, gauge_state_t st, float lambda);

typedef struct {
  float    lambda;      /* threshold */
  uint32_t hold_ms;     /* condition must hold this long */
} warn_cfg_t;

typedef struct {
  bool     active;
  bool     timing;
  uint32_t since_ms;
} warn_track_t;

typedef struct {
  warn_track_t lean;
  warn_track_t rich;
} warnings_t;

void warnings_reset(warnings_t *w);
/* Lean: lambda above lean.lambda in READY only, so overrun fuel cut on the
 * lean rail never fires it. Rich: lambda below rich.lambda in READY or
 * RICH_RAIL. Each clears when lambda is back WARN_HYSTERESIS inside its
 * threshold, or when lambda is undefined. */
warn_t warnings_update(warnings_t *w, const warn_cfg_t *lean, const warn_cfg_t *rich,
                       gauge_state_t st, float lambda, uint32_t t_ms);

#ifdef __cplusplus
}
#endif
