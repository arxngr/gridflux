#ifndef GF_TEST_FRAMEWORK_H
#define GF_TEST_FRAMEWORK_H

#include <stdio.h>

static int gf_tests_run = 0;
static int gf_tests_failed = 0;
static int gf_current_failed = 0;

#define RUN_TEST(fn)                                                                     \
    do                                                                                   \
    {                                                                                    \
        gf_current_failed = 0;                                                           \
        fn ();                                                                           \
        gf_tests_run++;                                                                  \
        printf ("[%s] %s\n", gf_current_failed ? "FAIL" : "PASS", #fn);                  \
    } while (0)

#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            gf_tests_failed++;                                                           \
            gf_current_failed++;                                                         \
            printf ("  FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                      \
        }                                                                                \
    } while (0)

#define CHECK_EQ(got, want, msg)                                                         \
    do                                                                                   \
    {                                                                                    \
        long _g = (long)(got), _w = (long)(want);                                        \
        if (_g != _w)                                                                    \
        {                                                                                \
            gf_tests_failed++;                                                           \
            gf_current_failed++;                                                         \
            printf ("  FAIL %s:%d: %s (got %ld, want %ld)\n", __FILE__, __LINE__, msg,   \
                    _g, _w);                                                             \
        }                                                                                \
    } while (0)

#define TEST_SUMMARY()                                                                   \
    (printf ("\n%d test(s), %d failure(s)\n", gf_tests_run, gf_tests_failed),            \
     gf_tests_failed == 0 ? 0 : 1)

#endif // GF_TEST_FRAMEWORK_H
