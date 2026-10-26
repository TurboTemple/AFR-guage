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

/* Alarms, in rising severity. ALARM_LEAN_LOAD is the dangerous one: lean
 * with the throttle open. */
typedef enum {
  ALARM_NONE,
  ALARM_RICH,       /* richer than the rich limit, or on the rich rail */
  ALARM_LEAN,       /* leaner than the part-throttle limit */
  ALARM_LEAN_LOAD,  /* leaner than the load limit, or lean rail, under load */
  ALARM_COUNT
} alarm_t;

typedef enum {
  FAULT_NONE,
  FAULT_I2C,           /* lambda ADC reads failed; clears itself */
  FAULT_ADC_NOT_FOUND, /* lambda ADC missing at boot; permanent */
  FAULT_AUX_NOT_FOUND, /* LED/TPS ADC missing at boot; permanent */
} fault_reason_t;

/* reading_t.flags */
enum {
  FLAG_CAL_NOMINAL    = 1u << 0, /* calibration source: nominal a, b */
  FLAG_CAL_STORED     = 1u << 1, /* calibration source: NVS */
  FLAG_CAL_FRESH      = 1u << 2, /* calibration source: this power-up */
  FLAG_CAL_REJECTED   = 1u << 3, /* last plateau fit was rejected */
  FLAG_RICH_RAIL_SEEN = 1u << 4, /* rich rail seen under load this session */
  FLAG_LEAN_RAIL_SEEN = 1u << 5, /* lean rail seen under load this session */
  FLAG_TPS_OK         = 1u << 6, /* throttle position is known */
  FLAG_TPS_FAULT      = 1u << 7, /* TPS learnt, but its voltage is implausible */
  FLAG_UNDER_LOAD     = 1u << 8, /* throttle at or above the load threshold */
  FLAG_EVENT_PENDING  = 1u << 9, /* an alarm event waits to be acknowledged */
};

/* The worst alarm since it was last acknowledged, kept on screen so a
 * driver who was looking at the track still sees it. */
typedef struct {
  uint8_t  type;        /* alarm_t */
  float    lambda;      /* worst lambda during it (end point on a rail) */
  float    tps;         /* throttle % when it began; NAN if unknown */
  uint32_t start_ms;
  uint32_t dur_ms;
} alarm_event_t;

typedef struct {
  uint32_t t_ms;            /* ms since boot */
  float    v_out;           /* Spartan output volts, calibrated */
  float    lambda;          /* NAN unless READY or a rail state */
  float    afr;             /* lambda * stoich of selected fuel */
  float    lambda_disp;     /* smoothed copy for the UI */
  gauge_state_t state;
  uint8_t  led_pattern;     /* decoded blue-wire pattern */
  float    supply_v;        /* Spartan 12 V; NAN if not fitted */
  uint16_t flags;           /* FLAG_* */
  uint8_t  alarm;           /* alarm_t, live */
  uint8_t  fault;           /* fault_reason_t, valid in ST_FAULT */
  uint32_t state_since_ms;  /* t_ms when the current state was entered */
  uint8_t  seq_progress;    /* 0-100 while in ST_SEQUENCER */
  float    peak_rich;       /* richest lambda under load this session; NAN if none */
  float    peak_lean;       /* leanest lambda under load this session; NAN if none */
  float    tps;             /* throttle %, NAN if unknown */
  float    tps_v;           /* TPS sensor volts (for learning); NAN if not read */
  alarm_event_t event;      /* valid with FLAG_EVENT_PENDING */
} reading_t;

#ifdef __cplusplus
}
#endif
