/* The per-sample signal chain, shared by the firmware sampler and the host
 * replay tool:
 *
 *   lambda ADC block mean -> calibrated volts -> state check -> lambda
 *   -> warnings and peaks -> display smoothing
 *
 * The firmware feeds one decimated lambda sample about every 4.65 ms
 * (860 SPS / 4) with afr_pipeline_step_raw(). The LED and supply come from
 * the second ADC at their own rates through the *_sample() calls; the
 * newest value is used at each step. Replay calls afr_pipeline_step_volts()
 * with a log's calibrated v_out and decoded LED, which skips calibration
 * and LED decoding. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"
#include "alarms.h"
#include "calibration.h"
#include "conversion.h"
#include "gauge_state.h"
#include "led_decoder.h"
#include "peaks.h"
#include "smoothing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Nominal processed-sample period, for the calibration window. */
#define AFR_SAMPLE_MS 5u

/* Events for the supervisor, collected with afr_pipeline_take_events(). */
enum {
  AFR_EVT_SPARTAN_UP   = 1u << 0, /* entered SEQUENCER: query the Spartan */
  AFR_EVT_SPARTAN_DOWN = 1u << 1, /* returned to NO_SPARTAN */
  AFR_EVT_CAL_ACCEPTED = 1u << 2, /* fresh calibration: save it */
  AFR_EVT_CAL_REJECTED = 1u << 3, /* fit rejected: log CAL_REJECTED */
  AFR_EVT_ALARM        = 1u << 4, /* a warning began */
  AFR_EVT_LEAN_EVENT   = 1u << 5, /* a lean-out lit the ring: lean_event */
};

typedef struct {
  lambda_range_t range;   /* from GETLAMZEROV / GETLAMFIVEV */
  float       stoich;     /* of the selected fuel */
  uint16_t    smooth_ms;  /* display filter tau */
  bool        race;       /* race mode (logged; the limits are in alarm) */
  alarm_cfg_t alarm;
} afr_config_t;

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

  led_decoder_t  led;
  cal_detector_t det;
  gauge_sm_t     sm;
  peaks_t        peaks;
  alarms_t       alarms;
  smoother_t     smooth;

  float          v_out;
  float          supply_v;
  led_pattern_t  led_pattern;
  alarm_t        last_alarm;
  lean_event_t   lean_event;  /* the last lean-out (AFR_EVT_LEAN_EVENT) */
  uint8_t        events;
} afr_pipeline_t;

/* stored: a saved calibration, or NULL for nominal. */
void afr_pipeline_init(afr_pipeline_t *p, const afr_config_t *cfg, const cal_coeffs_t *stored,
                       bool supply_fitted, uint16_t sample_ms, uint32_t t_ms);
/* Initialisation complete: BOOT -> NO_SPARTAN. */
void afr_pipeline_start(afr_pipeline_t *p, uint32_t t_ms);
void afr_pipeline_fault(afr_pipeline_t *p, fault_reason_t reason, uint32_t t_ms);
/* New limits re-arm the warnings (alarms_rearm). */
void afr_pipeline_set_config(afr_pipeline_t *p, const afr_config_t *cfg);
/* Long press: peaks and the lean ring. */
void afr_pipeline_reset_peaks(afr_pipeline_t *p);
uint8_t afr_pipeline_take_events(afr_pipeline_t *p);

/* Second-ADC inputs, whenever they are read. A failed read passes ok false. */
void afr_pipeline_led_sample(afr_pipeline_t *p, uint32_t t_ms, int16_t code);
void afr_pipeline_supply_sample(afr_pipeline_t *p, bool ok, int16_t code);

/* One decimated lambda sample: the mean ADC code of its block. */
void afr_pipeline_step_raw(afr_pipeline_t *p, uint32_t t_ms, bool ok, float code, reading_t *out);
/* Replay. supply_v NAN if not known. */
void afr_pipeline_step_volts(afr_pipeline_t *p, uint32_t t_ms, float v_out, led_pattern_t led,
                             float supply_v, reading_t *out);

#ifdef __cplusplus
}
#endif
