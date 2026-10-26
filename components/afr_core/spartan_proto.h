/* Spartan 3 Lite v2 UART protocol helpers (ASCII, 9600 8N1).
 *
 * The reply format is not documented, so a reply is read until newline or
 * a 200 ms time-out and the first number in it is taken. Never send
 * MEMRESET, DOCAL or RESETCAL automatically. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPARTAN_BAUD        9600
#define SPARTAN_TIMEOUT_MS  200
#define SPARTAN_TRIES       3

#define SP_GETFW        "GETFW"
#define SP_GETHW        "GETHW"
#define SP_GETTYPE      "GETTYPE"
#define SP_GETLAMZEROV  "GETLAMZEROV"
#define SP_GETLAMFIVEV  "GETLAMFIVEV"
#define SP_GETPERF      "GETPERF"
#define SP_GETSLOWHEAT  "GETSLOWHEAT"

/* Sensor types reported by GETTYPE. */
#define SP_TYPE_LSU49   0
#define SP_TYPE_LSUADV  1

/* Response modes for SETPERF. */
#define SP_PERF_20MS    0
#define SP_PERF_10MS    1
#define SP_PERF_LEAN    2

/* Smart heat-up: 3 waits up to 10 min for exhaust heat. 0 is off. */
#define SP_SLOWHEAT_OFF  0
#define SP_SLOWHEAT_WAIT 3

/* Extract the first decimal number in s (sign, digits, optional point).
 * Returns false if there is none. */
bool spartan_parse_number(const char *s, float *out);
/* The reply with control characters stripped, for logging, into out. */
void spartan_clean_reply(const char *s, char *out, size_t n);

/* "SETPERF1", "SETSLOWHEAT3", "SETLINOUT2.500". Return the length written,
 * or 0 if the value is out of range. */
size_t spartan_fmt_setperf(char *buf, size_t n, int mode);
size_t spartan_fmt_setslowheat(char *buf, size_t n, int mode);
size_t spartan_fmt_setlinout(char *buf, size_t n, float volts);

/* True for commands the firmware must never send on its own. */
bool spartan_cmd_forbidden(const char *cmd);

#ifdef __cplusplus
}
#endif
