/* microSD session logging.
 *
 * One CSV file per power-up under <mount>/logs. Rows are buffered in RAM,
 * written in 4 KB chunks and fsync'd once a second, so switching the
 * ignition off loses at most the last second. Earlier parts of a file are
 * never rewritten. At boot the previous session's file is cut back to its
 * final newline. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOGGER_QUEUE_LEN 256

typedef enum {
  LOG_STATE_OFF,      /* not started */
  LOG_STATE_OK,
  LOG_STATE_NO_CARD,  /* card missing or failed to mount */
  LOG_STATE_ERROR,    /* write failed, card full */
} log_state_t;

typedef struct {
  reading_t r;
  uint32_t  dropped;  /* rows lost to queue overflow since the previous row */
} log_row_t;

typedef struct {
  bool rtc_valid;
  int  year, mon, day, hour, min, sec;
} log_time_t;

/* Open the session file and start the logger task. card_ok false starts in
 * LOG_STATE_NO_CARD (the gauge runs on without logging). */
esp_err_t logger_start(const char *mount_point, bool card_ok, const log_time_t *now,
                       int core, int prio);
/* The 256-slot row queue; the sampler sends with zero wait. */
QueueHandle_t logger_queue(void);
/* Append a '#' header line ("key=value"). Safe from any task. */
void logger_meta(const char *key, const char *value);
/* Write the CSV column line; call after the start-up header lines. */
void logger_columns(void);
log_state_t logger_state(void);
/* File name of this session ("" if none). */
const char *logger_file_name(void);
/* Write and fsync on the next drain (within 100 ms). */
void logger_flush_now(void);
/* Flush, fsync and close. Blocks up to timeout_ms. */
void logger_stop(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
