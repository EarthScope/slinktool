/***************************************************************************
 * test_network.c: coverage for extreply_int(), the file-static helper in
 * network.c that locates an extended reply message in a server response.
 *
 * This file #includes network.c itself to reach its static function.
 * It is linked against libslink.a *after* its own object, so every
 * symbol network.c defines is already satisfied by this translation
 * unit and network.o is never pulled out of the archive -- there is
 * exactly one definition of each symbol in the final binary.
 ***************************************************************************/

#include "../network.c"

#include "slt.h"

static void
test_extreply_none (void)
{
  /* Buffer larger than the reply, tail poisoned with '\r' so an
   * out-of-bounds scan would find a match past the valid data. */
  char buf[16];

  memset (buf, '\r', sizeof (buf));
  memcpy (buf, "OK\r\n", 4);

  SLT_NULL (extreply_int (buf, 4), "no extended reply in 'OK\\r\\n'");
}

static void
test_extreply_present (void)
{
  char buf[16] = "OK\rmsg\r\n";

  SLT_EQ_STR (extreply_int (buf, 8), "msg", "extended reply extracted from 'OK\\rmsg\\r\\n'");
}

static void
test_extreply_present_error (void)
{
  char buf[16] = "ERROR\rtext\r\n";

  SLT_EQ_STR (extreply_int (buf, 12), "text", "extended reply extracted from 'ERROR\\rtext\\r\\n'");
}

static void
test_extreply_empty (void)
{
  char buf[16] = "OK\r\r\n";

  SLT_EQ_STR (extreply_int (buf, 5), "", "empty extended reply in 'OK\\r\\r\\n'");
}

static void
test_extreply_truncated (void)
{
  /* No second '\r' present at all. */
  char buf[16] = "OK\rmsg";

  SLT_NULL (extreply_int (buf, 6), "no second '\\r' yields no extended reply");
}

static void
test_extreply_no_bytes (void)
{
  char buf[16] = {0};

  SLT_NULL (extreply_int (buf, 0), "zero bytes read yields no extended reply");
  SLT_NULL (extreply_int (NULL, 4), "NULL buffer yields no extended reply");
}

static void
test_extreply_no_cr_at_all (void)
{
  char buf[16] = "OKOKOKOK";

  SLT_NULL (extreply_int (buf, 8), "no '\\r' at all yields no extended reply");
}

static void
test_extreply_full_buffer_no_second_cr (void)
{
  /* A full 100-byte readbuf with the first '\r' well past the start and
   * no second one: this is what drove the scan length negative before
   * the length arithmetic was fixed, reading up to 2*(term1-readbuf)
   * bytes past the end of the buffer. */
  char buf[100];

  memset (buf, 'x', sizeof (buf));
  buf[50] = '\r';

  SLT_NULL (extreply_int (buf, sizeof (buf)), "full buffer with no 2nd '\\r' yields no extended reply");
}

static char negotiate_v4_diag[256];

static void
negotiate_v4_diag_capture (const char *msg)
{
  strncat (negotiate_v4_diag, msg, sizeof (negotiate_v4_diag) - strlen (negotiate_v4_diag) - 1);
}

static void
test_negotiate_v4_start_time_too_long (void)
{
  /* negotiate_v4() converts slconn->start_time/end_time into fixed-size
   * stack buffers before ever touching the socket, so an over-long time
   * window string must be rejected before any network activity. slconn
   * has no connection at all here (link == -1), so a -1 return alone
   * doesn't prove the length guard fired -- it could equally mean
   * execution fell through to the send/receive loop and failed there
   * for lack of a socket. Capture the diagnostic log to tell the two
   * apart. */
  SLCD *slconn = sl_initslcd ("t", NULL);
  char longtime[40];

  memset (longtime, '1', sizeof (longtime) - 1);
  longtime[sizeof (longtime) - 1] = '\0';

  negotiate_v4_diag[0] = '\0';
  sl_loginit_r (slconn, 0, NULL, NULL, negotiate_v4_diag_capture, NULL);

  SLT_EQ_INT (sl_set_timewindow (slconn, longtime, NULL), 0,
             "sl_set_timewindow() itself does not bound the string length");
  SLT_EQ_INT (negotiate_v4 (slconn), -1,
             "an over-long start time is rejected before any socket use");
  SLT_ASSERT (strstr (negotiate_v4_diag, "too long") != NULL,
             "rejected specifically by the start-time length guard, not a later, unrelated socket failure");
  SLT_EQ_INT (slconn->config_error, 1,
             "flagged as a caller configuration error, since retrying the identical string cannot succeed");

  sl_freeslcd (slconn);
}

static void
test_negotiate_v4_unparsable_start_time_sets_config_error (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_timewindow (slconn, "not-a-time", NULL), 0,
             "sl_set_timewindow() itself does not validate the string");
  SLT_EQ_INT (negotiate_v4 (slconn), -1, "an unparsable start time is rejected");
  SLT_EQ_INT (slconn->config_error, 1,
             "flagged as a caller configuration error, since retrying the identical string cannot succeed");

  sl_freeslcd (slconn);
}

