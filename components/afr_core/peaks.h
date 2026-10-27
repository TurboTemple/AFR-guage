/* Peak hold: the richest and leanest lambda seen. */
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
/* Numbers come from READY only; rails are flagged separately. */
void peaks_update(peaks_t *p, gauge_state_t st, float lambda);

#ifdef __cplusplus
}
#endif
