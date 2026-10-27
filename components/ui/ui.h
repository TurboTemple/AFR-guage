/* LVGL 9 screens: main gauge, strip chart, settings, service and clock.
 *
 * The UI never blocks: it reads the newest reading through a hook (a peek of
 * the sampler's one-slot queue) and sends every change back through hooks. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "afr_settings.h"
#include "afr_types.h"
#include "conversion.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t  log_state;      /* log_state_t */
  char     log_name[32];
  char     gauge_fw[32];
  bool     spartan_seen;   /* boot queries answered */
  char     spartan_fw[24];
  char     spartan_hw[24];
  int      sensor_type;    /* -1 unknown */
  int      spartan_perf;   /* -1 unknown */
  int      spartan_slowheat;
  float    cal_a, cal_b;
  uint8_t  cal_src;        /* cal_source_t */
  bool     cal_rejected;
  uint32_t cal_time;
  bool     rtc_valid;
  bool     battery;
  uint32_t raw_conversions; /* lambda ADC conversions read */
  uint32_t missed;          /* conversions not read in time */
  uint32_t rdy_timeouts;    /* gaps in the conversion-ready pulses */
  uint32_t spikes;          /* blocks with a spike rejected */
  uint32_t aux_errors;      /* failed LED/supply reads */
} ui_status_t;

typedef struct {
  /* LVGL lock, for calls into the UI from other tasks. */
  void (*lock)(void);
  void (*unlock)(void);
  bool (*get_reading)(reading_t *out);
  void (*get_status)(ui_status_t *out);
  void (*reset_peaks)(void);
  void (*settings_changed)(const afr_settings_t *s);
  void (*set_brightness)(int percent);
  void (*set_clock)(const struct tm *tm);
  void (*spartan_get_modes)(void);
  void (*spartan_set_perf)(int mode);
  void (*spartan_set_slowheat)(int mode);
  void (*spartan_set_linout)(float volts);
} ui_hooks_t;

/* Splash screen while the rest boots. Takes the lock. */
void ui_splash(const ui_hooks_t *hooks, const char *version);
/* Build every screen and show the main one. Takes the lock. */
void ui_start(const ui_hooks_t *hooks, const afr_settings_t *s, lambda_range_t range);
/* Settings changed outside the UI (BOOT button). Takes the lock. */
void ui_set_settings(const afr_settings_t *s);
/* The Spartan reported its end points. Takes the lock. */
void ui_set_range(lambda_range_t range);

#ifdef __cplusplus
}
#endif
