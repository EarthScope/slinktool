/***************************************************************************
 * slt.h:
 *
 * Minimal header-only TAP (Test Anything Protocol) assertion framework
 * for libslink's C test binaries.  No external dependencies.
 *
 * Each test file defines test functions taking no arguments and
 * returning void, registers them with SLT_RUN() inside main(), then
 * calls SLT_REPORT() to print the TAP plan line and exit with the
 * appropriate status.
 *
 * Usage:
 *
 *   #include "slt.h"
 *
 *   static void
 *   test_something (void)
 *   {
 *     SLT_EQ_INT (2 + 2, 4, "arithmetic still works");
 *   }
 *
 *   int
 *   main (void)
 *   {
 *     SLT_RUN (test_something);
 *     return SLT_REPORT ();
 *   }
 ***************************************************************************/

#ifndef SLT_H
#define SLT_H 1

#include <stdio.h>
#include <string.h>
#include <math.h>

static int slt_testnum  = 0;
static int slt_failures = 0;

/* Run a single test function, named by its symbol for failure context */
#define SLT_RUN(fn)                        \
  do                                        \
  {                                         \
    fprintf (stderr, "# %s\n", #fn);        \
    fn ();                                  \
  } while (0)

/* Print the TAP plan line and return a process exit status */
#define SLT_REPORT()                                       \
  (printf ("1..%d\n", slt_testnum),                         \
   (slt_failures == 0) ? 0 : 1)

/* TAP directive for a test that cannot run in this build/environment. */
#define SLT_SKIP(desc, reason)                                     \
  do                                                                \
  {                                                                 \
    slt_testnum++;                                                  \
    printf ("ok %d - %s # SKIP %s\n", slt_testnum, desc, reason);     \
  } while (0)

#define SLT_PASS(desc)                                     \
  do                                                        \
  {                                                         \
    slt_testnum++;                                          \
    printf ("ok %d - %s\n", slt_testnum, desc);              \
  } while (0)

#define SLT_FAIL(desc, fmt, ...)                             \
  do                                                        \
  {                                                         \
    slt_testnum++;                                          \
    slt_failures++;                                          \
    printf ("not ok %d - %s\n", slt_testnum, desc);          \
    fprintf (stderr, "#   at %s:%d: " fmt "\n",               \
             __FILE__, __LINE__, ##__VA_ARGS__);              \
  } while (0)

#define SLT_ASSERT(cond, desc)                              \
  do                                                          \
  {                                                           \
    if (cond)                                                 \
      SLT_PASS (desc);                                        \
    else                                                       \
      SLT_FAIL (desc, "assertion failed: %s", #cond);          \
  } while (0)

#define SLT_EQ_INT(actual, expected, desc)                                \
  do                                                                       \
  {                                                                        \
    long long slt_a_ = (long long)(actual);                                \
    long long slt_e_ = (long long)(expected);                              \
    if (slt_a_ == slt_e_)                                                   \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected %lld, got %lld", slt_e_, slt_a_);            \
  } while (0)

#define SLT_NE_INT(actual, unexpected, desc)                              \
  do                                                                       \
  {                                                                        \
    long long slt_a_ = (long long)(actual);                                \
    long long slt_u_ = (long long)(unexpected);                            \
    if (slt_a_ != slt_u_)                                                   \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected value other than %lld", slt_u_);             \
  } while (0)

#define SLT_EQ_UINT(actual, expected, desc)                                       \
  do                                                                               \
  {                                                                                \
    unsigned long long slt_a_ = (unsigned long long)(actual);                       \
    unsigned long long slt_e_ = (unsigned long long)(expected);                     \
    if (slt_a_ == slt_e_)                                                           \
      SLT_PASS (desc);                                                              \
    else                                                                             \
      SLT_FAIL (desc, "expected %llu, got %llu", slt_e_, slt_a_);                    \
  } while (0)

#define SLT_EQ_DBL(actual, expected, tolerance, desc)                     \
  do                                                                       \
  {                                                                        \
    double slt_a_ = (double)(actual);                                      \
    double slt_e_ = (double)(expected);                                    \
    if (fabs (slt_a_ - slt_e_) <= (tolerance))                              \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected %g +/- %g, got %g", slt_e_, (double)(tolerance), slt_a_); \
  } while (0)

#define SLT_EQ_STR(actual, expected, desc)                                \
  do                                                                       \
  {                                                                        \
    const char *slt_a_ = (actual);                                         \
    const char *slt_e_ = (expected);                                       \
    if (slt_a_ && slt_e_ && strcmp (slt_a_, slt_e_) == 0)                    \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected \"%s\", got \"%s\"",                          \
               slt_e_ ? slt_e_ : "(null)", slt_a_ ? slt_a_ : "(null)");        \
  } while (0)

#define SLT_NULL(actual, desc)                                            \
  do                                                                       \
  {                                                                        \
    if ((actual) == NULL)                                                  \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected NULL, got non-NULL");                        \
  } while (0)

#define SLT_NOT_NULL(actual, desc)                                        \
  do                                                                       \
  {                                                                        \
    if ((actual) != NULL)                                                  \
      SLT_PASS (desc);                                                      \
    else                                                                     \
      SLT_FAIL (desc, "expected non-NULL, got NULL");                        \
  } while (0)

#endif /* SLT_H */
