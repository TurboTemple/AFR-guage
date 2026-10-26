#include "spartan_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool spartan_parse_number(const char *s, float *out)
{
  for (const char *p = s; *p; p++) {
    const char *q = p;
    bool neg = false;
    if ((*q == '-' || *q == '+') && (isdigit((unsigned char)q[1]) ||
        (q[1] == '.' && isdigit((unsigned char)q[2])))) {
      neg = *q == '-';
      q++;
    }
    if (!isdigit((unsigned char)*q) && !(*q == '.' && isdigit((unsigned char)q[1]))) continue;

    float v = 0.0f;
    while (isdigit((unsigned char)*q)) v = v * 10.0f + (float)(*q++ - '0');
    if (*q == '.') {
      q++;
      float scale = 0.1f;
      while (isdigit((unsigned char)*q)) {
        v += (float)(*q++ - '0') * scale;
        scale *= 0.1f;
      }
    }
    *out = neg ? -v : v;
    return true;
  }
  return false;
}

void spartan_clean_reply(const char *s, char *out, size_t n)
{
  size_t j = 0;
  if (n == 0) return;
  for (; *s && j + 1 < n; s++) {
    if (isprint((unsigned char)*s)) out[j++] = *s;
  }
  out[j] = '\0';
}

static size_t fmt(size_t n, int len)
{
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

size_t spartan_fmt_setperf(char *buf, size_t n, int mode)
{
  if (mode < SP_PERF_20MS || mode > SP_PERF_LEAN) return 0;
  return fmt(n, snprintf(buf, n, "SETPERF%d", mode));
}

size_t spartan_fmt_setslowheat(char *buf, size_t n, int mode)
{
  if (mode != SP_SLOWHEAT_OFF && mode != SP_SLOWHEAT_WAIT) return 0;
  return fmt(n, snprintf(buf, n, "SETSLOWHEAT%d", mode));
}

size_t spartan_fmt_setlinout(char *buf, size_t n, float volts)
{
  if (!(volts >= 0.0f && volts <= 5.0f)) return 0;
  /* Three decimals, without pulling in float printf formatting. */
  unsigned mv = (unsigned)(volts * 1000.0f + 0.5f);
  return fmt(n, snprintf(buf, n, "SETLINOUT%u.%03u", mv / 1000u, mv % 1000u));
}

bool spartan_cmd_forbidden(const char *cmd)
{
  static const char *const bad[] = { "MEMRESET", "DOCAL", "RESETCAL" };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    size_t k = strlen(bad[i]);
    bool match = true;
    for (size_t j = 0; j < k; j++) {
      if (toupper((unsigned char)cmd[j]) != bad[i][j]) { match = false; break; }
    }
    if (match) return true;
  }
  return false;
}
