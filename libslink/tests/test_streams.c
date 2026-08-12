/***************************************************************************
 * test_streams.c: sl_add_stream(), sl_set_allstation_params(),
 * sl_add_streamlist(), and sl_add_streamlist_file().
 ***************************************************************************/

#include <string.h>

#include "libslink.h"
#include "fixtures.h"
#include "slt.h"

static void
test_add_stream_basic (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  SLstream *s;

  SLT_EQ_INT (sl_add_stream (slconn, "XX_TEST", "BHZ", 12345, "2024-01-01T00:00:00Z"),
             0, "sl_add_stream() accepted");
  SLT_EQ_INT (slconn->multistation, 1, "adding a stream enables multistation mode");

  s = slconn->streams;
  SLT_NOT_NULL (s, "the stream list has an entry");
  SLT_EQ_STR (s->stationid, "XX_TEST", "station id stored");
  SLT_EQ_STR (s->selectors, "BHZ", "selectors stored");
  SLT_EQ_UINT (s->seqnum, 12345, "sequence number stored");
  SLT_EQ_STR (s->timestamp, "2024-01-01T00:00:00Z", "timestamp stored (already ISO, unchanged)");

  SLT_EQ_INT (sl_add_stream (slconn, "XX_TST2", NULL, SL_UNSETSEQUENCE, NULL),
             0, "a second stream with no selectors/timestamp is accepted");

  {
    SLstream *s = fx_find_stream (slconn, "XX_TST2");

    SLT_NOT_NULL (s, "XX_TST2 was added to the list");
    SLT_NULL (s->selectors, "NULL selectors are stored as NULL, not an empty string");
    SLT_EQ_UINT (s->seqnum, SL_UNSETSEQUENCE, "unset sequence number stored as SL_UNSETSEQUENCE");
    SLT_EQ_STR (s->timestamp, "", "no timestamp leaves an empty string");
  }

  SLT_EQ_INT (sl_add_stream (NULL, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL),
             -1, "NULL connection rejected");
  SLT_EQ_INT (sl_add_stream (slconn, NULL, NULL, SL_UNSETSEQUENCE, NULL),
             -1, "NULL station id rejected");

  sl_freeslcd (slconn);
}

static void
test_add_stream_sort_order (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  SLstream *cur;
  char order[128] = {0};

  /* Insert out of order and with mixed wildcard partitions; the list must
   * come out partitioned (no-wildcard, then '?', then '*') and
   * alphanumeric within each partition regardless of insertion order. */
  sl_add_stream (slconn, "XX_TST3", NULL, SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_*", NULL, SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_?ONE", NULL, SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_ALFA", NULL, SL_UNSETSEQUENCE, NULL);

  for (cur = slconn->streams; cur; cur = cur->next)
  {
    strcat (order, cur->stationid);
    strcat (order, ",");
  }

  SLT_EQ_STR (order, "XX_ALFA,XX_TEST,XX_TST3,XX_?ONE,XX_*,",
             "streams are partitioned by wildcard class and sorted alphanumerically within each");

  sl_freeslcd (slconn);
}

static void
test_add_stream_truncation (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char longid[64];

  memset (longid, 'X', sizeof (longid) - 1);
  longid[sizeof (longid) - 1] = '\0';

  SLT_EQ_INT (sl_add_stream (slconn, longid, NULL, SL_UNSETSEQUENCE, NULL),
             0, "an over-long station id is still accepted");
  SLT_EQ_INT ((int)strlen (slconn->streams->stationid), SL_MAX_STATIONID - 1,
             "the stored station id is truncated to SL_MAX_STATIONID - 1 characters");

  sl_freeslcd (slconn);
}

static void
test_add_stream_timestamp_bounds (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char longts[64];

  /* A station id at exactly SL_MAX_STATIONID - 1 characters must still be
   * matched back out, fully NUL-terminated. */
  SLT_EQ_INT (sl_add_stream (slconn, "XX_TWENTYONECHARLONGX", NULL, SL_UNSETSEQUENCE, NULL),
             0, "a station id of exactly SL_MAX_STATIONID - 1 chars is accepted");
  SLT_NOT_NULL (fx_find_stream (slconn, "XX_TWENTYONECHARLONGX"),
               "it can be found again by its full id");

  /* A comma-delimited legacy timestamp that fits still converts. */
  SLT_EQ_INT (sl_add_stream (slconn, "XX_TST4", NULL, SL_UNSETSEQUENCE, "2024,01,02,03,04,05"),
             0, "a legacy comma-delimited timestamp within bounds is accepted");
  {
    SLstream *s = fx_find_stream (slconn, "XX_TST4");

    SLT_NOT_NULL (s, "XX_TST4 was added");
    SLT_EQ_STR (s->timestamp, "2024-01-02T03:04:05Z",
               "the legacy timestamp is converted to ISO-8601");
  }

  memset (longts, '1', sizeof (longts) - 1);
  longts[sizeof (longts) - 1] = '\0';

  SLT_EQ_INT (sl_add_stream (slconn, "XX_TST5", NULL, SL_UNSETSEQUENCE, longts),
             -1, "a timestamp too long for the conversion buffer is rejected");
  SLT_NULL (fx_find_stream (slconn, "XX_TST5"), "the rejected stream was not added to the list");

  SLT_EQ_INT (sl_set_allstation_params (slconn, NULL, SL_UNSETSEQUENCE, longts),
             -1, "an over-long timestamp is also rejected for all-station mode");

  sl_freeslcd (slconn);
}

static void
test_add_stream_all_station_conflict (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_allstation_params (slconn, "BHZ", SL_UNSETSEQUENCE, NULL),
             0, "sl_set_allstation_params() accepted on an empty connection");
  SLT_EQ_INT (sl_add_stream (slconn, "XX_TEST", NULL, SL_UNSETSEQUENCE, NULL),
             -1, "adding a specific stream once all-station mode is set is rejected");

  sl_freeslcd (slconn);
}

