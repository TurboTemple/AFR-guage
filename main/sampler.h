/* Sampling.
 *
 * `sampler` (core 1, highest app priority): the lambda ADS1115 converts
 * continuously at 860 SPS and pulses ALERT/RDY on GPIO43 at each
 * conversion. The ISR timestamps the pulse and wakes the task, which reads
 * the code (one 2-byte I2C read), decimates by 4 with spike rejection and
 * runs afr_pipeline about every 4.65 ms. Every sample goes to the UI
 * (one-slot queue, xQueueOverwrite) and to the logger (zero wait; rows are
 * dropped and counted, never blocking).
 *
 * `aux` (core 1, below the sampler): the second ADS1115 in single-shot
 * mode, LED at 50 Hz, supply at 10 Hz. Values reach the sampler through a
 * mailbox, so only the sampler touches the pipeline. */
#pragma once

#include <stdbool.h>

#include "ads1115.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "pipeline.h"

#define SAMPLER_RDY_GPIO 43

typedef struct {
  uint32_t raw_conversions;  /* RDY pulses handled */
  uint32_t missed;           /* conversions not read in time */
  uint32_t rdy_timeouts;     /* no RDY for 5 ms */
  uint32_t spikes;           /* blocks with a spike rejected */
  uint32_t aux_errors;       /* failed housekeeping reads */
} sampler_stats_t;

/* lambda NULL: that ADC was not found (FAULT). aux NULL: the LED ADC was
 * not found (FAULT). */
esp_err_t sampler_start(ads1115_t *lambda, ads1115_t *aux, const afr_config_t *cfg,
                        const cal_coeffs_t *stored, bool supply_fitted);
/* Applied before the next sample. */
void sampler_set_config(const afr_config_t *cfg);
void sampler_reset_peaks(void);
/* The newest reading (non-blocking). */
bool sampler_latest(reading_t *out);
/* Bits are AFR_EVT_*. */
EventGroupHandle_t sampler_events(void);
void sampler_cal(cal_coeffs_t *cal, cal_source_t *src, bool *rejected);
/* The last lean-out that lit the ring (after AFR_EVT_LEAN_EVENT). */
lean_event_t sampler_lean_event(void);
sampler_stats_t sampler_stats(void);
