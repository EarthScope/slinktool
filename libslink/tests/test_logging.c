/***************************************************************************
 * test_logging.c: sl_log_rl()/sl_loginit_rl() message routing, and the
 * process-global sl_log()/sl_loginit() wrappers.
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "libslink.h"
#include "slt.h"

static char captured_log[512];
static int captured_log_count;
static char captured_diag[512];
static int captured_diag_count;

static void
reset_capture (void)
{
  captured_log[0]    = '\0';
  captured_log_count = 0;
  captured_diag[0]   = '\0';
  captured_diag_count = 0;
}

static void
capture_log (const char *msg)
{
  strncpy (captured_log, msg, sizeof (captured_log) - 1);
  captured_log[sizeof (captured_log) - 1] = '\0';
  captured_log_count++;
}

static void
capture_diag (const char *msg)
{
  strncpy (captured_diag, msg, sizeof (captured_diag) - 1);
  captured_diag[sizeof (captured_diag) - 1] = '\0';
  captured_diag_count++;
}

static void
test_level_routing (void)
{
  SLlog *log = sl_loginit_rl (NULL, 2, capture_log, "LOG: ", capture_diag, "ERR: ");

  SLT_NOT_NULL (log, "sl_loginit_rl(NULL, ...) allocates a new SLlog");

  reset_capture ();
  sl_log_rl (log, 0, 0, "normal message\n");
  SLT_EQ_INT (captured_log_count, 1, "level 0 goes to log_print");
  SLT_EQ_INT (captured_diag_count, 0, "level 0 does not go to diag_print");
  SLT_EQ_STR (captured_log, "LOG: normal message\n", "level 0 message carries the log prefix");

  reset_capture ();
  sl_log_rl (log, 1, 0, "diagnostic message\n");
  SLT_EQ_INT (captured_diag_count, 1, "level 1 goes to diag_print");
  SLT_EQ_INT (captured_log_count, 0, "level 1 does not go to log_print");
  SLT_EQ_STR (captured_diag, "LOG: diagnostic message\n", "level 1 message carries the *log* prefix, not the error prefix");

  reset_capture ();
  sl_log_rl (log, 2, 0, "error message\n");
  SLT_EQ_INT (captured_diag_count, 1, "level 2 goes to diag_print");
  SLT_EQ_INT (captured_log_count, 0, "level 2 does not go to log_print");
  SLT_EQ_STR (captured_diag, "ERR: error message\n", "level 2 message carries the error prefix");

  reset_capture ();
  sl_log_rl (log, 5, 0, "high level message\n");
  SLT_EQ_INT (captured_diag_count, 1, "level 5+ is still treated as an error message");
  SLT_EQ_STR (captured_diag, "ERR: high level message\n", "level 5+ still uses the error prefix");

  free (log);
}

static void
test_verbosity_gating (void)
{
  SLlog *log = sl_loginit_rl (NULL, 1, capture_log, NULL, capture_diag, NULL);

  reset_capture ();
  sl_log_rl (log, 0, 1, "at threshold\n");
  SLT_EQ_INT (captured_log_count, 1, "a message at the verbosity threshold (verb == verbosity) is printed");

  reset_capture ();
  sl_log_rl (log, 0, 2, "above threshold\n");
  SLT_EQ_INT (captured_log_count, 0, "a message above the verbosity threshold is suppressed");

  free (log);
}

static void
test_default_error_prefix (void)
{
  /* When no errprefix is set, "error: " is used by default. */
  SLlog *log = sl_loginit_rl (NULL, 2, NULL, NULL, capture_diag, NULL);

  reset_capture ();
  sl_log_rl (log, 2, 0, "boom\n");
  SLT_EQ_STR (captured_diag, "error: boom\n", "the default error prefix is 'error: ' when none is configured");

  free (log);
}

static void
test_message_truncation (void)
{
  SLlog *log = sl_loginit_rl (NULL, 2, capture_log, NULL, NULL, NULL);
  char longmsg[MAX_LOG_MSG_LENGTH * 2];

  memset (longmsg, 'x', sizeof (longmsg) - 2);
  longmsg[sizeof (longmsg) - 2] = '\n';
  longmsg[sizeof (longmsg) - 1] = '\0';

  reset_capture ();
  sl_log_rl (log, 0, 0, "%s", longmsg);

  SLT_EQ_INT ((int)strlen (captured_log), MAX_LOG_MSG_LENGTH - 1,
             "a message longer than MAX_LOG_MSG_LENGTH is truncated to exactly MAX_LOG_MSG_LENGTH - 1 characters");

  free (log);
}

static void
test_null_prefixes_and_callbacks (void)
{
  SLlog *log = sl_loginit_rl (NULL, 2, NULL, NULL, NULL, NULL);
  int retval;

  /* With no diag_print configured, output falls back to fprintf(stderr, ...);
   * an error-level message exercises that fallback without writing to the
   * shared stdout this binary's own TAP output is on (the log_print
   * fallback, for level 0, writes to stdout and is not exercised here for
   * that reason). */
  retval = sl_log_rl (log, 2, 0, "no callback configured\n");
  SLT_ASSERT (retval > 0, "sl_log_rl() with no diag_print callback still returns a formatted length");

  free (log);
}

static void
test_loginit_rl_reuse (void)
{
  SLlog log;
  SLlog *returned;

  memset (&log, 0, sizeof (log));

  returned = sl_loginit_rl (&log, 1, capture_log, "P: ", capture_diag, "E: ");
  SLT_ASSERT (returned == &log, "sl_loginit_rl() with a non-NULL SLlog* reuses it rather than allocating");
  SLT_EQ_INT (log.verbosity, 1, "verbosity applied to the reused SLlog");

  reset_capture ();
  sl_log_rl (&log, 0, 0, "hi\n");
  SLT_EQ_STR (captured_log, "P: hi\n", "the reused SLlog routes messages correctly");
}

static void
test_global_logging (void)
{
  /* sl_log()/sl_loginit() operate on a single process-global SLlog, so
   * this test is self-contained and restores default (NULL) behavior
   * afterward to avoid leaking capture callbacks into other test binaries
   * run in the same process -- though each test is its own process here,
   * it is still good hygiene. */
  sl_loginit (0, capture_log, "G: ", capture_diag, NULL);

  reset_capture ();
  sl_log (0, 0, "global log message\n");
  SLT_EQ_INT (captured_log_count, 1, "sl_log() routes through the process-global SLlog");
  SLT_EQ_STR (captured_log, "G: global log message\n", "sl_log() applies the globally configured prefix");

  reset_capture ();
  sl_log (2, 0, "global error message\n");
  SLT_EQ_INT (captured_diag_count, 1, "sl_log() at error level routes to diag_print globally");

  /* sl_log_r() with a NULL SLCD falls back to the same global parameters */
  reset_capture ();
  sl_log_r (NULL, 0, 0, "via sl_log_r with NULL slconn\n");
  SLT_EQ_INT (captured_log_count, 1, "sl_log_r(NULL, ...) falls back to the global logging parameters");

  sl_loginit (0, NULL, NULL, NULL, NULL); /* restore quiet defaults */
}

int
main (void)
{
  SLT_RUN (test_level_routing);
  SLT_RUN (test_verbosity_gating);
  SLT_RUN (test_default_error_prefix);
  SLT_RUN (test_message_truncation);
  SLT_RUN (test_null_prefixes_and_callbacks);
  SLT_RUN (test_loginit_rl_reuse);
  SLT_RUN (test_global_logging);

  return SLT_REPORT ();
}
