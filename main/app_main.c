/* Boot order and the supervisor loop.
 *
 * Boot: NVS and settings; BSP (display, touch, LVGL) and splash; AXP2101;
 * RTC; ADS1115 on I2C1; UART1; microSD and the session log; then the tasks.
 * When the Spartan powers up, its identity and end points are queried.
 *
 * The supervisor then handles what the tasks report: Spartan power-up and
 * query results, calibration results to save and log, settings changed on
 * screen (saved after a second of quiet), and the BOOT button (day/night). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "ads1115.h"
#include "afr_settings.h"
#include "logger.h"
#include "power.h"
#include "rtc_pcf85063.h"
#include "sampler.h"
#include "settings_store.h"
#include "spartan.h"
#include "spartan_proto.h"
#include "ui.h"

#if CONFIG_BSP_I2C_NUM == 1
#error "The ADS1115 owns I2C1: set CONFIG_BSP_I2C_NUM=0 (see sdkconfig.defaults)"
#endif

#ifdef CONFIG_AFR_SUPPLY_SENSE
#define SUPPLY_SENSE true
#else
#define SUPPLY_SENSE false
#endif

static const char *TAG = "app";

#define ADC_SDA          GPIO_NUM_17
#define ADC_SCL          GPIO_NUM_18
#define BOOT_BUTTON      GPIO_NUM_0
#define SAVE_DELAY_US    (1000 * 1000)
#define CORE_HOUSEKEEP   0

static afr_settings_t s_settings;
static lambda_range_t s_range;
static ads1115_t s_adc;
static bool s_rtc_valid;

/* Status shown on screen, written by several tasks. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static ui_status_t s_status;

/* Settings changed on screen: saved by the supervisor after a quiet second. */
static afr_settings_t s_pending;
static bool s_pending_dirty;
static int64_t s_pending_at;

static afr_config_t config_from(const afr_settings_t *s)
{
  afr_config_t c = {
    .range = s_range,
    .stoich = settings_stoich(s),
    .smooth_ms = s->smooth_ms,
    .lean = { s->warn_lean, s->warn_lean_ms },
    .rich = { s->warn_rich, s->warn_rich_ms },
  };
  return c;
}

static void apply_brightness(void)
{
  bsp_display_brightness_set(s_settings.night ? s_settings.bright_night : s_settings.bright_day);
}

static void meta_f(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void meta_f(const char *key, const char *fmt, ...)
{
  char v[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(v, sizeof v, fmt, ap);
  va_end(ap);
  logger_meta(key, v);
}

/* ------------------------------------------------------------- UI hooks */

static void ui_lock(void) { bsp_display_lock(-1); }
static void ui_unlock(void) { bsp_display_unlock(); }

static void ui_get_status(ui_status_t *out)
{
  taskENTER_CRITICAL(&s_mux);
  *out = s_status;
  taskEXIT_CRITICAL(&s_mux);
  out->log_state = (uint8_t)logger_state();
  power_status_t ps = power_status();
  out->battery = ps.battery;
  cal_coeffs_t c;
  cal_source_t src;
  bool rej;
  sampler_cal(&c, &src, &rej);
  out->cal_a = c.a;
  out->cal_b = c.b;
  out->cal_src = (uint8_t)src;
  out->cal_rejected = rej;
}

static void ui_settings_changed(const afr_settings_t *s)
{
  taskENTER_CRITICAL(&s_mux);
  s_pending = *s;
  s_pending_dirty = true;
  s_pending_at = esp_timer_get_time();
  taskEXIT_CRITICAL(&s_mux);
}

static void ui_set_brightness(int pct) { bsp_display_brightness_set(pct); }

static void ui_set_clock(const struct tm *tm)
{
  if (rtc_write(tm) != ESP_OK) return;
  struct tm t = *tm;
  struct timeval tv = { .tv_sec = mktime(&t) };
  settimeofday(&tv, NULL);
  s_rtc_valid = true;
  taskENTER_CRITICAL(&s_mux);
  s_status.rtc_valid = true;
  taskEXIT_CRITICAL(&s_mux);
  meta_f("clock_set", "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
         tm->tm_mday, tm->tm_hour, tm->tm_min);
}

static void sp_get_modes(void) { spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_GET_MODES }); }
static void sp_set_perf(int m) { spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_SET_PERF, .arg = m }); }
static void sp_set_slowheat(int m) { spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_SET_SLOWHEAT, .arg = m }); }
static void sp_set_linout(float v)
{
  meta_f("service", "SETLINOUT %.3f", (double)v);
  spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_SET_LINOUT, .value = v });
}

