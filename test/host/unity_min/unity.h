/* A small subset of the Unity test framework's API, enough for these tests,
 * so they build and run without downloading Unity. Configure with
 * -DAFR_FETCH_UNITY=ON to use the real thing. */
#pragma once

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

void setUp(void);
void tearDown(void);

static jmp_buf unity_jmp_;
static int unity_tests_, unity_fails_;
static const char *unity_name_;

#define UNITY_FAIL_AT_(line, ...) do { \
    printf("%s:%d:%s:FAIL: ", __FILE__, (int)(line), unity_name_); \
    printf(__VA_ARGS__); printf("\n"); longjmp(unity_jmp_, 1); } while (0)

#define UNITY_BEGIN() (void)(unity_tests_ = 0, unity_fails_ = 0)
#define UNITY_END() (printf("\n%d Tests %d Failures 0 Ignored\n%s\n", unity_tests_, \
    unity_fails_, unity_fails_ ? "FAIL" : "OK"), unity_fails_)
#define RUN_TEST(fn) do { unity_name_ = #fn; unity_tests_++; \
    if (setjmp(unity_jmp_) == 0) { setUp(); fn(); tearDown(); \
      printf("%s:%d:%s:PASS\n", __FILE__, __LINE__, #fn); } \
    else { unity_fails_++; tearDown(); } } while (0)

#define TEST_FAIL_MESSAGE(msg) UNITY_FAIL_AT_(__LINE__, "%s", msg)
#define TEST_ASSERT_MESSAGE(c, msg) do { if (!(c)) UNITY_FAIL_AT_(__LINE__, "%s", msg); } while (0)
#define TEST_ASSERT(c) do { if (!(c)) UNITY_FAIL_AT_(__LINE__, "Expression false: %s", #c); } while (0)
#define TEST_ASSERT_TRUE(c) TEST_ASSERT(c)
#define TEST_ASSERT_FALSE(c) TEST_ASSERT(!(c))
#define TEST_ASSERT_TRUE_MESSAGE(c, m) TEST_ASSERT_MESSAGE(c, m)
#define TEST_ASSERT_EQUAL_INT(e, a) do { long e_ = (long)(e), a_ = (long)(a); \
    if (e_ != a_) UNITY_FAIL_AT_(__LINE__, "Expected %ld Was %ld", e_, a_); } while (0)
#define TEST_ASSERT_EQUAL(e, a) TEST_ASSERT_EQUAL_INT(e, a)
#define TEST_ASSERT_EQUAL_UINT(e, a) do { unsigned long e_ = (unsigned long)(e), a_ = (unsigned long)(a); \
    if (e_ != a_) UNITY_FAIL_AT_(__LINE__, "Expected %lu Was %lu", e_, a_); } while (0)
#define TEST_ASSERT_EQUAL_INT_MESSAGE(e, a, m) do { long e_ = (long)(e), a_ = (long)(a); \
    if (e_ != a_) UNITY_FAIL_AT_(__LINE__, "Expected %ld Was %ld. %s", e_, a_, m); } while (0)
#define TEST_ASSERT_FLOAT_WITHIN(d, e, a) do { double d_ = (d), e_ = (e), a_ = (a); \
    if (!(fabs(e_ - a_) <= d_)) UNITY_FAIL_AT_(__LINE__, "Expected %.6f Was %.6f (within %.6f)", e_, a_, d_); } while (0)
#define TEST_ASSERT_FLOAT_WITHIN_MESSAGE(d, e, a, m) do { double d_ = (d), e_ = (e), a_ = (a); \
    if (!(fabs(e_ - a_) <= d_)) UNITY_FAIL_AT_(__LINE__, "Expected %.6f Was %.6f (within %.6f). %s", e_, a_, d_, m); } while (0)
#define TEST_ASSERT_FLOAT_IS_NAN(a) do { if (!isnan(a)) UNITY_FAIL_AT_(__LINE__, "Expected NaN Was %.6f", (double)(a)); } while (0)
#define TEST_ASSERT_FLOAT_IS_NOT_NAN(a) do { if (isnan(a)) UNITY_FAIL_AT_(__LINE__, "Expected not NaN"); } while (0)
#define TEST_ASSERT_EQUAL_STRING(e, a) do { const char *e_ = (e), *a_ = (a); \
    if (strcmp(e_, a_) != 0) UNITY_FAIL_AT_(__LINE__, "Expected '%s' Was '%s'", e_, a_); } while (0)
#define TEST_ASSERT_UINT_WITHIN(d, e, a) do { unsigned long d_ = (unsigned long)(d), e_ = (unsigned long)(e), a_ = (unsigned long)(a); \
    if ((a_ > e_ ? a_ - e_ : e_ - a_) > d_) UNITY_FAIL_AT_(__LINE__, "Expected %lu Was %lu (within %lu)", e_, a_, d_); } while (0)
