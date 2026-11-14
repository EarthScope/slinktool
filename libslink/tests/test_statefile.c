/***************************************************************************
 * test_statefile.c: sl_savestate() / sl_recoverstate() round-trip and
 * format coverage.
 ***************************************************************************/

#include <stdio.h>
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
test_roundtrip_nanosecond_timestamp (void)
{
  SLCD *saver     = sl_initslcd ("t", NULL);
  SLCD *recoverer = sl_initslcd ("t", NULL);
  char *path      = fx_write_tempfile ("");
  SLstream *s;

  sl_add_stream (saver, "XX_TEST", NULL, 100, "2024-08-03T17:23:18.123456789Z");
  SLT_EQ_INT (sl_savestate (saver, path), 0, "sl_savestate() succeeds");

  sl_add_stream (recoverer, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);
  SLT_EQ_INT (sl_recoverstate (recoverer, path), 0,
             "a nanosecond-resolution timestamp is recovered without error");

  s = fx_find_stream (recoverer, "XX_TEST");
  SLT_NOT_NULL (s, "XX_TEST survives the round trip");
  SLT_EQ_UINT (s->seqnum, 100, "XX_TEST sequence number round-trips");
  SLT_EQ_STR (s->timestamp, "2024-08-03T17:23:18.123456789Z",
             "a nanosecond-resolution timestamp round-trips");

  fx_unlink (path);
  sl_freeslcd (saver);
  sl_freeslcd (recoverer);
}

static void
test_savestate_replaces_file (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile ("previous content\n");
  char tmppath[1024];
  FILE *fp;
  char line[200] = {0};

  snprintf (tmppath, sizeof (tmppath), "%s.tmp", path);
  sl_add_stream (slconn, "XX_TEST", NULL, 100, NULL);

  SLT_EQ_INT (sl_savestate (slconn, path), 0, "sl_savestate() replaces an existing file");
  SLT_ASSERT ((fp = fopen (tmppath, "rb")) == NULL, "no temporary file remains after saving");
  if (fp)
    fclose (fp);

  fp = fopen (path, "rb");
  SLT_NOT_NULL (fp, "the state file exists");
  if (fp)
  {
    SLT_NOT_NULL (fgets (line, sizeof (line), fp), "the state file has a header line");
    SLT_EQ_STR (line, "#V2 StationID  Sequence  [Timestamp]\n", "the previous content is replaced");
    fclose (fp);
  }

  SLT_EQ_INT (sl_savestate (slconn, "/nonexistent/dir/state"), -1,
             "a state file in a missing directory is reported as an error");

  fx_unlink (path);
  sl_freeslcd (slconn);
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
test_recoverstate_wildcard_matched_stations_added (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "#V2 StationID  Sequence  [Timestamp]\n"
      "XX_S1 3 2024-01-01T00:00:00Z\n"
      "XX_S2 5\n"
      "YY_S9 7\n"
      "XX_* 5\n");
  SLstream *s;

  sl_add_stream (slconn, "XX_*", "BHZ", SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "sl_recoverstate() succeeds with wildcard-matched stations");

  s = fx_find_stream (slconn, "XX_S1");
  SLT_NOT_NULL (s, "an entry is added for a station matching a wildcard entry");
  SLT_EQ_UINT (s ? s->seqnum : 0, 3, "the added entry's sequence number is recovered");
  SLT_EQ_STR (s ? s->timestamp : NULL, "2024-01-01T00:00:00Z", "the added entry's timestamp is recovered");
  SLT_EQ_STR (s ? s->selectors : NULL, "BHZ", "the added entry carries the wildcard entry's selectors");

  s = fx_find_stream (slconn, "XX_S2");
  SLT_EQ_UINT (s ? s->seqnum : 0, 5, "a second wildcard-matched station is added");

  SLT_ASSERT (fx_find_stream (slconn, "YY_S9") == NULL, "a station matching no entry is ignored");
  SLT_EQ_UINT (fx_find_stream (slconn, "XX_*")->seqnum, 5, "the wildcard entry itself is recovered");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_all_station_matched_stations_added (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "#V2 StationID  Sequence  [Timestamp]\n"
      "* 10\n"
      "XX_S1 4\n");
  SLstream *s;

  sl_set_allstation_params (slconn, NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), 0, "sl_recoverstate() succeeds in all-station mode");
  SLT_EQ_STR (slconn->streams->stationid, "*", "the all-station entry remains first");
  SLT_EQ_UINT (slconn->streams->seqnum, 10, "the all-station entry is recovered");

  s = slconn->streams->next;
  SLT_ASSERT (s && strcmp (s->stationid, "XX_S1") == 0 && s->seqnum == 4,
              "the station entry is added after the all-station entry");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
read_whole_file (const char *path, char *buf, size_t bufsize)
{
  FILE *fp = fopen (path, "rb");
  size_t len = 0;

  if (fp)
  {
    len = fread (buf, 1, bufsize - 1, fp);
    fclose (fp);
  }

  buf[len] = '\0';
}

