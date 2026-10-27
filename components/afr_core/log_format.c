#include "log_format.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gauge_state.h"
#include "alarms.h"
#include "led_decoder.h"

size_t log_fmt_fixed(char *buf, size_t n, float v, int decimals)
{
  if (n == 0) return 0;
  if (isnan(v) || isinf(v)) {
    buf[0] = '\0';
    return 0;
  }
  static const uint32_t k_pow[] = { 1, 10, 100, 1000, 10000 };
  if (decimals < 0) decimals = 0;
  if (decimals > 4) decimals = 4;
  uint32_t scale = k_pow[decimals];
  bool neg = v < 0.0f;
  float a = neg ? -v : v;
  if (a > 1e5f) a = 1e5f;
  uint32_t q = (uint32_t)(a * (float)scale + 0.5f);
  uint32_t ip = q / scale, fp = q % scale;
  int len;
  if (decimals == 0) {
    len = snprintf(buf, n, "%s%lu", neg && q ? "-" : "", (unsigned long)ip);
  } else {
    len = snprintf(buf, n, "%s%lu.%0*lu", neg && q ? "-" : "", (unsigned long)ip,
                   decimals, (unsigned long)fp);
  }
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

size_t log_format_row(char *buf, size_t n, const reading_t *r, uint32_t dropped)
{
  char lam[16], afr[16], v[16], sup[16];
  bool has = gs_has_lambda(r->state);
  if (has) {
    log_fmt_fixed(lam, sizeof lam, r->lambda, 4);
    log_fmt_fixed(afr, sizeof afr, r->afr, 2);
  } else {
    lam[0] = afr[0] = '\0';
  }
  log_fmt_fixed(v, sizeof v, r->v_out, 4);
  log_fmt_fixed(sup, sizeof sup, r->supply_v, 2);
  int len = snprintf(buf, n, "%lu,%s,%s,%s,%s,%s,%s,%c,%c,%lu\n",
                     (unsigned long)r->t_ms, gs_state_name(r->state), lam, afr, v,
                     led_pattern_name((led_pattern_t)r->led_pattern), sup,
                     alarm_char((alarm_t)r->alarm), (r->flags & FLAG_RACE) ? 'R' : 'S',
                     (unsigned long)dropped);
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

size_t log_format_meta(char *buf, size_t n, const char *key, const char *value)
{
  int len = snprintf(buf, n, "# %s=%s\n", key, value);
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

void log_make_name(char *buf, size_t n, bool rtc_valid, int year, int mon, int day,
                   int hour, int min, int sec, uint32_t seq)
{
  if (rtc_valid) {
    snprintf(buf, n, "%04d%02d%02d_%02d%02d%02d.csv", year, mon, day, hour, min, sec);
  } else {
    snprintf(buf, n, "S%05lu.csv", (unsigned long)(seq % 100000u));
  }
}

uint32_t log_parse_seq(const char *name)
{
  if (name[0] != 'S' && name[0] != 's') return 0;
  uint32_t v = 0;
  int i = 1;
  for (; i <= 5; i++) {
    if (name[i] < '0' || name[i] > '9') return 0;
    v = v * 10u + (uint32_t)(name[i] - '0');
  }
  return strcmp(name + i, ".csv") == 0 || strcmp(name + i, ".CSV") == 0 ? v : 0;
}

size_t log_partial_tail(const char *tail, size_t len)
{
  if (len == 0 || tail[len - 1] == '\n') return 0;
  for (size_t i = len; i > 0; i--) {
    if (tail[i - 1] == '\n') return len - i;
  }
  /* No newline at all: rows are far shorter than a chunk, so the whole
   * chunk is one partial line (or garbage). */
  return len;
}
