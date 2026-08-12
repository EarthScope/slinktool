/***************************************************************************
 * fuzz_detect.c: mutation-based dynamic testing of slutils.c's file-static
 * miniSEED/header parsing helpers -- detect(), receive_header(), and
 * receive_payload() -- against malformed and randomly mutated input.
 * Build and run by hand under a sanitizer; not part of `make test`.
 *
 * #includes slutils.c directly to reach its static functions; see the
 * comment at the top of test_internals.c for why this is safe against
 * duplicate symbols when linked against libslink.a.
 ***************************************************************************/

#include "../../slutils.c"
#include "../fixtures.h"
#include "fuzzcommon.h"

#include <inttypes.h>

#define MAX_BUF 4096

/* Builds a v3 data header ("SL" + six hex digits) followed by a real
 * miniSEED2 record, a v3 INFO header ("SLINFO" + filler + continuation
 * flag), and a v4 header ("SE" + format/subformat/length/seqnum/
 * stationidlength) followed by a station id -- the three wire shapes
 * receive_header() actually branches on. Without these, a seed built only
 * from raw miniSEED (as detect()/receive_payload() want) almost never
 * carries a signature receive_header() recognizes, so its v3/v4 branches
 * and the v3 sequence-parsing loop go essentially unexercised. */
static void
build_header_seeds (uint8_t *seedv3, uint8_t *seedv3info, uint8_t *seedv4)
{
  MS2Fields f;

  memset (seedv3, 0, MAX_BUF);
  memcpy (seedv3, SIGNATURE_V3, 2);
  memcpy (seedv3 + 2, "01A2B3", 6); /* six hex digits */

  memset (&f, 0, sizeof (f));
  f.network = "XX";
  f.station = "TEST";
  f.channel = "BHZ";
  f.year = 2024;
  f.day = 216;
  f.numblockettes = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;
  fx_ms2_fixed (seedv3 + SLHEADSIZE_V3, MAX_BUF - SLHEADSIZE_V3, &f, 0);
  fx_ms2_b1000 (seedv3 + SLHEADSIZE_V3, MAX_BUF - SLHEADSIZE_V3, MS2_FIXED_LENGTH, 11, 0,
               9 /* 2^9 = 512 */, 0, 0);

  memset (seedv3info, 0, MAX_BUF);
  memcpy (seedv3info, INFOSIGNATURE, 6);
  seedv3info[6] = ' '; /* filler */
  seedv3info[7] = '*'; /* continuation flag */

  memset (seedv4, 0, MAX_BUF);
  memcpy (seedv4, SIGNATURE_V4, 2);
  seedv4[2] = SLPAYLOAD_MSEED3;
  seedv4[3] = 0;
  {
    uint32_t payloadlength = 40;
    uint64_t seqnum = 1;
    int k;

    for (k = 0; k < 4; k++)
      seedv4[4 + k] = (uint8_t)(payloadlength >> (8 * k));
    for (k = 0; k < 8; k++)
      seedv4[8 + k] = (uint8_t)(seqnum >> (8 * k));
  }
  seedv4[16] = 7; /* stationidlength */
  memcpy (seedv4 + SLHEADSIZE_V4, "XX_TEST", 7);
}

static void
fuzz_detect_fn (long iterations, const uint8_t *seed2, const uint8_t *seed3)
{
  uint8_t buf[MAX_BUF];
  char payloadformat;
  long i;

  for (i = 0; i < iterations; i++)
  {
    size_t len = 1 + fz_rand_below (MAX_BUF - 1);

    fz_fill_input (buf, MAX_BUF, len, seed2, seed3);
    detect ((const char *)buf, len, &payloadformat);
  }
}

static void
fuzz_receive_header_fn (long iterations, const uint8_t *seedv3, const uint8_t *seedv3info,
                        const uint8_t *seedv4)
{
  uint8_t buf[MAX_BUF];
  SLCD *slconn = sl_initslcd ("t", NULL);
  long i;

  for (i = 0; i < iterations; i++)
  {
    size_t len = fz_rand_below (MAX_BUF);
    int mode = (int)fz_rand_below (4);
    uint32_t bytesavailable;

    if (mode == 0)
    {
      /* Fill the whole buffer, not just `len` bytes: bytesavailable below
       * is drawn independently and is usually larger, so anything past
       * `len` would otherwise be uninitialized stack content the printed
       * seed can't reproduce. */
      fz_random_bytes (buf, MAX_BUF);
    }
    else if (mode == 1)
    {
      memcpy (buf, seedv3, MAX_BUF);
      fz_mutate (buf, len);
    }
    else if (mode == 2)
    {
      memcpy (buf, seedv3info, MAX_BUF);
      fz_mutate (buf, len);
    }
    else
    {
      memcpy (buf, seedv4, MAX_BUF);
      fz_mutate (buf, len);
    }

    slconn->protocol = fz_rand_below (2) ? SLPROTO3X : SLPROTO40;
    /* bytesavailable must never exceed the real capacity of buf -- in
     * real use it is bounded by the fixed-size internal receive buffer
     * it's drawn from, so reporting more here would be a lie about our
     * own input, not a case the library is responsible for handling. */
    bytesavailable = (uint32_t)fz_rand_below (MAX_BUF + 1);

    receive_header (slconn, buf, bytesavailable);
  }

  sl_freeslcd (slconn);
}

