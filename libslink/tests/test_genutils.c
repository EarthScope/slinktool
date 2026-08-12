/***************************************************************************
 * test_genutils.c: coverage for the pure utility functions in genutils.c.
 ***************************************************************************/

#include <string.h>

#include "libslink.h"
#include "slt.h"

static void
test_littleendianhost (void)
{
  union
  {
    uint16_t u;
    uint8_t b[2];
  } probe;
  uint8_t expected;

  probe.u  = 1;
  expected = probe.b[0]; /* 1 on little-endian, 0 on big-endian */

  SLT_EQ_UINT (sl_littleendianhost (), expected, "sl_littleendianhost() matches this host's actual byte order");
}

static void
test_doy2md (void)
{
  int month, mday;

  SLT_EQ_INT (sl_doy2md (2024, 1, &month, &mday), 0, "day 1 of a leap year succeeds");
  SLT_EQ_INT (month, 1, "day 1 is January");
  SLT_EQ_INT (mday, 1, "day 1 is the 1st");

  SLT_EQ_INT (sl_doy2md (2024, 366, &month, &mday), 0, "day 366 of a leap year succeeds");
  SLT_EQ_INT (month, 12, "day 366 (leap) is December");
  SLT_EQ_INT (mday, 31, "day 366 (leap) is the 31st");

  SLT_EQ_INT (sl_doy2md (2024, 60, &month, &mday), 0, "day 60 of a leap year succeeds");
  SLT_EQ_INT (month, 2, "day 60 (leap) is February");
  SLT_EQ_INT (mday, 29, "day 60 (leap) is the 29th (leap day)");

  SLT_EQ_INT (sl_doy2md (2023, 365, &month, &mday), 0, "day 365 of a non-leap year succeeds");
  SLT_EQ_INT (month, 12, "day 365 (non-leap) is December");
  SLT_EQ_INT (mday, 31, "day 365 (non-leap) is the 31st");

  SLT_EQ_INT (sl_doy2md (2023, 60, &month, &mday), 0, "day 60 of a non-leap year succeeds");
  SLT_EQ_INT (month, 3, "day 60 (non-leap) is March");
  SLT_EQ_INT (mday, 1, "day 60 (non-leap) is the 1st (no Feb 29)");

  SLT_EQ_INT (sl_doy2md (2023, 366, &month, &mday), -1, "day 366 of a non-leap year is rejected");
  SLT_EQ_INT (sl_doy2md (2024, 367, &month, &mday), -1, "day 367 of a leap year is rejected");
  SLT_EQ_INT (sl_doy2md (2024, 0, &month, &mday), -1, "day 0 is rejected");
  SLT_EQ_INT (sl_doy2md (2024, -1, &month, &mday), -1, "negative day is rejected");
  SLT_EQ_INT (sl_doy2md (1899, 1, &month, &mday), -1, "year below range is rejected");
  SLT_EQ_INT (sl_doy2md (2101, 1, &month, &mday), -1, "year above range is rejected");
  SLT_EQ_INT (sl_doy2md (1900, 1, &month, &mday), 0, "lower year boundary is accepted");
  SLT_EQ_INT (sl_doy2md (2100, 1, &month, &mday), 0, "upper year boundary is accepted");
}

static void
test_protocol_details (void)
{
  uint8_t major, minor;

  SLT_EQ_STR (sl_protocol_details (SLPROTO3X, &major, &minor), "3.X", "v3 protocol string");
  SLT_EQ_INT (major, 3, "v3 major version");
  SLT_EQ_INT (minor, 0, "v3 minor version");

  SLT_EQ_STR (sl_protocol_details (SLPROTO40, &major, &minor), "4.0", "v4 protocol string");
  SLT_EQ_INT (major, 4, "v4 major version");
  SLT_EQ_INT (minor, 0, "v4 minor version");

  SLT_EQ_STR (sl_protocol_details (UNSET_PROTO, &major, &minor), "Unknown", "unset protocol string");
  SLT_EQ_INT (major, 0, "unset major version");
  SLT_EQ_INT (minor, 0, "unset minor version");

  /* Optional out-params must be tolerated as NULL */
  SLT_EQ_STR (sl_protocol_details (SLPROTO40, NULL, NULL), "4.0", "NULL major/minor pointers are tolerated");
}

