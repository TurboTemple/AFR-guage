#include "spartan.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "conversion.h"
#include "spartan_proto.h"

static const char *TAG = "spartan";

#define SP_UART     UART_NUM_1
#define SP_TX_GPIO  16
#define SP_RX_GPIO  44

#if CONFIG_AFR_SPARTAN_EOL_CR
#define SP_EOL "\r"
#elif CONFIG_AFR_SPARTAN_EOL_LF
#define SP_EOL "\n"
#else
#define SP_EOL "\r\n"
#endif

static QueueHandle_t s_queue;
static spartan_result_cb_t s_cb;

esp_err_t spartan_uart_init(void)
{
  uart_config_t cfg = {
    .baud_rate = SPARTAN_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };
  ESP_RETURN_ON_ERROR(uart_driver_install(SP_UART, 512, 0, 0, NULL, 0), TAG, "install");
  ESP_RETURN_ON_ERROR(uart_param_config(SP_UART, &cfg), TAG, "config");
  return uart_set_pin(SP_UART, SP_TX_GPIO, SP_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static bool has_digit(const char *s)
{
  for (; *s; s++) {
    if (isdigit((unsigned char)*s)) return true;
  }
  return false;
}

/* Read lines until one is accepted or 200 ms pass. */
static bool read_reply(const char *cmd, char *reply, size_t n, bool want_number)
{
  char line[64];
  size_t len = 0;
  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(SPARTAN_TIMEOUT_MS);
  for (;;) {
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0) break;
    uint8_t c;
    if (uart_read_bytes(SP_UART, &c, 1, deadline - now) != 1) break;
    if (c != '\n' && c != '\r') {
      if (len + 1 < sizeof line) line[len++] = (char)c;
      continue;
    }
    line[len] = '\0';
    len = 0;
    char clean[64];
    spartan_clean_reply(line, clean, sizeof clean);
    if (!clean[0] || strcasecmp(clean, cmd) == 0) continue; /* blank or echo */
    if (want_number && !has_digit(clean)) continue;
    strlcpy(reply, clean, n);
    return true;
  }
  /* A final line without a newline still counts. */
  if (len) {
    line[len] = '\0';
    spartan_clean_reply(line, reply, n);
    return reply[0] && strcasecmp(reply, cmd) != 0 && (!want_number || has_digit(reply));
  }
  return false;
}

bool spartan_command(const char *cmd, char *reply, size_t n, bool want_number)
{
  if (spartan_cmd_forbidden(cmd)) {
    ESP_LOGE(TAG, "refusing to send %s", cmd);
    return false;
  }
  for (int i = 0; i < SPARTAN_TRIES; i++) {
    uart_flush_input(SP_UART);
    uart_write_bytes(SP_UART, cmd, strlen(cmd));
    uart_write_bytes(SP_UART, SP_EOL, strlen(SP_EOL));
    if (read_reply(cmd, reply, n, want_number)) {
      ESP_LOGI(TAG, "%s -> %s", cmd, reply);
      return true;
    }
  }
  ESP_LOGW(TAG, "%s: no reply", cmd);
  return false;
}

bool spartan_get_number(const char *cmd, float *out)
{
  char r[48];
  return spartan_command(cmd, r, sizeof r, true) && spartan_parse_number(r, out);
}

void spartan_read_info(spartan_info_t *info)
{
  memset(info, 0, sizeof(*info));
  info->type = -1;
  info->ok_fw = spartan_command(SP_GETFW, info->fw, sizeof info->fw, false);
  info->ok_hw = spartan_command(SP_GETHW, info->hw, sizeof info->hw, false);
  float v;
  if (spartan_get_number(SP_GETTYPE, &v)) {
    info->type = (int)v;
    info->ok_type = true;
  }
  float l0, l5;
  if (spartan_get_number(SP_GETLAMZEROV, &l0) && spartan_get_number(SP_GETLAMFIVEV, &l5)) {
    info->lam0 = l0;
    info->lam5 = l5;
    info->ok_range = conv_range_valid(l0, l5);
    if (!info->ok_range) ESP_LOGW(TAG, "end points %.3f/%.3f out of range", (double)l0, (double)l5);
  }
}

static void run(const spartan_req_t *req)
{
  spartan_result_t res = { .type = req->type, .perf = -1, .slowheat = -1 };
  char cmd[24], reply[48];
  float v;
  switch (req->type) {
  case SPARTAN_REQ_BOOT_INFO:
    spartan_read_info(&res.info);
    res.ok = res.info.ok_fw || res.info.ok_hw || res.info.ok_type || res.info.ok_range;
    break;
  case SPARTAN_REQ_GET_MODES:
    if (spartan_get_number(SP_GETPERF, &v)) res.perf = (int)v;
    if (spartan_get_number(SP_GETSLOWHEAT, &v)) res.slowheat = (int)v;
    res.ok = res.perf >= 0 || res.slowheat >= 0;
    break;
  case SPARTAN_REQ_SET_PERF:
    res.ok = spartan_fmt_setperf(cmd, sizeof cmd, req->arg) &&
             spartan_command(cmd, reply, sizeof reply, false);
    if (spartan_get_number(SP_GETPERF, &v)) res.perf = (int)v;
    break;
  case SPARTAN_REQ_SET_SLOWHEAT:
    res.ok = spartan_fmt_setslowheat(cmd, sizeof cmd, req->arg) &&
             spartan_command(cmd, reply, sizeof reply, false);
    if (spartan_get_number(SP_GETSLOWHEAT, &v)) res.slowheat = (int)v;
    break;
  case SPARTAN_REQ_SET_LINOUT:
    res.ok = spartan_fmt_setlinout(cmd, sizeof cmd, req->value) &&
             spartan_command(cmd, reply, sizeof reply, false);
    break;
  }
  if (s_cb) s_cb(&res);
}

static void task(void *arg)
{
  (void)arg;
  spartan_req_t req;
  for (;;) {
    if (xQueueReceive(s_queue, &req, portMAX_DELAY) == pdTRUE) run(&req);
  }
}

esp_err_t spartan_task_start(spartan_result_cb_t cb, int core, int prio)
{
  s_cb = cb;
  s_queue = xQueueCreate(8, sizeof(spartan_req_t));
  if (!s_queue) return ESP_ERR_NO_MEM;
  return xTaskCreatePinnedToCore(task, "spartan", 4096, NULL, prio, NULL, core) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}

bool spartan_request(const spartan_req_t *req)
{
  return s_queue && xQueueSend(s_queue, req, 0) == pdTRUE;
}
