#include <libmseed.h>
#include <tau/tau.h>
#include <time.h>

TEST (time, nstime2timestr)
{
  char timestr[50];
  nstime_t nstime;

  /* Suppress error and warning messages by accumulating them */
  ms_rloginit (NULL, NULL, NULL, NULL, 10);

  /* General parsing test to nstime_t */
  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.123456788Z");
  CHECK (nstime == 1084345689123456788,
         "Failed to convert time string: '2004-05-12T7:8:9.123456788Z'");

  /* Format variations */
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456788");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456788Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_DOY, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456788 (133)");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_DOY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456788Z (133)");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), SEEDORDINAL, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004,133,07:08:09.123456788");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), UNIXEPOCH, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "1084345689.123456788");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), NANOSECONDEPOCH, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "1084345689123456788");

  /* Extremes of the time scale */
  nstime = -2145916799999999998LL;
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "1902-01-01T00:00:00.000000002Z");

  nstime = INT64_MAX; // aka 9223372036854775807LL
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2262-04-11T23:47:16.854775807Z");

  /* Subsecond variations */

  /* Nano subseconds */
  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.123456788Z");
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456788Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, MICRO);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123456Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09Z");

  /* Micro subseconds */
  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.1234Z");
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123400Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123400Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.123400Z");

  /* No subseconds */
  nstime = ms_timestr2nstime ("2004-05-12T7:8:9Z");
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09.000000Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, MICRO_NONE);
  CHECK_STREQ (timestr, "2004-05-12T07:08:09Z");

  /* Negative sub-second times: UNIXEPOCH integer seconds are floored to agree
   * with calendar formats, e.g. -1.5s is second -2, not -1 */
  nstime = -1500000000; /* -1.5 seconds */
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), UNIXEPOCH, NONE);
  CHECK_STREQ (timestr, "-2");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), ISOMONTHDAY_Z, NONE);
  CHECK_STREQ (timestr, "1969-12-31T23:59:58Z");

  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), UNIXEPOCH, NANO);
  CHECK_STREQ (timestr, "-1.500000000");

  nstime = -500000000; /* -0.5 seconds */
  ms_nstime2timestr_n (nstime, timestr, sizeof (timestr), UNIXEPOCH, NANO);
  CHECK_STREQ (timestr, "-0.500000000");

  /* Unset time */
  ms_nstime2timestr_n (NSTUNSET, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "UNSET");

  /* Error time */
  ms_nstime2timestr_n (NSTERROR, timestr, sizeof (timestr), ISOMONTHDAY_Z, NANO_MICRO_NONE);
  CHECK_STREQ (timestr, "ERROR");
}

TEST (time, timestr2nstime)
{
  nstime_t nstime;

  nstime = ms_timestr2nstime ("2004");
  CHECK (nstime == 1072915200000000000, "Failed to convert time string: '2004'");

  nstime = ms_timestr2nstime ("2004-2-9");
  CHECK (nstime == 1076284800000000000, "Failed to convert time string: '2004-2-9'");

  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.12345Z");
  CHECK (nstime == 1084345689123450000, "Failed to convert time string: '2004-05-12T7:8:9.12345Z'");

  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.12345");
  CHECK (nstime == 1084345689123450000, "Failed to convert time string: '2004-05-12T7:8:9.12345'");

  nstime = ms_timestr2nstime ("2004-05-12T7:8:9.123456788");
  CHECK (nstime == 1084345689123456788,
         "Failed to convert time string: '2004-05-12T7:8:9.123456788'");

  nstime = ms_timestr2nstime ("1084345689.123456788");
  CHECK (nstime == 1084345689123456788, "Failed to convert time string: '1084345689.123456788'");

  nstime = ms_timestr2nstime ("1969,201,20,17,40.98");
  CHECK (nstime == -14182939020000000, "Failed to convert time string: '1969,201,20,17,40.98'");

  nstime = ms_timestr2nstime ("1969-201T20:17:40.987654321");
  CHECK (nstime == -14182939012345679,
         "Failed to convert time string: '1969-201T20:17:40.987654321'");

  nstime = ms_timestr2nstime ("-14182939.012345679");
  CHECK (nstime == -14182939012345679, "Failed to convert time string: '-14182939.012345679'");

  /* Extremes of the time scale */
  nstime = ms_timestr2nstime ("1902-01-01T00:00:00.000000002Z");
  CHECK (nstime == -2145916799999999998,
         "Failed to convert time string: '1902-01-01T00:00:00.000000002Z'");

  nstime = ms_timestr2nstime ("2262-04-11T23:47:16.854775807Z");
  CHECK (nstime == INT64_MAX, "Failed to convert time string: '2262-04-11T23:47:16.854775807Z'");

  /* Near-boundary value that remains in range */
  nstime = ms_timestr2nstime ("+9223372036.5");
  CHECK (nstime == 9223372036500000000, "Failed to convert time string: '+9223372036.5'");

  /* On the epoch time scale a leap second is a repeat of the second that
   * follows it, so 2016-12-31T23:59:60 is 2017-01-01T00:00:00 and a fraction
   * that rounds up to a full second carries to 2017-01-01T00:00:01. */
  nstime = ms_timestr2nstime ("2016-12-31T23:59:60");
  CHECK (nstime == 1483228800000000000, "Failed to convert time string: '2016-12-31T23:59:60'");

  nstime = ms_timestr2nstime ("2016-12-31T23:59:60.9999999999");
  CHECK (nstime == 1483228801000000000,
         "Failed to convert time string: '2016-12-31T23:59:60.9999999999'");

  nstime = ms_timestr2nstime ("2016,366,23,59,60.9999999999");
  CHECK (nstime == 1483228801000000000,
         "Failed to convert time string: '2016,366,23,59,60.9999999999'");

  /* A fraction that rounds up to a full second on a non-leap second */
  nstime = ms_timestr2nstime ("2016-12-31T23:59:59.9999999999");
  CHECK (nstime == 1483228800000000000,
         "Failed to convert time string: '2016-12-31T23:59:59.9999999999'");

  /* Parsing error tests */
  nstime = ms_timestr2nstime ("this is not a time string");
  CHECK (nstime == NSTERROR,
         "Failed to produce error for time string: 'this is not a time string'");

  /* Out-of-range seconds */
  nstime = ms_timestr2nstime ("+99999999999");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '+99999999999'");

  /* In-range seconds that overflow once the fraction is added */
  nstime = ms_timestr2nstime ("+9223372036.99999999");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '+9223372036.99999999'");

  /* In-range seconds that underflow once the fraction is subtracted */
  nstime = ms_timestr2nstime ("-9223372036.99999999");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '-9223372036.99999999'");

  nstime = ms_timestr2nstime ("0000-00-00");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '0000-00-00'");

  nstime = ms_timestr2nstime ("5000-00-00");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '5000-00-00'");

  nstime = ms_timestr2nstime ("20040512T000000");
  CHECK (nstime == NSTERROR, "Failed to produce error for time string: '20040512T000000'");
}

