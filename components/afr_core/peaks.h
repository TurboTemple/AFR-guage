/* Peak hold: the richest and leanest lambda seen where it matters. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float    rich;        /* lowest lambda; NAN if none */
  float    lean;        /* highest lambda; NAN if none */
  bool     rich_rail;   /* rich rail seen */
  bool     lean_rail;   /* lean rail seen */
} peaks_t;

void peaks_reset(peaks_t *p);
/* counts: this sample is eligible (under load when the throttle is known,
 * else any READY sample). Numbers come from READY only; rails are flagged
 * separately, and only while counts. */
void peaks_update(peaks_t *p, gauge_state_t st, float lambda, bool counts);

#ifdef __cplusplus
}
#endif
