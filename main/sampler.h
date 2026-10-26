/* The `sampler` task (core 1, highest app priority): every 5 ms it reads
 * the ADS1115 round-robin (AIN0 every 5 ms, AIN1 every 20 ms, AIN2 every
 * 100 ms), runs afr_pipeline, hands the newest reading to the UI through a
 * one-slot queue (xQueueOverwrite) and every reading at the log rate to the
 * logger's 256-slot queue, dropping and counting rows rather than ever
 * blocking. */
#pragma once

#include <stdbool.h>

#include "ads1115.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "pipeline.h"

/* adc NULL: the ADS1115 was not found; the gauge shows FAULT. */
esp_err_t sampler_start(ads1115_t *adc, const afr_config_t *cfg, const cal_coeffs_t *stored,
                        bool supply_fitted, uint8_t log_rate_hz);
/* Applied at the start of the next 5 ms tick. */
void sampler_set_config(const afr_config_t *cfg, uint8_t log_rate_hz);
void sampler_reset_peaks(void);
/* The newest reading (non-blocking). */
bool sampler_latest(reading_t *out);
/* Bits are AFR_EVT_*. */
EventGroupHandle_t sampler_events(void);
void sampler_cal(cal_coeffs_t *cal, cal_source_t *src, bool *rejected);
