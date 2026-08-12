/***************************************************************************
 * test_statefile.c: sl_savestate() / sl_recoverstate() round-trip and
 * format coverage.
 ***************************************************************************/

#include <string.h>

#include "libslink.h"
#include "fixtures.h"
#include "slt.h"

static void
test_roundtrip (void)
{
  SLCD *saver     = sl_initslcd ("t", NULL);
  SLCD *recoverer = sl_initslcd ("t", NULL);
  char *path      = fx_write_tempfile ("");
  SLstream *s;

  sl_add_stream (saver, "XX_TEST", "BHZ", 100, "2024-01-01T00:00:00Z");
  sl_add_stream (saver, "XX_TST2", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_savestate (saver, path), 0, "sl_savestate() succeeds");

  sl_add_stream (recoverer, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);
  sl_add_stream (recoverer, "XX_TST2", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (recoverer, path), 0, "sl_recoverstate() succeeds on a saved file");

  s = fx_find_stream (recoverer, "XX_TEST");
  SLT_NOT_NULL (s, "XX_TEST survives the round trip");
  SLT_EQ_UINT (s->seqnum, 100, "XX_TEST sequence number round-trips");
  SLT_EQ_STR (s->timestamp, "2024-01-01T00:00:00Z", "XX_TEST timestamp round-trips");

  s = fx_find_stream (recoverer, "XX_TST2");
  SLT_NOT_NULL (s, "XX_TST2 survives the round trip");
  SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE, "XX_TST2's unset sequence number round-trips as UNSET");
  SLT_EQ_STR (s->timestamp, "", "XX_TST2's absent timestamp round-trips as empty");

  fx_unlink (path);
  sl_freeslcd (saver);
  sl_freeslcd (recoverer);
}

static void
test_recoverstate_unmatched_streams_ignored (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "#V2 StationID  Sequence  [Timestamp]\n"
      "XX_TEST 100 2024-01-01T00:00:00Z\n"
      "XX_NOPE 200\n");

  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0,
             "an entry for a station not in the stream list is silently ignored");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TEST");

    SLT_NOT_NULL (s, "XX_TEST was added");
    SLT_EQ_UINT (s->seqnum, 100, "the matching station is still updated");
  }

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_legacy_format (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "XX      TEST    1234567890 2021,11,19,17,23,18\n"
      "XX      NONE    -1\n");
  SLstream *s;

  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_NONE", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "a legacy-format state file is recognized without a header");

  s = fx_find_stream (slconn, "XX_TEST");
  SLT_NOT_NULL (s, "XX_TEST was added");
  SLT_EQ_UINT (s->seqnum, 1234567890ULL, "legacy sequence number parsed");
  SLT_EQ_STR (s->timestamp, "2021-11-19T17:23:18Z", "legacy comma-delimited timestamp converted to ISO");

  s = fx_find_stream (slconn, "XX_NONE");
  SLT_NOT_NULL (s, "XX_NONE was added");
  SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE, "legacy '-1' sequence number maps to SL_UNSETSEQUENCE");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_legacy_uni_station (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path   = fx_write_tempfile ("XX      UNI     555\n");

  sl_set_allstation_params (slconn, NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "legacy uni-station entry recognized");
  {
    SLstream *s = fx_find_stream (slconn, "*");

    SLT_NOT_NULL (s, "the all-station entry exists");
    SLT_EQ_UINT (s->seqnum, 555,
                "the legacy 'XX UNI' special case maps to the all-station ('*') entry");
  }

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_unset_keyword (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "#V2 StationID  Sequence  [Timestamp]\n"
      "XX_TEST UNSET\n");

  sl_add_stream (slconn, "XX_TEST", NULL, 999, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "the UNSET keyword is recognized under the V2 format");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TEST");

    SLT_NOT_NULL (s, "XX_TEST was added");
    SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE, "UNSET maps to SL_UNSETSEQUENCE");
  }

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_comments_and_blanks (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "#V2 StationID  Sequence  [Timestamp]\n"
      "# a full comment line\n"
      "\n"
      "XX_TEST 100\n");

  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "comment and blank lines are skipped without error");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TEST");

    SLT_NOT_NULL (s, "XX_TEST was added");
    SLT_EQ_UINT (s->seqnum, 100, "the real entry after comments is still applied");
  }

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_timestamp_too_long (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char line[190];
  char *path;

  /* Legacy format: NET STA Sequence Timestamp, with a Timestamp field far
   * too long for the conversion buffer. */
  strcpy (line, "XX      TEST    1234567890 ");
  memset (line + strlen (line), '9', sizeof (line) - strlen (line) - 2);
  line[sizeof (line) - 2] = '\0';
  strcat (line, "\n");
  path = fx_write_tempfile (line);

  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), -1,
             "an over-long timestamp field is reported as an error, without aborting the file");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TEST");

    SLT_NOT_NULL (s, "XX_TEST was added");
    SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE,
                "the too-long line is skipped and does not update the stream");
  }

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_errors (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *badseq = fx_write_tempfile ("#V2 StationID  Sequence  [Timestamp]\nXX_TEST NOTANUMBER\n");
  char *badline = fx_write_tempfile ("#V2 StationID  Sequence  [Timestamp]\nJUSTONEFIELD\nXX_TEST 100\n");

  SLT_EQ_INT (sl_recoverstate (slconn, "/nonexistent/state/file"), 1,
             "a missing state file returns 1, not an error");

  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, badseq), -1, "an unparseable sequence number is reported as an error");

  SLT_EQ_INT (sl_recoverstate (slconn, badline), -1,
             "a line with too few fields is reported as an error, without aborting the file");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TEST");

    SLT_NOT_NULL (s, "XX_TEST was added");
    SLT_EQ_UINT (s->seqnum, 100,
                "a later, well-formed line in the same file is still applied");
  }

  fx_unlink (badseq);
  fx_unlink (badline);
  sl_freeslcd (slconn);
}

int
main (void)
{
  SLT_RUN (test_roundtrip);
  SLT_RUN (test_recoverstate_unmatched_streams_ignored);
  SLT_RUN (test_recoverstate_legacy_format);
  SLT_RUN (test_recoverstate_legacy_uni_station);
  SLT_RUN (test_recoverstate_unset_keyword);
  SLT_RUN (test_recoverstate_comments_and_blanks);
  SLT_RUN (test_recoverstate_timestamp_too_long);
  SLT_RUN (test_recoverstate_errors);

  return SLT_REPORT ();
}
