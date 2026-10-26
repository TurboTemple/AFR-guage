/* Decimation with spike rejection, and display smoothing. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The lambda ADC runs continuously at 860 SPS; each block of DECIM_N raw
 * codes becomes one sample (about 215 Hz). The block mean is a boxcar
 * filter with nulls at 215 and 430 Hz, the frequencies that would alias
 * onto the output. A block whose spread exceeds the spike limit is taken as
 * holding a spike: its highest and lowest codes are dropped and the middle
 * two averaged. */
#define DECIM_N 4

typedef struct {
  int32_t  codes[DECIM_N];
  uint64_t t_us[DECIM_N];
  uint8_t  n;
  int32_t  spike_codes;
  uint32_t spikes;      /* blocks with a spike rejected, since reset */
} decim_t;

void decim_init(decim_t *d, int32_t spike_codes);
/* Add one raw code taken at t_us (64-bit: 32-bit microseconds wrap after
 * 71.6 minutes). Returns true when a block is complete; then *mean is its
 * value and *t_mid_us the middle of the block. */
bool decim_feed(decim_t *d, int16_t code, uint64_t t_us, float *mean, uint64_t *t_mid_us);
void decim_reset(decim_t *d);

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
