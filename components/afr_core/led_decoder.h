/* Blue-wire blink classifier.
 *
 * Fed with AIN1 volts (at the ADC pin) at 50 Hz. The lit voltage is not
 * documented, so the on/off threshold is half the highest level seen in the
 * last 10 s, with a 0.5 V floor. Patterns are classified from edge timing. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_FLOOR_V          0.5f
#define LED_OFF_HYST         0.8f    /* off below 80 % of the on threshold */
#define LED_MAX_WINDOW_MS    10000u
#define LED_STEADY_MS        1500u   /* on continuously this long: STEADY */
#define LED_OFF_MS           10000u  /* off continuously this long: OFF */
#define LED_SLOW_MIN_MS      1500u   /* SLOW: pulse period 1.5-2.5 s */
#define LED_SLOW_MAX_MS      2500u
#define LED_FAST_MIN_MS      300u    /* FAST: pulse period 0.3-0.7 s */
#define LED_FAST_MAX_MS      700u
#define LED_VSLOW_MIN_MS     6000u   /* VERY_SLOW: gap between pulses 6-10 s */
#define LED_VSLOW_MAX_MS     10000u
/* A first pulse after a gap this long, but shorter than a VERY_SLOW gap,
 * is SLOW. FAST gaps are under 0.7 s. This lets a STEADY -> SLOW change be
 * shown on the first pulse rather than the second, inside the 3 s
 * acceptance limit. */
#define LED_SLOW_GAP_MIN_MS  750u
#define LED_SLOW_GAP_MAX_MS  2500u

#define LED_BUCKETS          10u     /* 1 s maximum buckets over 10 s */

typedef struct {
  float    bucket_max[LED_BUCKETS];
  uint32_t bucket_sec[LED_BUCKETS];
  bool     on;
  bool     have_rise;
  bool     have_fall;
  uint32_t last_rise_ms;
  uint32_t last_fall_ms;
  uint32_t since_ms;      /* when the current on/off level began */
  led_pattern_t pattern;
  bool     started;
} led_decoder_t;

void led_decoder_init(led_decoder_t *d);
/* Feed one sample. Returns the current pattern. */
led_pattern_t led_decoder_feed(led_decoder_t *d, uint32_t t_ms, float adc_v);
float led_decoder_threshold(const led_decoder_t *d, uint32_t t_ms);

const char *led_pattern_name(led_pattern_t p);
/* Parse a name written by led_pattern_name; LED_UNKNOWN if not recognised. */
led_pattern_t led_pattern_parse(const char *s);

#ifdef __cplusplus
}
#endif