static void
test_formatstr (void)
{
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_MSEED2, 0), "miniSEED 2", "plain miniSEED 2");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_MSEED2, 'E'), "miniSEED 2 event detection", "miniSEED 2 subformat E");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_MSEED2, 'L'), "miniSEED 2 log", "miniSEED 2 subformat L");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_MSEED3, 0), "miniSEED 3", "plain miniSEED 3");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_JSON, SLPAYLOAD_JSON_INFO), "INFO in JSON", "JSON INFO subformat");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_JSON, SLPAYLOAD_JSON_ERROR), "ERROR in JSON", "JSON ERROR subformat");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_JSON, 0), "JSON", "plain JSON");
  SLT_EQ_STR (sl_formatstr (SLPAYLOAD_UNKNOWN, 0), "Unknown", "unknown payload format");
  SLT_EQ_STR (sl_formatstr ((char)0x7f, 0), "Unrecognized payload type", "unrecognized payload format");
}

static void
test_gswap (void)
{
  uint16_t v2 = 0x0102;
  uint32_t v4 = 0x01020304;
  uint64_t v8 = 0x0102030405060708ULL;

  sl_gswap2 (&v2);
  SLT_EQ_UINT (v2, 0x0201, "sl_gswap2() reverses byte order");
  sl_gswap2 (&v2);
  SLT_EQ_UINT (v2, 0x0102, "sl_gswap2() is its own inverse");

  sl_gswap4 (&v4);
  SLT_EQ_UINT (v4, 0x04030201, "sl_gswap4() reverses byte order");
  sl_gswap4 (&v4);
  SLT_EQ_UINT (v4, 0x01020304, "sl_gswap4() is its own inverse");

  sl_gswap8 (&v8);
  SLT_EQ_UINT (v8, 0x0807060504030201ULL, "sl_gswap8() reverses byte order");
  sl_gswap8 (&v8);
  SLT_EQ_UINT (v8, 0x0102030405060708ULL, "sl_gswap8() is its own inverse");
}

static void
test_nstime (void)
{
  int64_t t1 = sl_nstime ();
  int64_t t2 = sl_nstime ();

  /* Sanity range: some time between 2020-01-01 and 2100-01-01 */
  SLT_ASSERT (t1 > SL_EPOCH2SLTIME (1577836800LL), "sl_nstime() is after 2020-01-01");
  SLT_ASSERT (t1 < SL_EPOCH2SLTIME (4102444800LL), "sl_nstime() is before 2100-01-01");
  SLT_ASSERT (t2 >= t1, "sl_nstime() is monotonically non-decreasing across two calls");
}

static void
test_strncpclean (void)
{
  char dest[16];
  int n;

  n = sl_strncpclean (dest, "XX   ", 5);
  SLT_EQ_INT (n, 2, "sl_strncpclean() returns count of non-space characters");
  SLT_EQ_STR (dest, "XX", "sl_strncpclean() strips trailing spaces");

  n = sl_strncpclean (dest, "T E S T", 7);
  SLT_EQ_INT (n, 4, "sl_strncpclean() strips embedded spaces too");
  SLT_EQ_STR (dest, "TEST", "sl_strncpclean() left-justifies remaining characters");

  n = sl_strncpclean (dest, "     ", 5);
  SLT_EQ_INT (n, 0, "sl_strncpclean() of all-spaces returns 0");
  SLT_EQ_STR (dest, "", "sl_strncpclean() of all-spaces yields an empty string");

  n = sl_strncpclean (dest, "ABCDE", 5);
  SLT_EQ_INT (n, 5, "sl_strncpclean() with no spaces copies the exact length");
  SLT_EQ_STR (dest, "ABCDE", "sl_strncpclean() with no spaces preserves content");
}

