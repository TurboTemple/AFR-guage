#include "led_decoder.h"

#include <string.h>

static const char *const k_names[LED_COUNT] = {
  "UNKNOWN", "STEADY", "SLOW", "FAST", "VERY_SLOW", "OFF"
};

void led_decoder_init(led_decoder_t *d)
{
  memset(d, 0, sizeof(*d));
  d->pattern = LED_UNKNOWN;
}

static void track_max(led_decoder_t *d, uint32_t t_ms, float v)
{
  uint32_t sec = t_ms / 1000u;
  unsigned i = sec % LED_BUCKETS;
  if (d->bucket_sec[i] != sec) {
    d->bucket_sec[i] = sec;
    d->bucket_max[i] = v;
  } else if (v > d->bucket_max[i]) {
    d->bucket_max[i] = v;
  }
}

float led_decoder_threshold(const led_decoder_t *d, uint32_t t_ms)
{
  uint32_t sec = t_ms / 1000u;
  float max = 0.0f;
  for (unsigned i = 0; i < LED_BUCKETS; i++) {
    if (sec - d->bucket_sec[i] < LED_BUCKETS && d->bucket_max[i] > max) {
      max = d->bucket_max[i];
    }
  }
  float thr = max * 0.5f;
  return thr > LED_FLOOR_V ? thr : LED_FLOOR_V;
}

static bool in_range(uint32_t v, uint32_t lo, uint32_t hi)
{
  return v >= lo && v <= hi;
}

static void on_rise(led_decoder_t *d, uint32_t t_ms)
{
  led_pattern_t p = LED_UNKNOWN;
  if (d->have_rise) {
    uint32_t period = t_ms - d->last_rise_ms;
    if (in_range(period, LED_FAST_MIN_MS, LED_FAST_MAX_MS)) p = LED_FAST;
    else if (in_range(period, LED_SLOW_MIN_MS, LED_SLOW_MAX_MS)) p = LED_SLOW;
  }
  if (p == LED_UNKNOWN && d->have_fall) {
    uint32_t gap = t_ms - d->last_fall_ms;
    if (in_range(gap, LED_VSLOW_MIN_MS, LED_VSLOW_MAX_MS)) p = LED_VERY_SLOW;
    else if (in_range(gap, LED_SLOW_GAP_MIN_MS, LED_SLOW_GAP_MAX_MS)) p = LED_SLOW;
  }
  if (p != LED_UNKNOWN) d->pattern = p;
  d->have_rise = true;
  d->last_rise_ms = t_ms;
}

static void on_fall(led_decoder_t *d, uint32_t t_ms)
{
  /* No longer steady, but not yet known what it is. */
  if (d->pattern == LED_STEADY) d->pattern = LED_UNKNOWN;
  d->have_fall = true;
  d->last_fall_ms = t_ms;
}

led_pattern_t led_decoder_feed(led_decoder_t *d, uint32_t t_ms, float adc_v)
{
  track_max(d, t_ms, adc_v);
  float thr = led_decoder_threshold(d, t_ms);

  if (!d->started) {
    d->started = true;
    d->on = adc_v > thr;
    d->since_ms = t_ms;
  } else if (!d->on && adc_v > thr) {
    d->on = true;
    d->since_ms = t_ms;
    on_rise(d, t_ms);
  } else if (d->on && adc_v < thr * LED_OFF_HYST) {
    d->on = false;
    d->since_ms = t_ms;
    on_fall(d, t_ms);
  }

  uint32_t held = t_ms - d->since_ms;
  if (d->on && held >= LED_STEADY_MS) d->pattern = LED_STEADY;
  if (!d->on && held >= LED_OFF_MS) d->pattern = LED_OFF;
  return d->pattern;
}

const char *led_pattern_name(led_pattern_t p)
{
  return (unsigned)p < LED_COUNT ? k_names[p] : k_names[LED_UNKNOWN];
}

led_pattern_t led_pattern_parse(const char *s)
{
  for (int i = 0; i < LED_COUNT; i++) {
    if (strcmp(s, k_names[i]) == 0) return (led_pattern_t)i;
  }
  return LED_UNKNOWN;
}
