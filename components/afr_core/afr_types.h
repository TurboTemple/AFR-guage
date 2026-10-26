/* Core data types shared by every part of the gauge.
 *
 * afr_core is pure C11: no ESP-IDF headers, so it builds and is unit-tested
 * on a PC (test/host) as well as on the ESP32-S3. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ST_BOOT, ST_NO_SPARTAN, ST_SEQUENCER, ST_WAIT_EXHAUST, ST_WARMING,
  ST_READY, ST_RICH_RAIL, ST_LEAN_RAIL, ST_TOO_HOT, ST_FAULT,
  ST_COUNT
} gauge_state_t;

/* Blue-wire blink patterns, decoded by led_decoder. */
typedef enum {
  LED_UNKNOWN,   /* not classified yet: keep the previous gauge state */
  LED_STEADY,    /* at operating temperature */
  LED_SLOW,      /* heating up, or cold */
  LED_FAST,      /* too hot */
  LED_VERY_SLOW, /* waiting for exhaust heat (smart heat-up) */
  LED_OFF,       /* Spartan unpowered, or LED circuit open */
  LED_COUNT
} led_pattern_t;

typedef enum { WARN_NONE, WARN_LEAN, WARN_RICH } warn_t;

typedef enum {
  FAULT_NONE,
  FAULT_I2C,           /* three ADS1115 reads failed in a row; clears itself */
  FAULT_ADC_NOT_FOUND, /* ADS1115 missing at boot; permanent */
} fault_reason_t;

/* reading_t.flags */
enum {
  FLAG_CAL_NOMINAL  = 1u << 0, /* calibration source: nominal a, b */
  FLAG_CAL_STORED   = 1u << 1, /* calibration source: NVS */
  FLAG_CAL_FRESH    = 1u << 2, /* calibration source: this power-up */
  FLAG_CAL_REJECTED = 1u << 3, /* last plateau fit was rejected */
  FLAG_RICH_RAIL_SEEN = 1u << 4, /* rich rail seen this session */
  FLAG_LEAN_RAIL_SEEN = 1u << 5, /* lean rail seen this session */
};

typedef struct {
  uint32_t t_ms;            /* ms since boot */
  float    v_out;           /* Spartan output volts, calibrated */
  float    lambda;          /* NAN unless READY or a rail state */
  float    afr;             /* lambda * stoich of selected fuel */
  float    lambda_disp;     /* smoothed copy for the UI */
  gauge_state_t state;
  uint8_t  led_pattern;     /* decoded blue-wire pattern */
  float    supply_v;        /* Spartan 12 V via AIN2; NAN if not fitted */
  uint16_t flags;           /* CAL_NOMINAL | CAL_STORED | CAL_FRESH | ... */
  /* Additions to the spec's type, needed by the UI and logger. */
  uint8_t  warn;            /* warn_t */
  uint8_t  fault;           /* fault_reason_t, valid in ST_FAULT */
  uint32_t state_since_ms;  /* t_ms when the current state was entered */
  uint8_t  seq_progress;    /* 0-100 while in ST_SEQUENCER */
  float    peak_rich;       /* richest lambda this session; NAN if none */
  float    peak_lean;       /* leanest lambda this session; NAN if none */
} reading_t;

#ifdef __cplusplus
}
#endif
