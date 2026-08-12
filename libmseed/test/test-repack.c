#include <libmseed.h>
#include <math.h>
#include <tau/tau.h>

extern int cmpfiles (char *fileA, char *fileB);

/* Write test output files.  Reference files are at "data/reference-<name>" */
#define TESTFILE_REPACK_V3 "testdata-repack.mseed3"
#define TESTFILE_REPACK_V2 "testdata-repack.mseed2"

#define V2INPUT_RECORD "data/reference-testdata-defaults.mseed2"

static char packbuf[4096];
static int packbuflen = 0;

static void
record_handler_buf (char *record, int reclen, void *handlerdata)
{
  (void)handlerdata;

  if (packbuflen + reclen <= (int)sizeof (packbuf))
  {
    memcpy (packbuf + packbuflen, record, reclen);
    packbuflen += reclen;
  }
}

TEST (repack, v3)
{
  MS3Record *msr = NULL;
  char buffer[8192];
  uint32_t flags;
  int packedlength;
  int rv;

  /* Read v2 input data */
  flags = MSF_UNPACKDATA;
  rv = ms3_readmsr (&msr, V2INPUT_RECORD, flags, 0);

  CHECK (rv == MS_NOERROR, "ms3_readmsr() did not return expected MS_NOERROR");
  REQUIRE (msr != NULL, "ms3_readmsr() did not populate 'msr'");

  /* Change some header fields */
  strcpy (msr->sid, "FDSN:XX_REPAK__H_H_Z");
  msr->starttime = ms_timestr2nstime ("2008-05-12T13:44:55.123456789Z");
  msr->samprate = 100.0;
  msr->pubversion = 2;

  /* Repack to v3 record */
  packedlength = msr3_repack_mseed3 (msr, buffer, sizeof (buffer), 0);

  CHECK (packedlength > 0, "msr3_repack_mseed3() returned an error");

  /* Write output */
  FILE *fd = fopen (TESTFILE_REPACK_V3, "wb");

  CHECK (fd != NULL, "Failed to open output file");

  rv = (int)fwrite (buffer, 1, packedlength, fd);

  CHECK (rv == packedlength, "Failed to write output file");
  CHECK (fclose (fd) == 0, "Failed to close output file");

  /* Compare to reference */
  rv = cmpfiles (TESTFILE_REPACK_V3, "data/reference-" TESTFILE_REPACK_V3);

  CHECK (rv == 0, "Repacked v3 record does not match reference");

  ms3_readmsr (&msr, NULL, flags, 0);
}

TEST (repack, v2)
{
  MS3Record *msr = NULL;
  char buffer[8192];
  uint32_t flags;
  int packedlength;
  int rv;

  /* Read v2 input data */
  flags = MSF_UNPACKDATA;
  rv = ms3_readmsr (&msr, V2INPUT_RECORD, flags, 0);

  CHECK (rv == MS_NOERROR, "ms3_readmsr() did not return expected MS_NOERROR");
  REQUIRE (msr != NULL, "ms3_readmsr() did not populate 'msr'");

  /* Change some header fields */
  strcpy (msr->sid, "FDSN:XX_REPAK__H_H_Z");
  msr->starttime = ms_timestr2nstime ("2008-05-12T13:44:55.123456789Z");
  msr->samprate = 100.0;
  msr->pubversion = 2;

  /* Repack to v2 record */
  packedlength = msr3_repack_mseed2 (msr, buffer, sizeof (buffer), 0);

  CHECK (packedlength > 0, "msr3_repack_mseed2() returned an error");

  /* Write output */
  FILE *fd = fopen (TESTFILE_REPACK_V2, "wb");

  CHECK (fd != NULL, "Failed to open output file");

  rv = (int)fwrite (buffer, 1, packedlength, fd);

  CHECK (rv == packedlength, "Failed to write output file");
  CHECK (fclose (fd) == 0, "Failed to close output file");

  /* Compare to reference */
  rv = cmpfiles (TESTFILE_REPACK_V2, "data/reference-" TESTFILE_REPACK_V2);

  CHECK (rv == 0, "Repacked v2 record does not match reference");

  ms3_readmsr (&msr, NULL, flags, 0);
}

/* Test that negative /FDSN/Time/Correction values round-trip through a v2
 * record without losing precision to truncation-toward-zero rounding.
 */
TEST (repack, v2_negative_time_correction)
{
  MS3Record *msr = NULL;
  MS3Record *parsed = NULL;
  double correction;
  int64_t packedsamples = 0;
  int rv;

  msr = msr3_init (msr);
  REQUIRE (msr != NULL, "msr3_init() returned unexpected NULL");

  strcpy (msr->sid, "FDSN:XX_TEST__L_H_Z");
  msr->reclen = 512;
  msr->pubversion = 1;
  msr->starttime = ms_timestr2nstime ("2020-01-01T00:00:00Z");
  msr->samprate = 0;

  msr->extra = "{\"FDSN\":{\"Time\":{\"Correction\":-1.5}}}";
  msr->extralength = (uint16_t)strlen (msr->extra);

  packbuflen = 0;
  rv = msr3_pack (msr, record_handler_buf, NULL, &packedsamples, MSF_FLUSHDATA | MSF_PACKVER2, 0);
  REQUIRE (rv == 1, "msr3_pack() returned unexpected value");
  REQUIRE (packbuflen > 0, "msr3_pack() did not produce any output");

  rv = msr3_parse (packbuf, (uint64_t)packbuflen, &parsed, 0, 0);
  REQUIRE (rv == MS_NOERROR, "msr3_parse() did not return expected MS_NOERROR");
  REQUIRE (parsed != NULL, "msr3_parse() did not populate 'parsed'");

  rv = mseh_get_number (parsed, "/FDSN/Time/Correction", &correction);
  CHECK (rv == 0, "mseh_get_number() returned unexpected non-match");
  CHECK (fabs (correction - (-1.5)) < 0.00001, "/FDSN/Time/Correction did not round-trip -1.5s");

  /* Sub-100-usec correction, rounds to the nearest 100 usec unit */
  msr->extra = "{\"FDSN\":{\"Time\":{\"Correction\":-0.00012}}}";
  msr->extralength = (uint16_t)strlen (msr->extra);

  packbuflen = 0;
  rv = msr3_pack (msr, record_handler_buf, NULL, &packedsamples, MSF_FLUSHDATA | MSF_PACKVER2, 0);
  REQUIRE (rv == 1, "msr3_pack() returned unexpected value");

  msr3_parse (packbuf, (uint64_t)packbuflen, &parsed, 0, 0);
  rv = mseh_get_number (parsed, "/FDSN/Time/Correction", &correction);
  CHECK (rv == 0, "mseh_get_number() returned unexpected non-match");
  CHECK (fabs (correction - (-0.0001)) < 0.00001,
         "/FDSN/Time/Correction did not round-trip -0.00012s");

  msr->extra = NULL;
  msr->extralength = 0;
  msr3_free (&msr);
  msr3_free (&parsed);
}