static void
test_savestate_wildcard_entries_unset (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile ("");
  char content[512];

  sl_add_stream (slconn, "XX_*", "BHZ", 5, "2024-01-01T00:00:00Z");
  sl_add_stream (slconn, "YY_S?", NULL, 6, NULL);
  sl_add_stream (slconn, "XX_S1", "BHZ", 3, "2024-01-01T00:00:00Z");

  SLT_EQ_INT (sl_savestate (slconn, path), 0, "sl_savestate() succeeds");
  read_whole_file (path, content, sizeof (content));

  SLT_ASSERT (strstr (content, "\nXX_S1 3 2024-01-01T00:00:00Z\n") != NULL,
              "a station entry is written with its sequence number and time stamp");
  SLT_ASSERT (strstr (content, "\nXX_* UNSET\n") != NULL,
              "a * wildcard entry is written without a sequence number or time stamp");
  SLT_ASSERT (strstr (content, "\nYY_S? UNSET\n") != NULL,
              "a ? wildcard entry is written without a sequence number");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_savestate_all_station_entry_keeps_sequence (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile ("");
  char content[512];

  sl_set_allstation_params (slconn, NULL, 10, NULL);

  SLT_EQ_INT (sl_savestate (slconn, path), 0, "sl_savestate() succeeds in all-station mode");
  read_whole_file (path, content, sizeof (content));

  SLT_ASSERT (strstr (content, "\n* 10 \n") != NULL,
              "the all-station entry keeps its sequence number for v3 uni-station resume");

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
test_recoverstate_legacy_station_too_long (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile ("XX ABCDEFGHIJKLMNOPQRSTUVWXYZ 100\n");

  sl_add_stream (slconn, "XX_ABCDEFGHIJKLMNOPQR", NULL, SL_UNSETSEQUENCE, NULL);

  SLT_EQ_INT (sl_recoverstate (slconn, path), -1,
             "an over-long legacy NET STA pair is reported as an error");
  {
    SLstream *s = fx_find_stream (slconn, "XX_ABCDEFGHIJKLMNOPQR");

    SLT_NOT_NULL (s, "the stream exists");
    SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE,
                "an over-long legacy NET STA pair does not update a truncated match");
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

  {
    static const char *badseqs[] = {"-2", "+5", "99999999999999999999999"};
    size_t i;

    for (i = 0; i < sizeof (badseqs) / sizeof (badseqs[0]); i++)
    {
      char content[128];
      char *path;
      SLstream *s = fx_find_stream (slconn, "XX_TEST");

      s->seqnum = 100;
      snprintf (content, sizeof (content), "#V2 StationID  Sequence  [Timestamp]\nXX_TEST\t%s\n",
                badseqs[i]);
      path = fx_write_tempfile (content);

      SLT_EQ_INT (sl_recoverstate (slconn, path), -1,
                 "a signed or out-of-range sequence number is reported as an error");
      SLT_EQ_UINT (s->seqnum, 100, "a signed or out-of-range sequence number is not applied");

      fx_unlink (path);
    }
  }

  fx_unlink (badseq);
  fx_unlink (badline);
  sl_freeslcd (slconn);
}

static void
test_recoverstate_long_line (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char content[512];
  char *path;

  sl_add_stream (slconn, "XX_*", NULL, SL_UNSETSEQUENCE, NULL);

  snprintf (content, sizeof (content), "#V2 StationID  Sequence  [Timestamp]\n"
            "XX_S1 3%*sXX_S2 9\nXX_S3 4\n", 200, "");
  path = fx_write_tempfile (content);

  SLT_EQ_INT (sl_recoverstate (slconn, path), -1, "an over-long line is reported as an error");
  SLT_NULL (fx_find_stream (slconn, "XX_S1"), "an over-long line is skipped");
  SLT_NULL (fx_find_stream (slconn, "XX_S2"), "the remainder of an over-long line is not an entry");
  SLT_NOT_NULL (fx_find_stream (slconn, "XX_S3"), "lines after an over-long line are recovered");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

int
main (void)
{
  SLT_RUN (test_roundtrip);
  SLT_RUN (test_roundtrip_nanosecond_timestamp);
  SLT_RUN (test_savestate_replaces_file);
  SLT_RUN (test_recoverstate_unmatched_streams_ignored);
  SLT_RUN (test_recoverstate_wildcard_matched_stations_added);
  SLT_RUN (test_recoverstate_all_station_matched_stations_added);
  SLT_RUN (test_savestate_wildcard_entries_unset);
  SLT_RUN (test_savestate_all_station_entry_keeps_sequence);
  SLT_RUN (test_recoverstate_legacy_format);
  SLT_RUN (test_recoverstate_legacy_uni_station);
  SLT_RUN (test_recoverstate_unset_keyword);
  SLT_RUN (test_recoverstate_long_line);
  SLT_RUN (test_recoverstate_comments_and_blanks);
  SLT_RUN (test_recoverstate_timestamp_too_long);
  SLT_RUN (test_recoverstate_legacy_station_too_long);
  SLT_RUN (test_recoverstate_errors);

  return SLT_REPORT ();
}
