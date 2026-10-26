/* NVS load/save of afr_settings_t, with a schema version: an unknown schema
 * loads the defaults. */
#pragma once

#include "afr_settings.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* nvs_flash_init() must have run. Always fills s (defaults on any error). */
esp_err_t settings_load(afr_settings_t *s);
esp_err_t settings_save(const afr_settings_t *s);
/* Just the calibration keys: written by the calibration path. */
esp_err_t settings_save_cal(float a, float b, uint32_t unix_time);

#ifdef __cplusplus
}
#endif
