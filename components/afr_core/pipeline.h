/* The per-sample signal chain, shared by the firmware sampler and the host
 * replay tool:
 *
 *   ADC code -> calibrated volts -> spike filter -> state check -> lambda
 *   -> peaks and warnings -> display smoothing
 *
 * The firmware calls afr_pipeline_step_raw() every 5 ms with ADC codes.
 * Replay calls afr_pipeline_step_volts() with a log's calibrated v_out and
 * decoded LED pattern, which skips calibration and LED decoding. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"
#include "calibration.h"
#include "conversion.h"
#include "gauge_state.h"
#include "led_decoder.h"
#include "peaks.h"
#include "smoothing.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AFR_SAMPLE_MS 5u

/* Events for the supervisor, collected with afr_pipeline_take_events(). */
enum {
  AFR_EVT_SPARTAN_UP   = 1u << 0, /* entered SEQUENCER: query the Spartan */
  AFR_EVT_SPARTAN_DOWN = 1u << 1, /* returned to NO_SPARTAN */
  AFR_EVT_CAL_ACCEPTED = 1u << 2, /* fresh calibration: save it */
  AFR_EVT_CAL_REJECTED = 1u << 3, /* fit rejected: log CAL_REJECTED */
};

typedef struct {
  lambda_range_t range;   /* from GETLAMZEROV / GETLAMFIVEV */
  float      stoich;      /* of the selected fuel */
  uint16_t   smooth_ms;   /* display filter tau */
  warn_cfg_t lean;
  warn_cfg_t rich;
} afr_config_t;

typedef struct {
  uint32_t t_ms;
  bool     lam_ok;        /* AIN0 read succeeded */
  int16_t  lam_code;
  bool     led_new;       /* AIN1 read this sample, and succeeded */
  int16_t  led_code;
  bool     supply_new;    /* AIN2 read this sample, and succeeded */
  int16_t  supply_code;
} afr_raw_t;

typedef struct {
  afr_config_t cfg;
  bool         supply_fitted;
  bool         raw_mode;

  cal_coeffs_t cal;
  cal_source_t cal_src;
  bool         cal_rejected;
  bool         cal_done;      /* plateaus captured this power-up */
  cal_coeffs_t last_fit;      /* last fit attempted */
  float        c1, c2;        /* its plateau mean codes */

  median3_t      med;
  led_decoder_t  led;
  cal_detector_t det;
  gauge_sm_t     sm;
  peaks_t        peaks;
  warnings_t     warn;
  smoother_t     smooth;

  float          v_out;
  float          supply_v;
  led_pattern_t  led_pattern;
  uint8_t        events;
} afr_pipeline_t;

/* stored: a saved calibration, or NULL for nominal. */
void afr_pipeline_init(afr_pipeline_t *p, const afr_config_t *cfg, const cal_coeffs_t *stored,
                       bool supply_fitted, uint16_t sample_ms, uint32_t t_ms);
/* Initialisation complete: BOOT -> NO_SPARTAN. */
void afr_pipeline_start(afr_pipeline_t *p, uint32_t t_ms);
void afr_pipeline_fault(afr_pipeline_t *p, fault_reason_t reason, uint32_t t_ms);
void afr_pipeline_set_config(afr_pipeline_t *p, const afr_config_t *cfg);
void afr_pipeline_reset_peaks(afr_pipeline_t *p);
uint8_t afr_pipeline_take_events(afr_pipeline_t *p);

void afr_pipeline_step_raw(afr_pipeline_t *p, const afr_raw_t *in, reading_t *out);
/* supply_v NAN if not fitted. */
void afr_pipeline_step_volts(afr_pipeline_t *p, uint32_t t_ms, float v_out, led_pattern_t led,
                             float supply_v, reading_t *out);

#ifdef __cplusplus
}
#endif