static void
test_allstation_params (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_allstation_params (slconn, "BHZ BHN", 42, NULL), 0,
             "sl_set_allstation_params() accepted");
  SLT_EQ_STR (slconn->streams->stationid, "*", "all-station entry uses a bare wildcard station id");
  SLT_EQ_STR (slconn->streams->selectors, "BHZ BHN", "all-station selectors stored");
  SLT_EQ_UINT (slconn->streams->seqnum, 42, "all-station sequence number stored");
  SLT_EQ_INT (slconn->multistation, 0, "all-station mode leaves multistation disabled");
  SLT_NULL (slconn->streams->next, "all-station mode has exactly one stream entry");

  /* Calling again overwrites the existing all-station entry rather than
   * rejecting it. */
  SLT_EQ_INT (sl_set_allstation_params (slconn, "LHZ", SL_UNSETSEQUENCE, NULL), 0,
             "sl_set_allstation_params() can be called again to overwrite settings");
  SLT_EQ_STR (slconn->streams->selectors, "LHZ", "overwritten selectors take effect");

  SLT_EQ_INT (sl_set_allstation_params (NULL, "BHZ", SL_UNSETSEQUENCE, NULL), -1,
             "NULL connection rejected");

  sl_freeslcd (slconn);
}

static void
test_add_streamlist_string (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  SLstream *s;

  SLT_EQ_INT (sl_add_streamlist (slconn, "XX_TEST:B_H_E B_H_N,XX_TST2,XX_TST3:H_H_?", NULL),
             3, "sl_add_streamlist() returns the number of streams parsed");

  s = fx_find_stream (slconn, "XX_TEST");
  SLT_NOT_NULL (s, "XX_TEST was added");
  SLT_EQ_STR (s->selectors, "B_H_E B_H_N", "XX_TEST selectors parsed up to the comma");

  s = fx_find_stream (slconn, "XX_TST2");
  SLT_NOT_NULL (s, "XX_TST2 was added");
  SLT_NULL (s->selectors, "XX_TST2 has no selectors and no default was given");

  s = fx_find_stream (slconn, "XX_TST3");
  SLT_NOT_NULL (s, "XX_TST3 was added");
  SLT_EQ_STR (s->selectors, "H_H_?", "XX_TST3 selectors parsed to the end of the string");

  sl_freeslcd (slconn);
}

