#include "sampler.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "logger.h"

static const char *TAG = "sampler";

#define SAMPLER_CORE 1
#define SAMPLER_PRIO 20

static ads1115_t *s_adc;
static afr_pipeline_t s_p;
static TaskHandle_t s_task;
static QueueHandle_t s_ui_q;
static EventGroupHandle_t s_events;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static afr_config_t s_pending_cfg;
static bool s_cfg_dirty;
static bool s_reset_peaks;
static uint8_t s_log_rate;
static cal_coeffs_t s_cal_copy;
static cal_source_t s_cal_src_copy;
static bool s_cal_rej_copy;

static void timer_cb(void *arg)
{
  (void)arg;
  xTaskNotifyGive(s_task);
}

static void task(void *arg)
{
  (void)arg;
  esp_task_wdt_add(NULL);
  QueueHandle_t log_q = logger_queue();
  uint32_t tick = 0, dropped = 0;
  reading_t r;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    esp_task_wdt_reset();

    uint8_t rate;
    bool reset;
    afr_config_t cfg;
    bool dirty;
    taskENTER_CRITICAL(&s_mux);
    dirty = s_cfg_dirty;
    if (dirty) cfg = s_pending_cfg;
    s_cfg_dirty = false;
    reset = s_reset_peaks;
    s_reset_peaks = false;
    rate = s_log_rate;
    taskEXIT_CRITICAL(&s_mux);
    if (dirty) afr_pipeline_set_config(&s_p, &cfg);
    if (reset) afr_pipeline_reset_peaks(&s_p);

    afr_raw_t in = { .t_ms = (uint32_t)(esp_timer_get_time() / 1000) };
    if (s_adc) {
      in.lam_ok = ads1115_read(s_adc, 0, &in.lam_code) == ESP_OK;
      if (tick % 4 == 0) {
        in.led_new = ads1115_read(s_adc, 1, &in.led_code) == ESP_OK;
      } else if (s_p.supply_fitted && tick % 20 == 10) {
        in.supply_new = ads1115_read(s_adc, 2, &in.supply_code) == ESP_OK;
      }
    }
    afr_pipeline_step_raw(&s_p, &in, &r);

    xQueueOverwrite(s_ui_q, &r);

    uint32_t decim = 1000u / AFR_SAMPLE_MS / (rate ? rate : 20);
    if (log_q && tick % decim == 0) {
      log_row_t row = { .r = r, .dropped = dropped };
      if (xQueueSend(log_q, &row, 0) == pdTRUE) dropped = 0;
      else dropped++;
    }

    uint8_t ev = afr_pipeline_take_events(&s_p);
    if (ev) {
      taskENTER_CRITICAL(&s_mux);
      s_cal_copy = s_p.cal;
      s_cal_src_copy = s_p.cal_src;
      s_cal_rej_copy = s_p.cal_rejected;
      taskEXIT_CRITICAL(&s_mux);
      xEventGroupSetBits(s_events, ev);
    }
    tick++;
  }
}

esp_err_t sampler_start(ads1115_t *adc, const afr_config_t *cfg, const cal_coeffs_t *stored,
                        bool supply_fitted, uint8_t log_rate_hz)
{
  s_adc = adc;
  s_log_rate = log_rate_hz;
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  afr_pipeline_init(&s_p, cfg, stored, supply_fitted, AFR_SAMPLE_MS, now);
  s_cal_copy = s_p.cal;
  s_cal_src_copy = s_p.cal_src;
  if (!adc) afr_pipeline_fault(&s_p, FAULT_ADC_NOT_FOUND, now);
  afr_pipeline_start(&s_p, now);

  s_ui_q = xQueueCreate(1, sizeof(reading_t));
  s_events = xEventGroupCreate();
  if (!s_ui_q || !s_events) return ESP_ERR_NO_MEM;

  if (xTaskCreatePinnedToCore(task, "sampler", 4096, NULL, SAMPLER_PRIO, &s_task,
                              SAMPLER_CORE) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  const esp_timer_create_args_t ta = { .callback = timer_cb, .name = "sampler" };
  esp_timer_handle_t timer;
  ESP_ERROR_CHECK(esp_timer_create(&ta, &timer));
  ESP_LOGI(TAG, "%s, supply sense %s", adc ? "running at 200 Hz" : "no ADC", supply_fitted ? "on" : "off");
  return esp_timer_start_periodic(timer, AFR_SAMPLE_MS * 1000);
}

void sampler_set_config(const afr_config_t *cfg, uint8_t log_rate_hz)
{
  taskENTER_CRITICAL(&s_mux);
  s_pending_cfg = *cfg;
  s_cfg_dirty = true;
  s_log_rate = log_rate_hz;
  taskEXIT_CRITICAL(&s_mux);
}

void sampler_reset_peaks(void)
{
  taskENTER_CRITICAL(&s_mux);
  s_reset_peaks = true;
  taskEXIT_CRITICAL(&s_mux);
}

bool sampler_latest(reading_t *out)
{
  return s_ui_q && xQueuePeek(s_ui_q, out, 0) == pdTRUE;
}

EventGroupHandle_t sampler_events(void)
{
  return s_events;
}

void sampler_cal(cal_coeffs_t *cal, cal_source_t *src, bool *rejected)
{
  taskENTER_CRITICAL(&s_mux);
  *cal = s_cal_copy;
  *src = s_cal_src_copy;
  *rejected = s_cal_rej_copy;
  taskEXIT_CRITICAL(&s_mux);
}
