/* Two-point calibration from the Spartan's start-up output sequencer.
 *
 * At power-up the linear output holds 1.666 V, then 3.333 V, before real
 * readings. The detector finds both flat plateaus in a stream of volts
 * (nominal conversion), and cal_fit() turns their mean ADC codes into the
 * line v = a * code + b that cancels divider tolerance, ADC gain and ground
 * offset. Timings vary between Spartan generations, so plateaus are found by
 * flatness, never by fixed times. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_PLATEAU1_V   1.666f
#define CAL_PLATEAU2_V   3.333f
#define CAL_WINDOW_MS    500u    /* rolling standard deviation window */
#define CAL_STD_MAX_V    0.002f  /* flat: std dev under 2 mV */
#define CAL_MEAN_TOL_V   0.060f  /* mean within 60 mV of the plateau */
#define CAL_MIN_RUN_MS   2000u   /* a plateau lasts at least 2 s */
#define CAL_MAX_GAP_MS   2000u   /* plateau 2 starts within 2 s of plateau 1 */
#define CAL_TRIM         0.20f   /* average the middle 60 % */
#define CAL_A_TOL        0.03f   /* accept a within +/-3 % of nominal */
#define CAL_B_MAX_V      0.050f  /* accept |b| under 50 mV */

#define CAL_WINDOW_MAX   128u
#define CAL_RUN_MAX      2048u   /* about 10 s at 200 Hz */

typedef struct {
  float a; /* Spartan volts per ADC code */
  float b; /* offset, volts */
} cal_coeffs_t;

typedef enum { CAL_SRC_NOMINAL, CAL_SRC_STORED, CAL_SRC_FRESH } cal_source_t;

typedef enum {
  CAL_SEARCH_1, CAL_IN_1, CAL_SEARCH_2, CAL_IN_2, CAL_CAPTURED
} cal_phase_t;

typedef struct {
  uint16_t win_n;
  uint16_t win_head;
  uint16_t win_count;
  float    win[CAL_WINDOW_MAX];
  uint32_t win_t[CAL_WINDOW_MAX];

  cal_phase_t phase;
  uint16_t run_n;
  uint32_t run_start_ms;
  uint32_t run_last_ms;
  float    run[CAL_RUN_MAX];

  uint32_t p1_end_ms;
  float    m1; /* trimmed mean of plateau 1, input units */
  float    m2; /* trimmed mean of plateau 2, input units */
} cal_detector_t;

cal_coeffs_t cal_nominal(void);

/* sample_period_ms sets how many samples make the 0.5 s window. */
void cal_detector_init(cal_detector_t *d, uint16_t sample_period_ms);
void cal_detector_reset(cal_detector_t *d);
/* Feed one sample in volts (nominal conversion). Returns true once, on the
 * sample that completes plateau 2. */
bool cal_detector_feed(cal_detector_t *d, uint32_t t_ms, float v);
/* Close a plateau 2 that is still running (sequencer time-out). Returns true
 * if that completes the capture. */
bool cal_detector_finish(cal_detector_t *d);
bool cal_detector_captured(const cal_detector_t *d, float *m1, float *m2);

/* Fit from the two plateau mean codes. Writes the fit to out whether or not
 * it is accepted; returns true if accepted. */
bool cal_fit(float c1, float c2, cal_coeffs_t *out);
/* True if a and b are plausible (the acceptance test of cal_fit). */
bool cal_plausible(const cal_coeffs_t *c);

#ifdef __cplusplus
}
#endif