/* -------------------------------------------------------- Spartan results */

static void on_spartan(const spartan_result_t *res)
{
  if (res->type == SPARTAN_REQ_BOOT_INFO) {
    const spartan_info_t *i = &res->info;
    taskENTER_CRITICAL(&s_mux);
    s_status.spartan_seen = res->ok;
    strlcpy(s_status.spartan_fw, i->ok_fw ? i->fw : "?", sizeof s_status.spartan_fw);
    strlcpy(s_status.spartan_hw, i->ok_hw ? i->hw : "?", sizeof s_status.spartan_hw);
    s_status.sensor_type = i->ok_type ? i->type : -1;
    taskEXIT_CRITICAL(&s_mux);

    if (!res->ok) {
      logger_meta("spartan", "no UART reply; using default end points");
      return;
    }
    logger_meta("spartan_fw", s_status.spartan_fw);
    logger_meta("spartan_hw", s_status.spartan_hw);
    meta_f("sensor_type", "%d", s_status.sensor_type);
    if (i->ok_type && i->type != SP_TYPE_LSU49) {
      ESP_LOGW(TAG, "sensor type %d is not LSU 4.9", i->type);
      logger_meta("WARNING", "sensor type is not LSU 4.9");
    }
    if (i->ok_range) {
      s_range.lam0 = i->lam0;
      s_range.lam5 = i->lam5;
      afr_config_t c = config_from(&s_settings);
      sampler_set_config(&c, s_settings.log_rate_hz);
      ui_set_range(s_range);
    }
    meta_f("lambda_end_points", "%.3f,%.3f%s", (double)s_range.lam0, (double)s_range.lam5,
           i->ok_range ? "" : " (default; GETLAMZEROV/GETLAMFIVEV failed)");
  } else if (res->type == SPARTAN_REQ_GET_MODES || res->type == SPARTAN_REQ_SET_PERF ||
             res->type == SPARTAN_REQ_SET_SLOWHEAT) {
    taskENTER_CRITICAL(&s_mux);
    if (res->perf >= 0) s_status.spartan_perf = res->perf;
    if (res->slowheat >= 0) s_status.spartan_slowheat = res->slowheat;
    taskEXIT_CRITICAL(&s_mux);
  }
}

/* ------------------------------------------------------------ Power path */

static bool read_expander_pin(int pin)
{
  esp_io_expander_handle_t ex = bsp_io_expander_init();
  uint32_t level = 0;
  if (!ex || esp_io_expander_get_level(ex, 1u << pin, &level) != ESP_OK) return true;
  return level != 0;
}

static void on_vbus_lost(void) { logger_flush_now(); }

static void on_shutdown(void)
{
  logger_meta("power", "ignition off");
  logger_stop(800);
  bsp_sdcard_unmount();
}

/* ------------------------------------------------------------- Supervisor */

static void handle_events(EventBits_t ev)
{
  if (ev & AFR_EVT_SPARTAN_UP) {
    meta_f("spartan_up", "t_ms=%lu", (unsigned long)(esp_timer_get_time() / 1000));
    spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_BOOT_INFO });
    spartan_request(&(spartan_req_t){ .type = SPARTAN_REQ_GET_MODES });
  }
  if (ev & AFR_EVT_SPARTAN_DOWN) {
    meta_f("spartan_down", "t_ms=%lu", (unsigned long)(esp_timer_get_time() / 1000));
  }
  if (ev & (AFR_EVT_CAL_ACCEPTED | AFR_EVT_CAL_REJECTED)) {
    cal_coeffs_t c;
    cal_source_t src;
    bool rej;
    sampler_cal(&c, &src, &rej);
    if (ev & AFR_EVT_CAL_ACCEPTED) {
      time_t now = time(NULL);
      uint32_t stamp = s_rtc_valid ? (uint32_t)now : 0;
      settings_save_cal(c.a, c.b, stamp);
      taskENTER_CRITICAL(&s_mux);
      s_status.cal_time = stamp;
      taskEXIT_CRITICAL(&s_mux);
      meta_f("cal", "FRESH a=%.6e b=%.5f", (double)c.a, (double)c.b);
    } else {
      meta_f("cal", "CAL_REJECTED; keeping %s a=%.6e b=%.5f",
             src == CAL_SRC_STORED ? "STORED" : src == CAL_SRC_FRESH ? "FRESH" : "NOMINAL",
             (double)c.a, (double)c.b);
    }
  }
}

