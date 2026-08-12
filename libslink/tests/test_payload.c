/***************************************************************************
 * test_payload.c: sl_payload_info() and sl_payload_summary() coverage
 * over synthetic miniSEED 2 and miniSEED 3 payloads.
 ***************************************************************************/

#include <string.h>

#include "libslink.h"
#include "fixtures.h"
#include "slt.h"

static SLpacketinfo
mkinfo (char format, char subformat, uint32_t payloadlength)
{
  SLpacketinfo pi;

  memset (&pi, 0, sizeof (pi));
  pi.payloadformat    = format;
  pi.payloadsubformat = subformat;
  pi.payloadlength    = payloadlength;

  return pi;
}

static void
test_ms2_basic (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;
  char sourceid[64];
  char starttime[32];
  double samplerate;
  uint32_t samplecount;

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.location      = "00";
  f.channel       = "BHZ";
  f.year          = 2024;
  f.day           = 216; /* 2024-08-03 */
  f.hour          = 17;
  f.min           = 23;
  f.sec           = 18;
  f.fsec          = 500;
  f.numsamples    = 100;
  f.samprate_fact = 100;
  f.samprate_mult = 1;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0 /* little endian, matches host */);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);

  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf),
                               sourceid, sizeof (sourceid),
                               starttime, sizeof (starttime),
                               &samplerate, &samplecount),
             0, "sl_payload_info() succeeds for a valid miniSEED2 header");

  SLT_EQ_STR (sourceid, "FDSN:XX_TEST_00_B_H_Z", "miniSEED2 source id maps NET_STA_LOC_B_S_P");
  SLT_EQ_STR (starttime, "2024-08-03T17:23:18.0500Z", "miniSEED2 start time string");
  SLT_EQ_DBL (samplerate, 100.0, 0.0001, "miniSEED2 sample rate: positive factor, positive multiplier");
  SLT_EQ_UINT (samplecount, 100, "miniSEED2 sample count");
}

static void
test_ms2_samplerate_signs (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;
  double samplerate = -1.0; /* sentinel: sl_payload_info() must overwrite this on success */

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.channel = "BHZ";
  f.year    = 2024;
  f.day     = 216;

  /* Negative factor, positive multiplier: rate = (-1/fact) * mult */
  f.samprate_fact = -100;
  f.samprate_mult = 1;
  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, &samplerate, NULL),
             0, "sl_payload_info() succeeds: negative factor, positive multiplier");
  SLT_EQ_DBL (samplerate, 0.01, 0.0001, "sample rate: negative factor, positive multiplier");

  /* Positive factor, negative multiplier: rate = fact * (-1 * (rate/mult)) */
  samplerate = -1.0;
  f.samprate_fact = 100;
  f.samprate_mult = -2;
  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, &samplerate, NULL),
             0, "sl_payload_info() succeeds: positive factor, negative multiplier");
  SLT_EQ_DBL (samplerate, 50.0, 0.0001, "sample rate: positive factor, negative multiplier");

  /* Negative factor, negative multiplier */
  samplerate = -1.0;
  f.samprate_fact = -100;
  f.samprate_mult = -2;
  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, &samplerate, NULL),
             0, "sl_payload_info() succeeds: negative factor, negative multiplier");
  SLT_EQ_DBL (samplerate, 0.005, 0.0001, "sample rate: negative factor, negative multiplier");
}

static void
test_ms2_byteswap_detect (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;
  char sourceid[64];
  char starttime[32];
  double samplerate;
  uint32_t samplecount;

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TST2";
  f.location      = "";
  f.channel       = "HHZ";
  f.year          = 2024;
  f.day           = 216;
  f.hour          = 1;
  f.min           = 2;
  f.sec           = 3;
  f.fsec          = 0;
  f.numsamples    = 50;
  f.samprate_fact = 50;
  f.samprate_mult = 1;

  /* Written in the byte order opposite of the host: sl_payload_info() must
   * detect this via the year/day sanity check and swap transparently. */
  fx_ms2_fixed (buf, sizeof (buf), &f, 1 /* big endian */);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);

  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf),
                               sourceid, sizeof (sourceid),
                               starttime, sizeof (starttime),
                               &samplerate, &samplecount),
             0, "sl_payload_info() succeeds for a swapped-byte-order header");
  SLT_EQ_STR (sourceid, "FDSN:XX_TST2__H_H_Z", "byte-swapped header still yields the correct source id");
  SLT_EQ_STR (starttime, "2024-08-03T01:02:03.0000Z", "byte-swapped header still yields the correct start time");
  SLT_EQ_DBL (samplerate, 50.0, 0.0001, "byte-swapped header still yields the correct sample rate");
  SLT_EQ_UINT (samplecount, 50, "byte-swapped header still yields the correct sample count");
}

