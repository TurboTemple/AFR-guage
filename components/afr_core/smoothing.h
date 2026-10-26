/* Spike filter and display smoothing. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Median of the last three samples. */
typedef struct {
  float   v[3];
  uint8_t n;
  uint8_t head;
} median3_t;

void  median3_reset(median3_t *m);
float median3_feed(median3_t *m, float x);

/* First-order filter: y += (x - y) * (1 - exp(-dt / tau)). */
typedef struct {
  float    y;
  bool     primed;
  uint32_t last_ms;
} smoother_t;

void smoother_reset(smoother_t *s);
/* tau_ms 0 passes x through. A NAN input resets the filter and returns NAN;
 * the next number starts it from that value. */
float smoother_feed(smoother_t *s, float x, uint32_t t_ms, uint16_t tau_ms);

#ifdef __cplusplus
}
#endif