static void save_pending(void)
{
  afr_settings_t s;
  bool due = false;
  taskENTER_CRITICAL(&s_mux);
  if (s_pending_dirty) {
    s = s_pending;
    due = true;
    s_pending_dirty = false;
  }
  taskEXIT_CRITICAL(&s_mux);
  if (!due) return;

  bool rate_changed = s.log_rate_hz != s_settings.log_rate_hz;
  bool fuel_changed = s.fuel != s_settings.fuel || s.fuel_custom != s_settings.fuel_custom;
  /* Calibration is not edited on screen. */
  s.cal_valid = s_settings.cal_valid;
  s.cal_a = s_settings.cal_a;
  s.cal_b = s_settings.cal_b;
  s.cal_time = s_settings.cal_time;
  s_settings = s;
  afr_config_t c = config_from(&s_settings);
  sampler_set_config(&c, s_settings.log_rate_hz);
  if (rate_changed) meta_f("log_rate_hz", "%u", s_settings.log_rate_hz);
  if (fuel_changed) meta_f("fuel", "%s %.2f", conv_fuel_name((fuel_t)s_settings.fuel),
                           (double)settings_stoich(&s_settings));
  if (settings_save(&s_settings) != ESP_OK) ESP_LOGE(TAG, "settings save failed");
}

static void supervisor(void)
{
  gpio_config_t io = {
    .pin_bit_mask = 1ULL << BOOT_BUTTON,
    .mode = GPIO_MODE_INPUT, /* externally pulled up; never driven */
  };
  gpio_config(&io);
  int last_btn = 1;
  int64_t btn_at = 0;

  for (;;) {
    EventBits_t ev = xEventGroupWaitBits(sampler_events(), 0xFF, pdTRUE, pdFALSE,
                                         pdMS_TO_TICKS(50));
    if (ev) handle_events(ev);

    /* Settings from the screen, after a quiet second. */
    bool dirty;
    int64_t at;
    taskENTER_CRITICAL(&s_mux);
    dirty = s_pending_dirty;
    at = s_pending_at;
    taskEXIT_CRITICAL(&s_mux);
    if (dirty) {
      /* Apply at once so the gauge responds; persist when quiet. */
      afr_settings_t s;
      taskENTER_CRITICAL(&s_mux);
      s = s_pending;
      taskEXIT_CRITICAL(&s_mux);
      afr_config_t c = config_from(&s);
      sampler_set_config(&c, s.log_rate_hz);
      if (esp_timer_get_time() - at >= SAVE_DELAY_US) save_pending();
    }

    /* BOOT button: toggle day/night brightness (debounced, on press). */
    int b = gpio_get_level(BOOT_BUTTON);
    int64_t now = esp_timer_get_time();
    if (b != last_btn && now - btn_at > 50000) {
      btn_at = now;
      last_btn = b;
      if (b == 0) {
        s_settings.night = !s_settings.night;
        apply_brightness();
        ui_set_settings(&s_settings);
        settings_save(&s_settings);
      }
    }
  }
}

/* ------------------------------------------------------------------ Boot */