static void
test_ms2_optional_outparams (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.channel = "BHZ";
  f.year    = 2024;
  f.day     = 216;
  f.samprate_fact = 20;
  f.samprate_mult = 1;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);

  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf),
                               NULL, 0, NULL, 0, NULL, NULL),
             0, "sl_payload_info() tolerates every out-param being NULL");
}

static void
test_ms2_truncation (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;
  char sourceid[6]; /* too small for "FDSN:XX_TEST_00_B_H_Z" */
  char starttime[8]; /* too small for the full timestamp */

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.location = "00";
  f.channel  = "BHZ";
  f.year     = 2024;
  f.day      = 216;
  f.hour     = 17;
  f.min      = 23;
  f.sec      = 18;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);

  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf),
                               sourceid, sizeof (sourceid),
                               starttime, sizeof (starttime),
                               NULL, NULL),
             0, "sl_payload_info() succeeds even when output buffers are undersized");
  SLT_EQ_INT ((int)strlen (sourceid), (int)sizeof (sourceid) - 1, "truncated source id fills the buffer");
  SLT_ASSERT (sourceid[sizeof (sourceid) - 1] == '\0', "truncated source id is still null terminated");
  SLT_ASSERT (starttime[sizeof (starttime) - 1] == '\0', "truncated start time is still null terminated");
}

static void
test_ms2_errors (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.channel = "BHZ";
  f.year    = 2024;
  f.day     = 216;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);

  pi = mkinfo (SLPAYLOAD_MSEED2, 0, 47); /* one byte short of the 48-byte fixed header */
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "a miniSEED2 payload shorter than 48 bytes is rejected");

  pi = mkinfo (SLPAYLOAD_UNKNOWN, 0, MS2_FIXED_LENGTH);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "an unsupported payload format is rejected");

  SLT_EQ_INT (sl_payload_info (NULL, NULL, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "a NULL packetinfo is rejected");

  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, NULL, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "a NULL payload buffer is rejected");
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, 0, NULL, 0, NULL, 0, NULL, NULL),
             -1, "a zero-length payload buffer is rejected");
}

static void
test_ms3_basic (void)
{
  uint8_t buf[128] = {0};
  MS3Fields f;
  SLpacketinfo pi;
  size_t hdrlen;
  char sourceid[64];
  char starttime[40];
  double samplerate;
  uint32_t samplecount;

  memset (&f, 0, sizeof (f));
  f.sid         = "FDSN:XX_TEST_00_B_H_Z";
  f.year        = 2024;
  f.day         = 216;
  f.hour        = 17;
  f.min         = 23;
  f.sec         = 18;
  f.nsec        = 123456789;
  f.samplerate  = 100.0;
  f.numsamples  = 200;
  f.pubversion  = 1;

  /* miniSEED3 is always little-endian on the wire, matching this host. */
  hdrlen = fx_ms3_fixed (buf, sizeof (buf), &f, 0);
  SLT_ASSERT (hdrlen > 0, "fx_ms3_fixed() builds a header");

  pi = mkinfo (SLPAYLOAD_MSEED3, 0, (uint32_t)hdrlen);

  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf),
                               sourceid, sizeof (sourceid),
                               starttime, sizeof (starttime),
                               &samplerate, &samplecount),
             0, "sl_payload_info() succeeds for a valid miniSEED3 header");
  SLT_EQ_STR (sourceid, "FDSN:XX_TEST_00_B_H_Z", "miniSEED3 source id comes from the variable-length SID");
  SLT_EQ_STR (starttime, "2024-08-03T17:23:18.123456789Z", "miniSEED3 start time has nanosecond precision");
  SLT_EQ_DBL (samplerate, 100.0, 0.0001, "miniSEED3 sample rate is a direct float64 field");
  SLT_EQ_UINT (samplecount, 200, "miniSEED3 sample count");
}

