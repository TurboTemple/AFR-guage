/* CSV log format: one file per power-up, '#' header lines, one row per
 * reading at the log rate. Header lines that only become known later (the
 * Spartan's identity, a fresh calibration) are appended as '#' lines when
 * they arrive; readers skip '#' lines wherever they appear. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "afr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_COLUMNS "t_ms,state,lambda,afr,v_out,led,tps,supply_v,alarm,dropped"
#define LOG_ROW_MAX 112

/* Fixed-point formatting: newlib's float printf is slow and large. Writes
 * nothing for NAN. Returns the length written. */
size_t log_fmt_fixed(char *buf, size_t n, float v, int decimals);

/* One row, with trailing newline. Returns the length, 0 if it did not fit. */
size_t log_format_row(char *buf, size_t n, const reading_t *r, uint32_t dropped);

/* '#' line "key=value\n". Returns the length, 0 if it did not fit. */
size_t log_format_meta(char *buf, size_t n, const char *key, const char *value);

/* "20261026_154233.csv" from a valid RTC time, else "S00001.csv". */
void log_make_name(char *buf, size_t n, bool rtc_valid, int year, int mon, int day,
                   int hour, int min, int sec, uint32_t seq);
/* Sequence number from "S00042.csv", or 0 if the name is not one. */
uint32_t log_parse_seq(const char *name);

/* Given the last len bytes of a file (up to a few KB), how many bytes to cut
 * so that the file ends on its final newline. Returns 0 if it already does,
 * and len if the chunk holds no newline. */
size_t log_partial_tail(const char *tail, size_t len);


#ifdef __cplusplus
}
#endif