void app_main(void)
{
  const esp_app_desc_t *app = esp_app_get_description();
  ESP_LOGI(TAG, "AFR gauge %s", app->version);

  /* 1. NVS and settings. */
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
  settings_load(&s_settings);
  s_range = conv_range_default();

  /* 2. BSP: onboard I2C, display, touch, LVGL; splash. */
  bsp_display_start();
  apply_brightness();
  ui_hooks_t hooks = {
    .lock = ui_lock,
    .unlock = ui_unlock,
    .get_reading = sampler_latest,
    .get_status = ui_get_status,
    .reset_peaks = sampler_reset_peaks,
    .settings_changed = ui_settings_changed,
    .set_brightness = ui_set_brightness,
    .set_clock = ui_set_clock,
    .spartan_get_modes = sp_get_modes,
    .spartan_set_perf = sp_set_perf,
    .spartan_set_slowheat = sp_set_slowheat,
    .spartan_set_linout = sp_set_linout,
  };
  ui_splash(&hooks, app->version);

  /* 3. AXP2101: VBUS present; conservative charge current if a LiPo is fitted. */
  i2c_master_bus_handle_t onboard = bsp_i2c_get_handle();
  if (power_init(onboard) != ESP_OK) ESP_LOGE(TAG, "AXP2101 not responding");

  /* 4. RTC. */
  struct tm tm = { 0 };
  if (rtc_init(onboard) == ESP_OK && rtc_read(&tm, &s_rtc_valid) == ESP_OK && s_rtc_valid) {
    struct tm t = tm;
    struct timeval tv = { .tv_sec = mktime(&t) };
    settimeofday(&tv, NULL);
  } else {
    s_rtc_valid = false;
  }
  ESP_LOGI(TAG, "RTC %s", s_rtc_valid ? "valid" : "not set");

  /* 5. ADS1115 on I2C1. Missing: FAULT "ADC not found". */
  bool adc_ok = ads1115_init(&s_adc, I2C_NUM_1, ADC_SDA, ADC_SCL, ADS1115_ADDR) == ESP_OK;

  /* 6. UART1 at 9600 8N1. */
  if (spartan_uart_init() != ESP_OK) ESP_LOGE(TAG, "UART1 init failed");

  /* 7. microSD and the session file; without it, carry on without logging. */
  bool card = bsp_sdcard_mount() == ESP_OK;
  log_time_t lt = {
    .rtc_valid = s_rtc_valid, .year = tm.tm_year + 1900, .mon = tm.tm_mon + 1,
    .day = tm.tm_mday, .hour = tm.tm_hour, .min = tm.tm_min, .sec = tm.tm_sec,
  };
  logger_start(BSP_SD_MOUNT_POINT, card, &lt, CORE_HOUSEKEEP, 3);
  logger_meta("firmware", app->version);
  meta_f("fuel", "%s %.2f", conv_fuel_name((fuel_t)s_settings.fuel), (double)settings_stoich(&s_settings));
  meta_f("log_rate_hz", "%u", s_settings.log_rate_hz);
  if (s_settings.cal_valid) {
    meta_f("cal", "STORED a=%.6e b=%.5f", (double)s_settings.cal_a, (double)s_settings.cal_b);
  } else {
    meta_f("cal", "NOMINAL a=%.6e b=0", (double)CAL_A_NOMINAL);
  }
  meta_f("lambda_end_points", "%.3f,%.3f (default until the Spartan answers)",
         (double)s_range.lam0, (double)s_range.lam5);
  meta_f("warn_lean", "%.2f,%u", (double)s_settings.warn_lean, s_settings.warn_lean_ms);
  meta_f("warn_rich", "%.2f,%u", (double)s_settings.warn_rich, s_settings.warn_rich_ms);
  meta_f("smooth_ms", "%u", s_settings.smooth_ms);
  logger_meta("supply_sense", SUPPLY_SENSE ? "yes" : "no");
  logger_meta("rtc", s_rtc_valid ? "valid" : "not set");
  logger_columns();

  memset(&s_status, 0, sizeof s_status);
  strlcpy(s_status.gauge_fw, app->version, sizeof s_status.gauge_fw);
  strlcpy(s_status.log_name, logger_file_name(), sizeof s_status.log_name);
  s_status.sensor_type = s_status.spartan_perf = s_status.spartan_slowheat = -1;
  s_status.rtc_valid = s_rtc_valid;
  s_status.cal_time = s_settings.cal_time;

  /* 8. Tasks. */
  cal_coeffs_t stored = { s_settings.cal_a, s_settings.cal_b };
  afr_config_t cfg = config_from(&s_settings);
  ESP_ERROR_CHECK(sampler_start(adc_ok ? &s_adc : NULL, &cfg,
                                s_settings.cal_valid ? &stored : NULL,
                                SUPPLY_SENSE, s_settings.log_rate_hz));
  ESP_ERROR_CHECK(spartan_task_start(on_spartan, CORE_HOUSEKEEP, 10));
  power_hooks_t ph = {
    .read_expander_pin = read_expander_pin,
    .on_vbus_lost = on_vbus_lost,
    .on_shutdown = on_shutdown,
  };
  ESP_ERROR_CHECK(power_task_start(&ph, CORE_HOUSEKEEP, 10));
  ui_start(&hooks, &s_settings, s_range);

  /* 9. Spartan queries follow AFR_EVT_SPARTAN_UP in the supervisor. */
  supervisor();
}