/* Verify ms_sampletime() adjusts by one second per leap second contained in
 * the span, not just the first, using the embedded leap second list (which
 * includes leaps at 2015-07-01 and 2017-01-01). */
TEST (time, sampletime_multileap)
{
  nstime_t start;
  nstime_t naivespan;
  nstime_t expected;
  nstime_t result;
  int64_t offsetsec;

  /* Two leap seconds contained: 2015-07-01 and 2017-01-01 */
  start = ms_timestr2nstime ("2015-01-01T00:00:00Z");
  offsetsec = (ms_timestr2nstime ("2017-06-01T00:00:00Z") - start) / NSTMODULUS;
  naivespan = (nstime_t)((double)offsetsec * 1.0 * NSTMODULUS + 0.5);
  expected = start + naivespan - 2 * NSTMODULUS;
  result = ms_sampletime (start, offsetsec, -1.0);
  CHECK (result == expected, "ms_sampletime() did not adjust for both contained leap seconds");

  /* One leap second contained: 2015-07-01 only */
  start = ms_timestr2nstime ("2015-01-01T00:00:00Z");
  offsetsec = (ms_timestr2nstime ("2016-06-01T00:00:00Z") - start) / NSTMODULUS;
  naivespan = (nstime_t)((double)offsetsec * 1.0 * NSTMODULUS + 0.5);
  expected = start + naivespan - 1 * NSTMODULUS;
  result = ms_sampletime (start, offsetsec, -1.0);
  CHECK (result == expected, "ms_sampletime() did not adjust for the single contained leap second");

  /* No leap seconds contained */
  start = ms_timestr2nstime ("2018-01-01T00:00:00Z");
  offsetsec = (ms_timestr2nstime ("2018-02-01T00:00:00Z") - start) / NSTMODULUS;
  naivespan = (nstime_t)((double)offsetsec * 1.0 * NSTMODULUS + 0.5);
  expected = start + naivespan;
  result = ms_sampletime (start, offsetsec, -1.0);
  CHECK (result == expected,
         "ms_sampletime() unexpectedly adjusted a span with no contained leap seconds");
}

/* Verify ms_sampletime() rejects spans that are not representable, instead
 * of casting a non-finite or out-of-range double to nstime_t. */
TEST (time, sampletime_overflow)
{
  nstime_t start;
  nstime_t result;

  start = ms_timestr2nstime ("2020-01-01T00:00:00Z");

  /* Denormal rate drives the span to +Inf before the cast to integer */
  result = ms_sampletime (start, 2, 5e-324);
  CHECK (result == NSTERROR, "ms_sampletime() did not reject a denormal-rate span");

  /* A tiny but normal rate combined with a modest offset overflows int64 */
  result = ms_sampletime (start, 100, 1e-9);
  CHECK (result == NSTERROR, "ms_sampletime() did not reject an out-of-range span");

  /* A normal rate still calculates the correct time */
  result = ms_sampletime (start, 100, 40.0);
  CHECK (result == start + (nstime_t)(100.0 / 40.0 * NSTMODULUS + 0.5),
         "ms_sampletime() did not calculate the expected time for a normal rate");
}

TEST (time, systemtime)
{
  time_t timeval;
  nstime_t currenttime;
  nstime_t difference;

  currenttime = lmp_systemtime ();
  timeval = time (NULL);

  CHECK (currenttime > 0, "lmp_systemtime() failed to get current time");

  /* Check that the current time is within 1 second of the system time */
  difference = currenttime - (nstime_t)timeval * NSTMODULUS;

  CHECK (difference < 1 * NSTMODULUS, "lmp_systemtime() is not within 1 second of system time");
}