static void
fuzz_receive_payload_fn (long iterations, const uint8_t *seed2, const uint8_t *seed3)
{
  uint8_t buf[MAX_BUF];
  char plbuffer[MAX_BUF];
  SLCD *slconn = sl_initslcd ("t", NULL);
  long i;

  for (i = 0; i < iterations; i++)
  {
    size_t len = fz_rand_below (MAX_BUF);
    uint32_t plbuffersize = (uint32_t)fz_rand_below (MAX_BUF + 1);
    /* bytesavailable must never exceed the real capacity of buf; see the
     * comment in fuzz_receive_header_fn() above. */
    uint32_t bytesavailable = (uint32_t)fz_rand_below (MAX_BUF + 1);

    fz_fill_input (buf, MAX_BUF, len, seed2, seed3);

    slconn->protocol = fz_rand_below (2) ? SLPROTO3X : SLPROTO40;
    /* Randomize prior state too: payloadlength/payloadcollected persist
     * across calls in real use, including combinations a single valid
     * sequence of calls might not reach on its own.
     *
     * payloadlength is biased toward 0 a quarter of the time: only when
     * it's exactly 0 does the v3 detect-against-the-receive-buffer path
     * run at all, and a uniform draw over [0, MAX_BUF*2) would give that
     * branch (and the "cannot determine length within N bytes" error path
     * below, gated on recvdatalen) a negligible share of the budget.
     *
     * payloadcollected is drawn independently rather than clamped to
     * <= payloadlength: a caller passing a smaller plbuffersize on a later
     * sl_collect() call than an earlier one did for the same in-flight
     * payload can leave payloadcollected already ahead of payloadlength,
     * and that state deserves coverage too, not just the one reachable by
     * a single self-consistent sequence of calls. */
    slconn->stat->packetinfo.payloadlength =
        (fz_rand_below (4) == 0) ? 0 : (uint32_t)fz_rand_below (MAX_BUF * 2);
    slconn->stat->packetinfo.payloadcollected = (uint32_t)fz_rand_below (MAX_BUF * 2 + 1);
    slconn->stat->packetinfo.payloadformat =
        fz_rand_below (2) ? SLPAYLOAD_UNKNOWN : SLPAYLOAD_MSEED2;
    /* Left at 0 in every prior iteration otherwise, which makes the
     * "buffer full, length still undetermined" error path in
     * receive_payload() unreachable in practice. */
    slconn->recvdatalen = (uint32_t)fz_rand_below (sizeof (slconn->recvbuffer) + 1);

    receive_payload (slconn, plbuffer, plbuffersize, buf, bytesavailable);
  }

  sl_freeslcd (slconn);
}

int
main (int argc, char **argv)
{
  long iterations = 2000000;
  uint8_t seed2[MAX_BUF];
  uint8_t seed3[MAX_BUF];
  uint8_t seedv3[MAX_BUF];
  uint8_t seedv3info[MAX_BUF];
  uint8_t seedv4[MAX_BUF];
  uint64_t seed = fz_setup (argc, argv, &iterations);

  fz_suppress_logging ();

  printf ("fuzz_detect: seed=%" PRIu64 " iterations=%ld (x3 targets)\n", seed, iterations);
  fflush (stdout);

  fz_build_mseed_seeds (seed2, seed3, MAX_BUF);
  build_header_seeds (seedv3, seedv3info, seedv4);

  fuzz_detect_fn (iterations, seed2, seed3);
  printf ("fuzz_detect: detect() survived %ld iterations\n", iterations);
  fflush (stdout);

  fuzz_receive_header_fn (iterations, seedv3, seedv3info, seedv4);
  printf ("fuzz_detect: receive_header() survived %ld iterations\n", iterations);
  fflush (stdout);

  fuzz_receive_payload_fn (iterations, seed2, seed3);
  printf ("fuzz_detect: receive_payload() survived %ld iterations\n", iterations);
  fflush (stdout);

  return 0;
}
