#include "unity.h"

#include "spartan_proto.h"

void setUp(void) {}
void tearDown(void) {}

static float parse(const char *s)
{
  float v = -999.0f;
  TEST_ASSERT_TRUE(spartan_parse_number(s, &v));
  return v;
}

static void test_parse_first_number(void)
{
  /* The reply format is undocumented: accept echoes, prefixes, endings. */
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.68, parse("0.68"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.36, parse("1.360\r\n"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.68, parse("GETLAMZEROV\r\nLamZeroV: 0.680\r\n"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.0, parse("Type=0"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 3.4, parse("> 3.4"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, -0.5, parse("x-0.5"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.25, parse(".25"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.1, parse("FW v2.1.3"));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 7.0, parse("a-b+7"));
}

static void test_parse_none(void)
{
  float v;
  TEST_ASSERT_FALSE(spartan_parse_number("", &v));
  TEST_ASSERT_FALSE(spartan_parse_number("OK\r\n", &v));
  TEST_ASSERT_FALSE(spartan_parse_number("- . -.", &v));
}

static void test_clean_reply(void)
{
  char out[16];
  spartan_clean_reply("V1.2\r\n\x01", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("V1.2", out);
  spartan_clean_reply("ABCDEFGHIJKLMNOPQRSTUVWXYZ", out, 5);
  TEST_ASSERT_EQUAL_STRING("ABCD", out);
}

static void test_format_commands(void)
{
  char b[24];
  TEST_ASSERT_EQUAL_UINT(8, spartan_fmt_setperf(b, sizeof b, SP_PERF_10MS));
  TEST_ASSERT_EQUAL_STRING("SETPERF1", b);
  TEST_ASSERT_EQUAL_UINT(0, spartan_fmt_setperf(b, sizeof b, 3));
  TEST_ASSERT_EQUAL_UINT(12, spartan_fmt_setslowheat(b, sizeof b, SP_SLOWHEAT_WAIT));
  TEST_ASSERT_EQUAL_STRING("SETSLOWHEAT3", b);
  TEST_ASSERT_EQUAL_UINT(0, spartan_fmt_setslowheat(b, sizeof b, 2));
  TEST_ASSERT_TRUE(spartan_fmt_setlinout(b, sizeof b, 2.5f) > 0);
  TEST_ASSERT_EQUAL_STRING("SETLINOUT2.500", b);
  spartan_fmt_setlinout(b, sizeof b, 0.0f);
  TEST_ASSERT_EQUAL_STRING("SETLINOUT0.000", b);
  spartan_fmt_setlinout(b, sizeof b, 4.9996f);
  TEST_ASSERT_EQUAL_STRING("SETLINOUT5.000", b);
  TEST_ASSERT_EQUAL_UINT(0, spartan_fmt_setlinout(b, sizeof b, 5.1f));
  TEST_ASSERT_EQUAL_UINT(0, spartan_fmt_setlinout(b, sizeof b, -0.1f));
  TEST_ASSERT_EQUAL_UINT(0, spartan_fmt_setlinout(b, 8, 2.5f)); /* too small */
}

static void test_forbidden(void)
{
  TEST_ASSERT_TRUE(spartan_cmd_forbidden("MEMRESET"));
  TEST_ASSERT_TRUE(spartan_cmd_forbidden("docal"));
  TEST_ASSERT_TRUE(spartan_cmd_forbidden("ResetCal"));
  TEST_ASSERT_FALSE(spartan_cmd_forbidden("GETFW"));
  TEST_ASSERT_FALSE(spartan_cmd_forbidden("DOC"));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_parse_first_number);
  RUN_TEST(test_parse_none);
  RUN_TEST(test_clean_reply);
  RUN_TEST(test_format_commands);
  RUN_TEST(test_forbidden);
  return UNITY_END();
}
