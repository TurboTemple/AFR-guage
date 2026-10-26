#include "unity.h"

#include <math.h>
#include <string.h>

#include "log_format.h"

void setUp(void) {}
void tearDown(void) {}

static reading_t row(void)
{
  reading_t r;
  memset(&r, 0, sizeof r);
  r.t_ms = 182340;
  r.state = ST_READY;
  r.lambda = 0.8712f;
  r.afr = 0.8712f * 14.7f;
  r.v_out = 1.4059f;
  r.led_pattern = LED_STEADY;
  r.supply_v = 13.92f;
  r.warn = WARN_NONE;
  return r;
}

static void test_spec_example_row(void)
{
  char b[LOG_ROW_MAX];
  reading_t r = row();
  size_t n = log_format_row(b, sizeof b, &r, 0);
  TEST_ASSERT_EQUAL_STRING("182340,READY,0.8712,12.81,1.4059,STEADY,13.92,0,0\n", b);
  TEST_ASSERT_EQUAL_UINT(strlen(b), n);
  /* About 60 bytes a row at 20 Hz: 4.3 MB per hour. */
  TEST_ASSERT_TRUE(n <= 60);
}

static void test_blank_columns(void)
{
  char b[LOG_ROW_MAX];
  reading_t r = row();
  r.state = ST_WARMING;
  r.lambda = NAN;
  r.afr = NAN;
  r.led_pattern = LED_SLOW;
  r.supply_v = NAN;
  r.warn = WARN_LEAN;
  log_format_row(b, sizeof b, &r, 3);
  TEST_ASSERT_EQUAL_STRING("182340,WARMING,,,1.4059,SLOW,,L,3\n", b);
}

static void test_rail_row(void)
{
  char b[LOG_ROW_MAX];
  reading_t r = row();
  r.state = ST_LEAN_RAIL;
  r.lambda = 1.36f;
  r.afr = 1.36f * 14.7f;
  r.v_out = 5.0f;
  r.warn = WARN_RICH;
  log_format_row(b, sizeof b, &r, 0);
  TEST_ASSERT_EQUAL_STRING("182340,LEAN_RAIL,1.3600,19.99,5.0000,STEADY,13.92,R,0\n", b);
}

static void test_fixed(void)
{
  char b[16];
  log_fmt_fixed(b, sizeof b, -0.00004f, 4);
  TEST_ASSERT_EQUAL_STRING("0.0000", b);
  log_fmt_fixed(b, sizeof b, -0.0153f, 4);
  TEST_ASSERT_EQUAL_STRING("-0.0153", b);
  log_fmt_fixed(b, sizeof b, 9.99999f, 2);
  TEST_ASSERT_EQUAL_STRING("10.00", b);
  log_fmt_fixed(b, sizeof b, 12.0f, 0);
  TEST_ASSERT_EQUAL_STRING("12", b);
  TEST_ASSERT_EQUAL_UINT(0, log_fmt_fixed(b, sizeof b, NAN, 2));
  TEST_ASSERT_EQUAL_STRING("", b);
}

static void test_too_small_buffer(void)
{
  char b[20];
  reading_t r = row();
  TEST_ASSERT_EQUAL_UINT(0, log_format_row(b, sizeof b, &r, 0));
}

static void test_meta(void)
{
  char b[64];
  log_format_meta(b, sizeof b, "fuel", "E10 14.08");
  TEST_ASSERT_EQUAL_STRING("# fuel=E10 14.08\n", b);
}

static void test_names(void)
{
  char b[32];
  log_make_name(b, sizeof b, true, 2026, 10, 26, 9, 5, 7, 0);
  TEST_ASSERT_EQUAL_STRING("20261026_090507.csv", b);
  log_make_name(b, sizeof b, false, 0, 0, 0, 0, 0, 0, 1);
  TEST_ASSERT_EQUAL_STRING("S00001.csv", b);
  log_make_name(b, sizeof b, false, 0, 0, 0, 0, 0, 0, 42);
  TEST_ASSERT_EQUAL_STRING("S00042.csv", b);
  TEST_ASSERT_EQUAL_UINT(42, log_parse_seq("S00042.csv"));
  TEST_ASSERT_EQUAL_UINT(7, log_parse_seq("S00007.CSV"));
  TEST_ASSERT_EQUAL_UINT(0, log_parse_seq("20261026_090507.csv"));
  TEST_ASSERT_EQUAL_UINT(0, log_parse_seq("S0004.csv"));
  TEST_ASSERT_EQUAL_UINT(0, log_parse_seq("S00042.txt"));
}

static void test_partial_tail(void)
{
  const char *ok = "a,b\n1,2\n";
  TEST_ASSERT_EQUAL_UINT(0, log_partial_tail(ok, strlen(ok)));
  const char *cut = "a,b\n1,2\n3,";
  TEST_ASSERT_EQUAL_UINT(2, log_partial_tail(cut, strlen(cut)));
  const char *none = "garbage";
  TEST_ASSERT_EQUAL_UINT(7, log_partial_tail(none, strlen(none)));
  TEST_ASSERT_EQUAL_UINT(0, log_partial_tail("", 0));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_spec_example_row);
  RUN_TEST(test_blank_columns);
  RUN_TEST(test_rail_row);
  RUN_TEST(test_fixed);
  RUN_TEST(test_too_small_buffer);
  RUN_TEST(test_meta);
  RUN_TEST(test_names);
  RUN_TEST(test_partial_tail);
  return UNITY_END();
}
