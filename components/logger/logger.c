#include "logger.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "log_format.h"

static const char *TAG = "logger";

#define CHUNK        4096
#define SYNC_MS      1000
#define DRAIN_MS     100
#define META_LEN     96
#define NVS_NS       "logger"
#define NVS_LAST     "last"

typedef struct {
  char line[META_LEN];
} meta_t;

static QueueHandle_t s_rows;
static QueueHandle_t s_meta;
static SemaphoreHandle_t s_stopped;
static volatile bool s_stop;
static volatile bool s_flush_now;
static volatile log_state_t s_state = LOG_STATE_OFF;
static FILE *s_file;
static char s_dir[64];
static char s_name[32];
static char s_buf[CHUNK + LOG_ROW_MAX + META_LEN];
static size_t s_len;

QueueHandle_t logger_queue(void) { return s_rows; }
log_state_t logger_state(void) { return s_state; }
const char *logger_file_name(void) { return s_name; }

static void fail(const char *what)
{
  ESP_LOGE(TAG, "%s failed (errno %d): logging stopped", what, errno);
  if (s_file) fclose(s_file);
  s_file = NULL;
  s_state = LOG_STATE_ERROR;
}

static bool write_out(size_t n)
{
  if (!s_file || n == 0) return s_file != NULL;
  if (fwrite(s_buf, 1, n, s_file) != n) {
    fail("write");
    return false;
  }
  memmove(s_buf, s_buf + n, s_len - n);
  s_len -= n;
  return true;
}

static void sync_file(void)
{
  if (!s_file) return;
  if (!write_out(s_len)) return;
  if (fflush(s_file) != 0 || fsync(fileno(s_file)) != 0) fail("fsync");
}

static void append(const char *s, size_t n)
{
  if (!s_file) return;
  memcpy(s_buf + s_len, s, n);
  s_len += n;
  if (s_len >= CHUNK) write_out(CHUNK);
}

/* Cut the previous session's file back to its final newline. */
static void repair_last(void)
{
  nvs_handle_t h;
  char last[32] = "";
  size_t n = sizeof last;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
  esp_err_t err = nvs_get_str(h, NVS_LAST, last, &n);
  nvs_close(h);
  if (err != ESP_OK || !last[0]) return;

  char path[128];
  snprintf(path, sizeof path, "%s/%s", s_dir, last);
  FILE *f = fopen(path, "r+");
  if (!f) return;
  static char tail[CHUNK];
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  long take = size < (long)sizeof tail ? size : (long)sizeof tail;
  if (take > 0 && fseek(f, size - take, SEEK_SET) == 0 &&
      fread(tail, 1, (size_t)take, f) == (size_t)take) {
    size_t cut = log_partial_tail(tail, (size_t)take);
    if (cut) {
      fflush(f);
      if (ftruncate(fileno(f), size - (long)cut) == 0) {
        ESP_LOGW(TAG, "%s: cut %u bytes of partial row", last, (unsigned)cut);
      }
    }
  }
  fclose(f);
}

static void remember(const char *name)
{
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, NVS_LAST, name);
  nvs_commit(h);
  nvs_close(h);
}

static uint32_t next_seq(void)
{
  uint32_t max = 0;
  DIR *d = opendir(s_dir);
  if (!d) return 1;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    uint32_t s = log_parse_seq(e->d_name);
    if (s > max) max = s;
  }
  closedir(d);
  return max + 1;
}

static bool exists(const char *name)
{
  char path[128];
  struct stat st;
  snprintf(path, sizeof path, "%s/%s", s_dir, name);
  return stat(path, &st) == 0;
}

static void drain(void)
{
  log_row_t row;
  meta_t m;
  char line[LOG_ROW_MAX];
  while (xQueueReceive(s_meta, &m, 0) == pdTRUE) append(m.line, strlen(m.line));
  while (xQueueReceive(s_rows, &row, 0) == pdTRUE) {
    size_t n = log_format_row(line, sizeof line, &row.r, row.dropped);
    if (n) append(line, n);
  }
}

static void task(void *arg)
{
  (void)arg;
  int64_t last_sync = esp_timer_get_time();
  while (!s_stop) {
    vTaskDelay(pdMS_TO_TICKS(DRAIN_MS));
    if (!s_file) {
      /* Keep the queues empty so nothing upstream ever waits. */
      xQueueReset(s_rows);
      xQueueReset(s_meta);
      continue;
    }
    drain();
    int64_t now = esp_timer_get_time();
    if (s_flush_now || now - last_sync >= SYNC_MS * 1000LL) {
      s_flush_now = false;
      sync_file();
      last_sync = now;
    }
  }
  if (s_file) {
    drain();
    sync_file();
    if (s_file) fclose(s_file);
    s_file = NULL;
    s_state = LOG_STATE_OFF;
  }
  xSemaphoreGive(s_stopped);
  vTaskDelete(NULL);
}

esp_err_t logger_start(const char *mount_point, bool card_ok, const log_time_t *now,
                       int core, int prio)
{
  s_rows = xQueueCreate(LOGGER_QUEUE_LEN, sizeof(log_row_t));
  s_meta = xQueueCreate(32, sizeof(meta_t));
  s_stopped = xSemaphoreCreateBinary();
  if (!s_rows || !s_meta || !s_stopped) return ESP_ERR_NO_MEM;

  s_state = LOG_STATE_NO_CARD;
  if (card_ok) {
    snprintf(s_dir, sizeof s_dir, "%s/logs", mount_point);
    if (mkdir(s_dir, 0775) != 0 && errno != EEXIST) {
      ESP_LOGE(TAG, "mkdir %s: errno %d", s_dir, errno);
    } else {
      repair_last();
      log_make_name(s_name, sizeof s_name, now->rtc_valid, now->year, now->mon, now->day,
                    now->hour, now->min, now->sec, 0);
      if (!now->rtc_valid || exists(s_name)) {
        log_make_name(s_name, sizeof s_name, false, 0, 0, 0, 0, 0, 0, next_seq());
      }
      char path[128];
      snprintf(path, sizeof path, "%s/%s", s_dir, s_name);
      s_file = fopen(path, "w");
      if (s_file) {
        setvbuf(s_file, NULL, _IONBF, 0); /* we buffer ourselves */
        remember(s_name);
        s_state = LOG_STATE_OK;
        ESP_LOGI(TAG, "logging to %s", path);
      } else {
        ESP_LOGE(TAG, "open %s: errno %d", path, errno);
        s_state = LOG_STATE_ERROR;
        s_name[0] = '\0';
      }
    }
  }
  if (xTaskCreatePinnedToCore(task, "logger", 4096, NULL, prio, NULL, core) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void logger_meta(const char *key, const char *value)
{
  if (!s_meta) return;
  meta_t m;
  if (!log_format_meta(m.line, sizeof m.line, key, value)) return;
  xQueueSend(s_meta, &m, 0);
}

void logger_columns(void)
{
  if (!s_meta) return;
  meta_t m;
  snprintf(m.line, sizeof m.line, "%s\n", LOG_COLUMNS);
  xQueueSend(s_meta, &m, 0);
}

void logger_flush_now(void)
{
  s_flush_now = true;
}

void logger_stop(uint32_t timeout_ms)
{
  if (!s_stopped) return;
  s_stop = true;
  xSemaphoreTake(s_stopped, pdMS_TO_TICKS(timeout_ms));
}
