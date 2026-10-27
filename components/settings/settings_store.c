#include "settings_store.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";
static const char *NS = "afr";

/* NVS keys are at most 15 characters. */
#define K_SCHEMA     "schema"
#define K_FUEL       "fuel"
#define K_FUEL_CUST  "fuel_custom"
#define K_UNIT       "primary_unit"
#define K_SHOW_AFR   "show_afr"
#define K_RACE       "race"
#define K_LEAN       "lean"          /* lambda x 1000 */
#define K_LEAN_MS    "lean_ms"
#define K_RACE_LEAN  "race_lean"     /* lambda x 1000 */
#define K_RACE_MS    "race_lean_ms"
#define K_RACE_TGT   "race_target"   /* lambda x 1000 */
#define K_RICH       "rich"          /* lambda x 1000 */
#define K_RICH_MS    "rich_ms"
#define K_SMOOTH     "smooth_ms"
#define K_SP_PERF    "spartan_perf"
#define K_BR_DAY     "bright_day"
#define K_BR_NIGHT   "bright_night"
#define K_NIGHT      "night"
#define K_CAL_A      "cal_a"         /* float bits */
#define K_CAL_B      "cal_b"
#define K_CAL_TIME   "cal_time"

static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint16_t milli(float v) { return (uint16_t)lroundf(v * 1000.0f); }

static void get_u8(nvs_handle_t h, const char *k, uint8_t *v) { nvs_get_u8(h, k, v); }
static void get_u16(nvs_handle_t h, const char *k, uint16_t *v) { nvs_get_u16(h, k, v); }
static void get_bool(nvs_handle_t h, const char *k, bool *v)
{
  uint8_t u;
  if (nvs_get_u8(h, k, &u) == ESP_OK) *v = u != 0;
}
static void get_milli(nvs_handle_t h, const char *k, float *v)
{
  uint16_t u;
  if (nvs_get_u16(h, k, &u) == ESP_OK) *v = (float)u / 1000.0f;
}

esp_err_t settings_load(afr_settings_t *s)
{
  settings_defaults(s);
  nvs_handle_t h;
  esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "no settings yet: defaults");
    return err;
  }
  uint8_t schema = 0;
  if (nvs_get_u8(h, K_SCHEMA, &schema) != ESP_OK || schema != SETTINGS_SCHEMA) {
    ESP_LOGW(TAG, "schema %u, want %u: defaults", schema, SETTINGS_SCHEMA);
    nvs_close(h);
    return ESP_ERR_INVALID_VERSION;
  }
  uint32_t u;
  get_u8(h, K_FUEL, &s->fuel);
  if (nvs_get_u32(h, K_FUEL_CUST, &u) == ESP_OK) s->fuel_custom = bitsf(u);
  get_u8(h, K_UNIT, &s->primary_unit);
  get_bool(h, K_SHOW_AFR, &s->show_afr);
  get_bool(h, K_RACE, &s->race);
  get_milli(h, K_LEAN, &s->lean);
  get_u16(h, K_LEAN_MS, &s->lean_ms);
  get_milli(h, K_RACE_LEAN, &s->race_lean);
  get_u16(h, K_RACE_MS, &s->race_lean_ms);
  get_milli(h, K_RACE_TGT, &s->race_target);
  get_milli(h, K_RICH, &s->rich);
  get_u16(h, K_RICH_MS, &s->rich_ms);
  get_u16(h, K_SMOOTH, &s->smooth_ms);
  get_u8(h, K_SP_PERF, &s->spartan_perf);
  get_u8(h, K_BR_DAY, &s->bright_day);
  get_u8(h, K_BR_NIGHT, &s->bright_night);
  get_bool(h, K_NIGHT, &s->night);
  uint32_t a, b;
  if (nvs_get_u32(h, K_CAL_A, &a) == ESP_OK && nvs_get_u32(h, K_CAL_B, &b) == ESP_OK) {
    s->cal_a = bitsf(a);
    s->cal_b = bitsf(b);
    s->cal_valid = true;
    nvs_get_u32(h, K_CAL_TIME, &s->cal_time);
  }
  nvs_close(h);
  if (settings_validate(s)) ESP_LOGW(TAG, "stored settings out of range: clamped");
  return ESP_OK;
}

esp_err_t settings_save(const afr_settings_t *s)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  nvs_set_u8(h, K_SCHEMA, SETTINGS_SCHEMA);
  nvs_set_u8(h, K_FUEL, s->fuel);
  nvs_set_u32(h, K_FUEL_CUST, fbits(s->fuel_custom));
  nvs_set_u8(h, K_UNIT, s->primary_unit);
  nvs_set_u8(h, K_SHOW_AFR, s->show_afr);
  nvs_set_u8(h, K_RACE, s->race);
  nvs_set_u16(h, K_LEAN, milli(s->lean));
  nvs_set_u16(h, K_LEAN_MS, s->lean_ms);
  nvs_set_u16(h, K_RACE_LEAN, milli(s->race_lean));
  nvs_set_u16(h, K_RACE_MS, s->race_lean_ms);
  nvs_set_u16(h, K_RACE_TGT, milli(s->race_target));
  nvs_set_u16(h, K_RICH, milli(s->rich));
  nvs_set_u16(h, K_RICH_MS, s->rich_ms);
  nvs_set_u16(h, K_SMOOTH, s->smooth_ms);
  nvs_set_u8(h, K_SP_PERF, s->spartan_perf);
  nvs_set_u8(h, K_BR_DAY, s->bright_day);
  nvs_set_u8(h, K_BR_NIGHT, s->bright_night);
  nvs_set_u8(h, K_NIGHT, s->night);
  err = nvs_commit(h);
  nvs_close(h);
  return err;
}

esp_err_t settings_save_cal(float a, float b, uint32_t unix_time)
{
  nvs_handle_t h;
  esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  nvs_set_u8(h, K_SCHEMA, SETTINGS_SCHEMA);
  nvs_set_u32(h, K_CAL_A, fbits(a));
  nvs_set_u32(h, K_CAL_B, fbits(b));
  nvs_set_u32(h, K_CAL_TIME, unix_time);
  err = nvs_commit(h);
  nvs_close(h);
  return err;
}