static void
test_add_streamlist_defaults_and_malformed (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  SLstream *s;

  SLT_EQ_INT (sl_add_streamlist (slconn, "XX_TEST,XX_TST2:BHZ,,XX_TST3", "BHE"),
             3, "malformed (empty) entries between commas are skipped and not counted");

  s = fx_find_stream (slconn, "XX_TEST");
  SLT_NOT_NULL (s, "XX_TEST was added");
  SLT_EQ_STR (s->selectors, "BHE", "a default selector is applied when none is specified");

  s = fx_find_stream (slconn, "XX_TST2");
  SLT_NOT_NULL (s, "XX_TST2 was added");
  SLT_EQ_STR (s->selectors, "BHZ", "an explicit selector overrides the default");

  s = fx_find_stream (slconn, "XX_TST3");
  SLT_NOT_NULL (s, "XX_TST3 was added");
  SLT_EQ_STR (s->selectors, "BHE", "the default is applied to a later entry too");

  SLT_EQ_INT (sl_add_streamlist (NULL, "XX_TEST", NULL), -1, "NULL connection rejected");
  SLT_EQ_INT (sl_add_streamlist (slconn, NULL, NULL), -1, "NULL streamlist string rejected");

  sl_freeslcd (slconn);
}

static void
test_add_streamlist_file (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile (
      "# Comment lines begin with a '#'\n"
      "XX_TST5  BH?\n"
      "XX_TST6\n"
      "XX_TST3  BH? HH? LH?\n");
  SLstream *s;

  SLT_EQ_INT (sl_add_streamlist_file (slconn, path, "LLZ"), 3,
             "sl_add_streamlist_file() returns the number of streams read");

  s = fx_find_stream (slconn, "XX_TST5");
  SLT_NOT_NULL (s, "XX_TST5 was read from the file");
  SLT_EQ_STR (s->selectors, "BH?", "XX_TST5 selectors read from the file");

  s = fx_find_stream (slconn, "XX_TST6");
  SLT_NOT_NULL (s, "XX_TST6 was read from the file");
  SLT_EQ_STR (s->selectors, "LLZ", "a bare station id gets the default selector");

  s = fx_find_stream (slconn, "XX_TST3");
  SLT_NOT_NULL (s, "XX_TST3 was read from the file");
  SLT_EQ_STR (s->selectors, "BH? HH? LH?", "multiple space-separated selectors are read as one string");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

static void
test_add_streamlist_file_errors (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *emptypath = fx_write_tempfile ("# only a comment\n");

  SLT_EQ_INT (sl_add_streamlist_file (slconn, "/nonexistent/path/to/streams.conf", NULL),
             -1, "a missing stream list file returns -1");

  SLT_EQ_INT (sl_add_streamlist_file (slconn, emptypath, NULL), 0,
             "a file with no usable entries returns a count of 0, not an error");

  fx_unlink (emptypath);
  sl_freeslcd (slconn);
}

/* sl_add_streamlist_file() parses each line with
 * sscanf(line, "%63s %199[^\n]", stationid, selectors), then explicitly
 * trims trailing whitespace from the selectors field before storing it. */
static void
test_streamlist_file_trailing_whitespace (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char *path = fx_write_tempfile ("XX_TST5  BH?   \n");
  SLstream *s;

  sl_add_streamlist_file (slconn, path, NULL);

  s = fx_find_stream (slconn, "XX_TST5");
  SLT_NOT_NULL (s, "XX_TST5 was read from the file");
  SLT_EQ_STR (s->selectors, "BH?",
             "trailing whitespace after a selector is stripped, not stored");

  fx_unlink (path);
  sl_freeslcd (slconn);
}

int
main (void)
{
  SLT_RUN (test_add_stream_basic);
  SLT_RUN (test_add_stream_sort_order);
  SLT_RUN (test_add_stream_truncation);
  SLT_RUN (test_add_stream_timestamp_bounds);
  SLT_RUN (test_add_stream_all_station_conflict);
  SLT_RUN (test_allstation_params);
  SLT_RUN (test_add_streamlist_string);
  SLT_RUN (test_add_streamlist_defaults_and_malformed);
  SLT_RUN (test_add_streamlist_file);
  SLT_RUN (test_add_streamlist_file_errors);
  SLT_RUN (test_streamlist_file_trailing_whitespace);

  return SLT_REPORT ();
}