static void
test_isodatetime (void)
{
  char out[64];

  SLT_EQ_STR (sl_isodatetime (out, "2024,08,03,17,23,18"),
             "2024-08-03T17:23:18Z", "comma-delimited converts to ISO with Z suffix");
  SLT_EQ_STR (sl_isodatetime (out, "2024-08-03T17:23:18.5"),
             "2024-08-03T17:23:18.5Z", "ISO input missing Z gets one appended");
  SLT_EQ_STR (sl_isodatetime (out, "2024-08-03T17:23:18.5Z"),
             "2024-08-03T17:23:18.5Z", "ISO input with Z is passed through unchanged");
  SLT_EQ_STR (sl_isodatetime (out, "2024,08,03"),
             "2024-08-03", "date-only comma input has no Z appended (fewer than 3 delimiters)");
  SLT_NULL (sl_isodatetime (out, NULL), "NULL input datetime returns NULL");
  SLT_NULL (sl_isodatetime (NULL, "2024,08,03"), "NULL output buffer returns NULL");
  SLT_NULL (sl_isodatetime (out, "2024/08/03"), "unrecognized delimiter returns NULL");
  SLT_NULL (sl_isodatetime (out, "2024,08,03,17,23,18,5,9"), "an eighth delimiter (beyond fractional seconds) is rejected");

  /* In-place conversion: output buffer is the same as the input buffer */
  {
    char buf[32];
    strcpy (buf, "2024,08,03,17,23,18");
    SLT_EQ_STR (sl_isodatetime (buf, buf), "2024-08-03T17:23:18Z", "in-place conversion works when out == in");
  }
}

static void
test_commadatetime (void)
{
  char out[64];

  SLT_EQ_STR (sl_commadatetime (out, "2024-08-03T17:23:18.5Z"),
             "2024,08,03,17,23,18", "ISO input converts to comma-delimited, truncating fractional seconds");
  SLT_EQ_STR (sl_commadatetime (out, "2024-08-03T17:23:18"),
             "2024,08,03,17,23,18", "ISO input without Z converts identically");
  SLT_EQ_STR (sl_commadatetime (out, "2024,08,03,17,23,18"),
             "2024,08,03,17,23,18", "comma input passes through unchanged");
  SLT_NULL (sl_commadatetime (out, NULL), "NULL input datetime returns NULL");
  SLT_NULL (sl_commadatetime (NULL, "2024-08-03"), "NULL output buffer returns NULL");
  SLT_NULL (sl_commadatetime (out, "2024/08/03"), "unrecognized delimiter returns NULL");

  /* In-place conversion */
  {
    char buf[32];
    strcpy (buf, "2024-08-03T17:23:18.5Z");
    SLT_EQ_STR (sl_commadatetime (buf, buf), "2024,08,03,17,23,18", "in-place conversion works when out == in");
  }
}

static void
test_v3to4selector (void)
{
  char out[32];

  SLT_EQ_STR (sl_v3to4selector (out, sizeof (out), "BHZ"), "*_B_H_Z", "3-char selector gets a wildcard location");
  SLT_EQ_STR (sl_v3to4selector (out, sizeof (out), "00BHZ"), "00_B_H_Z", "5-char selector keeps its location code");
  SLT_EQ_STR (sl_v3to4selector (out, sizeof (out), "--BHZ"), "_B_H_Z", "leading dashes become an empty location code");
  SLT_EQ_STR (sl_v3to4selector (out, sizeof (out), "EH?.D"), "*_E_H_?.D", "wildcard component and a type suffix are preserved");
  SLT_EQ_STR (sl_v3to4selector (out, sizeof (out), "0BHZ"), "0_B_H_Z", "4-char selector (single-char location)");
  SLT_NULL (sl_v3to4selector (out, sizeof (out), NULL), "NULL selector returns NULL");
  SLT_NULL (sl_v3to4selector (NULL, sizeof (out), "BHZ"), "NULL output buffer returns NULL");
  SLT_NULL (sl_v3to4selector (out, sizeof (out), "BH"), "a 2-character selector has no recognized conversion");
  SLT_NULL (sl_v3to4selector (out, sizeof (out), "B#Z"), "an invalid stream-id character is rejected");

  {
    char tiny[4];
    SLT_NULL (sl_v3to4selector (tiny, sizeof (tiny), "00BHZ"), "an output buffer too small to hold the result returns NULL");
  }
}

int
main (void)
{
  SLT_RUN (test_littleendianhost);
  SLT_RUN (test_doy2md);
  SLT_RUN (test_protocol_details);
  SLT_RUN (test_formatstr);
  SLT_RUN (test_gswap);
  SLT_RUN (test_nstime);
  SLT_RUN (test_strncpclean);
  SLT_RUN (test_isodatetime);
  SLT_RUN (test_commadatetime);
  SLT_RUN (test_v3to4selector);

  return SLT_REPORT ();
}