static void
test_ms3_errors (void)
{
  uint8_t buf[128] = {0};
  MS3Fields f;
  SLpacketinfo pi;
  size_t hdrlen;

  memset (&f, 0, sizeof (f));
  f.sid        = "FDSN:XX_TEST";
  f.year       = 2024;
  f.day        = 216;
  f.samplerate = 20.0;

  hdrlen = fx_ms3_fixed (buf, sizeof (buf), &f, 0);

  pi = mkinfo (SLPAYLOAD_MSEED3, 0, MS3_FIXED_LENGTH - 1); /* shorter than the fixed header alone */
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "a miniSEED3 payload shorter than the fixed header is rejected");

  pi = mkinfo (SLPAYLOAD_MSEED3, 0, (uint32_t)MS3_FIXED_LENGTH); /* header only, SID excluded */
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             -1, "a miniSEED3 payload shorter than header+SID is rejected");

  pi = mkinfo (SLPAYLOAD_MSEED3, 0, (uint32_t)hdrlen);
  SLT_EQ_INT (sl_payload_info (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0, NULL, 0, NULL, NULL),
             0, "the exact header+SID length succeeds");
}

static void
test_summary (void)
{
  uint8_t buf[64] = {0};
  MS2Fields f;
  SLpacketinfo pi;
  char summary[128];
  char shortbuf[10];
  int fulllen;

  memset (&f, 0, sizeof (f));
  f.network       = "XX";
  f.station       = "TEST";
  f.location      = "00";
  f.channel       = "BHZ";
  f.year          = 2024;
  f.day           = 216;
  f.hour          = 17;
  f.min           = 23;
  f.sec           = 18;
  f.numsamples    = 100;
  f.samprate_fact = 20;
  f.samprate_mult = 1;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  pi = mkinfo (SLPAYLOAD_MSEED2, 0, MS2_FIXED_LENGTH);

  fulllen = sl_payload_summary (NULL, &pi, (char *)buf, sizeof (buf), summary, sizeof (summary));
  SLT_ASSERT (fulllen > 0, "sl_payload_summary() returns a positive length");
  SLT_ASSERT (strstr (summary, "FDSN:XX_TEST_00_B_H_Z") != NULL, "summary contains the source id");
  SLT_ASSERT (strstr (summary, "100 samples") != NULL, "summary contains the sample count");

  /* A too-small buffer: snprintf's return is the length that would have
   * been written, so a caller can detect truncation. */
  SLT_EQ_INT (sl_payload_summary (NULL, &pi, (char *)buf, sizeof (buf), shortbuf, sizeof (shortbuf)),
             fulllen, "sl_payload_summary() reports the untruncated length even into a short buffer");
  SLT_ASSERT (shortbuf[sizeof (shortbuf) - 1] == '\0', "the truncated summary is still null terminated");

  SLT_EQ_INT (sl_payload_summary (NULL, &pi, (char *)buf, sizeof (buf), NULL, 0),
             -1, "a NULL summary buffer is rejected");
  SLT_EQ_INT (sl_payload_summary (NULL, NULL, (char *)buf, sizeof (buf), summary, sizeof (summary)),
             -1, "a NULL packetinfo is rejected");
}

int
main (void)
{
  SLT_RUN (test_ms2_basic);
  SLT_RUN (test_ms2_samplerate_signs);
  SLT_RUN (test_ms2_byteswap_detect);
  SLT_RUN (test_ms2_optional_outparams);
  SLT_RUN (test_ms2_truncation);
  SLT_RUN (test_ms2_errors);
  SLT_RUN (test_ms3_basic);
  SLT_RUN (test_ms3_errors);
  SLT_RUN (test_summary);

  return SLT_REPORT ();
}
