/* Spartan 3 Lite v2 over UART1 (TX GPIO16 -> yellow, RX GPIO44 <- orange
 * through a divider), 9600 8N1. Boot queries and settings commands only:
 * the Lite has no documented live-data command. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool  ok_fw, ok_hw, ok_type, ok_range;
  char  fw[24];
  char  hw[24];
  int   type;      /* 0 LSU 4.9, 1 LSU ADV */
  float lam0;      /* lambda at 0 V */
  float lam5;      /* lambda at 5 V */
} spartan_info_t;

typedef enum {
  SPARTAN_REQ_BOOT_INFO,  /* GETFW, GETHW, GETTYPE, GETLAMZEROV, GETLAMFIVEV */
  SPARTAN_REQ_GET_MODES,  /* GETPERF, GETSLOWHEAT */
  SPARTAN_REQ_SET_PERF,   /* arg: 0, 1, 2 */
  SPARTAN_REQ_SET_SLOWHEAT, /* arg: 0 or 3 */
  SPARTAN_REQ_SET_LINOUT, /* value: volts; service menu only */
} spartan_req_type_t;

typedef struct {
  spartan_req_type_t type;
  int   arg;
  float value;
} spartan_req_t;

typedef struct {
  spartan_req_type_t type;
  bool  ok;               /* the Spartan answered */
  spartan_info_t info;    /* BOOT_INFO */
  int   perf;             /* GET_MODES; -1 if unknown */
  int   slowheat;         /* GET_MODES; -1 if unknown */
} spartan_result_t;

typedef void (*spartan_result_cb_t)(const spartan_result_t *res);

esp_err_t spartan_uart_init(void);
/* Send one command and read its reply line (200 ms time-out), up to three
 * tries. Lines without a digit, such as an echo of the command, are skipped
 * when want_number is set. Returns true if a reply line was read. */
bool spartan_command(const char *cmd, char *reply, size_t n, bool want_number);
bool spartan_get_number(const char *cmd, float *out);
void spartan_read_info(spartan_info_t *info);

/* The `spartan` task: runs queued requests and reports through cb. */
esp_err_t spartan_task_start(spartan_result_cb_t cb, int core, int prio);
bool spartan_request(const spartan_req_t *req);

#ifdef __cplusplus
}
#endif