static void
test_negotiate_v4_oversized_selector_sets_config_error (void)
{
  /* negotiate_v4()'s per-token scratch buffer is 32 bytes; a selector at
   * or beyond that is rejected while still assembling the command list,
   * before any socket use. */
  SLCD *slconn = sl_initslcd ("t", NULL);
  char overlong[40];

  memset (overlong, 'A', sizeof (overlong) - 1);
  overlong[sizeof (overlong) - 1] = '\0';

  SLT_EQ_INT (sl_add_stream (slconn, "XX_TEST", overlong, SL_UNSETSEQUENCE, NULL), 0,
             "stream added with an oversized selector");
  SLT_EQ_INT (negotiate_v4 (slconn), -1, "an oversized selector is rejected");
  SLT_EQ_INT (slconn->config_error, 1,
             "flagged as a caller configuration error, since retrying the identical selector cannot succeed");

  sl_freeslcd (slconn);
}

static void
test_negotiate_uni_v3_start_time_too_long_sets_config_error (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char longtime[40];

  memset (longtime, '1', sizeof (longtime) - 1);
  longtime[sizeof (longtime) - 1] = '\0';

  SLT_EQ_INT (sl_set_timewindow (slconn, longtime, NULL), 0,
             "sl_set_timewindow() itself does not bound the string length");
  SLT_EQ_INT (negotiate_uni_v3 (slconn), -1,
             "an over-long start time is rejected before any socket use");
  SLT_EQ_INT (slconn->config_error, 1,
             "flagged as a caller configuration error, since retrying the identical string cannot succeed");

  sl_freeslcd (slconn);
}

static void
test_negotiate_uni_v3_unparsable_start_time_sets_config_error (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_timewindow (slconn, "not-a-time", NULL), 0,
             "sl_set_timewindow() itself does not validate the string");
  SLT_EQ_INT (negotiate_uni_v3 (slconn), -1, "an unparsable start time is rejected");
  SLT_EQ_INT (slconn->config_error, 1,
             "flagged as a caller configuration error, since retrying the identical string cannot succeed");

  sl_freeslcd (slconn);
}

static void
test_negotiate_uni_v3_no_streams (void)
{
  /* sl_collect() only reaches negotiate_uni_v3() with a non-empty stream
   * list, but sl_configlink() is public API a caller can invoke directly
   * ("low level negotiation independently"); an empty list must return an
   * error, not crash on the unchecked curstream->selectors dereference
   * this used to be. */
  SLCD *slconn = sl_initslcd ("t", NULL);

  slconn->protocol = SLPROTO3X;

  SLT_EQ_INT (negotiate_uni_v3 (slconn), -1, "no stream configured is a clean -1, not a crash");

  sl_freeslcd (slconn);
}

static void
test_sl_configlink_null (void)
{
  SLT_EQ_INT (sl_configlink (NULL), -1, "NULL slconn is guarded, not a crash");
}

static void
test_sl_configlink_unset_protocol (void)
{
  /* sl_collect() always negotiates a protocol via sayhello_int() before
   * calling sl_configlink(), but a direct caller who skips HELLO
   * (sl_connect(slconn, 0)) can reach sl_configlink() with protocol still
   * UNSET_PROTO; it must not silently report success with nothing
   * negotiated. */
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_configlink (slconn), -1, "no protocol negotiated is a clean -1, not silent success");

  sl_freeslcd (slconn);
}

static void
test_sl_configlink_resets_config_error (void)
{
  /* A config_error flag from a previous negotiation attempt must not
   * leak into the next one just because sl_configlink() is called
   * again for a fresh attempt. */
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_allstation_params (slconn, NULL, SL_UNSETSEQUENCE, NULL), 0,
             "all-station mode configured with no selectors");
  slconn->protocol = SLPROTO3X;
  slconn->config_error = 1; /* simulate a stale flag left over from a prior attempt */

  SLT_EQ_INT (sl_configlink (slconn), -1, "negotiation fails with no real connection (link == -1)");
  SLT_EQ_INT (slconn->config_error, 0,
             "sl_configlink() reset config_error before this attempt, and this failure is not a config error");

  sl_freeslcd (slconn);
}

int
main (void)
{
  SLT_RUN (test_extreply_none);
  SLT_RUN (test_extreply_present);
  SLT_RUN (test_extreply_present_error);
  SLT_RUN (test_extreply_empty);
  SLT_RUN (test_extreply_truncated);
  SLT_RUN (test_extreply_no_bytes);
  SLT_RUN (test_extreply_no_cr_at_all);
  SLT_RUN (test_extreply_full_buffer_no_second_cr);
  SLT_RUN (test_negotiate_v4_start_time_too_long);
  SLT_RUN (test_negotiate_v4_unparsable_start_time_sets_config_error);
  SLT_RUN (test_negotiate_v4_oversized_selector_sets_config_error);
  SLT_RUN (test_negotiate_uni_v3_start_time_too_long_sets_config_error);
  SLT_RUN (test_negotiate_uni_v3_unparsable_start_time_sets_config_error);
  SLT_RUN (test_negotiate_uni_v3_no_streams);
  SLT_RUN (test_sl_configlink_null);
  SLT_RUN (test_sl_configlink_unset_protocol);
  SLT_RUN (test_sl_configlink_resets_config_error);

  return SLT_REPORT ();
}
