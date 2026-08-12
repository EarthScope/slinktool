/***************************************************************************
 * fuzz_payload.c: mutation-based dynamic testing of the public payload
 * metadata extraction functions in payload.c -- sl_payload_info() and
 * sl_payload_summary() -- against malformed and randomly mutated input,
 * mismatched buffer sizes, and out-of-range packetinfo fields. Build and
 * run by hand under a sanitizer; not part of `make test`.
 ***************************************************************************/

#include "../../libslink.h"
#include "../fixtures.h"
#include "fuzzcommon.h"

#include <inttypes.h>

#define MAX_BUF 4096

/* Payload formats worth exercising specifically, beyond pure-random bytes
 * in the field: the two recognized ones, and a handful the switch in
 * sl_payload_info() must reject cleanly. */
static char
random_payloadformat (void)
{
  static const char formats[] = {
      SLPAYLOAD_MSEED2, SLPAYLOAD_MSEED3, SLPAYLOAD_UNKNOWN,
      SLPAYLOAD_JSON,   SLPAYLOAD_XML,    'Z',
  };
  return formats[fz_rand_below (sizeof (formats))];
}

static void
run (long iterations, const uint8_t *seed2, const uint8_t *seed3)
{
  uint8_t plbuffer[MAX_BUF];
  char sourceid[128];
  char starttimestr[128];
  char summary[256];
  long i;

  for (i = 0; i < iterations; i++)
  {
    SLpacketinfo packetinfo;
    double samplerate = 0.0;
    uint32_t samplecount = 0;
    size_t len = fz_rand_below (MAX_BUF);
    /* Never exceeds the real destination size -- doing so would be a lie
     * to the callee about how much room it actually has, which is the
     * caller's responsibility to get right, not something this API can
     * defend against. Under-reporting (including 0) is fair game. */
    uint32_t plbuffer_size = (uint32_t)fz_rand_below (MAX_BUF + 1);
    size_t sourceid_size = fz_rand_below (sizeof (sourceid) + 1);
    size_t starttimestr_size = fz_rand_below (sizeof (starttimestr) + 1);
    size_t summary_size = fz_rand_below (sizeof (summary) + 1);

    fz_fill_input (plbuffer, MAX_BUF, len, seed2, seed3);

    memset (&packetinfo, 0, sizeof (packetinfo));
    packetinfo.payloadformat = random_payloadformat ();
    /* payloadlength deliberately not tied to plbuffer_size or len -- every
     * combination of "declared longer/shorter than the real buffer, and
     * longer/shorter than what's actually initialized" must be safe. */
    packetinfo.payloadlength = (uint32_t)fz_rand_below (MAX_BUF * 2);

    sl_payload_info (NULL, &packetinfo, (const char *)plbuffer, plbuffer_size,
                     fz_rand_below (2) ? sourceid : NULL, sourceid_size,
                     fz_rand_below (2) ? starttimestr : NULL, starttimestr_size,
                     fz_rand_below (2) ? &samplerate : NULL,
                     fz_rand_below (2) ? &samplecount : NULL);

    sl_payload_summary (NULL, &packetinfo, (const char *)plbuffer, plbuffer_size, summary,
                        summary_size);
  }
}

int
main (int argc, char **argv)
{
  long iterations = 10000000;
  uint8_t seed2[MAX_BUF];
  uint8_t seed3[MAX_BUF];
  uint64_t seed = fz_setup (argc, argv, &iterations);

  fz_suppress_logging ();

  printf ("fuzz_payload: seed=%" PRIu64 " iterations=%ld\n", seed, iterations);
  fflush (stdout);

  fz_build_mseed_seeds (seed2, seed3, MAX_BUF);
  run (iterations, seed2, seed3);

  printf ("fuzz_payload: survived %ld iterations\n", iterations);
  return 0;
}
