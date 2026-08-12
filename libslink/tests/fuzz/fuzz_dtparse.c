/***************************************************************************
 * fuzz_dtparse.c: mutation-based dynamic testing of the date-time and
 * selector string parsers in genutils.c -- sl_isodatetime(),
 * sl_commadatetime(), and sl_v3to4selector() -- against malformed and
 * randomly mutated input. Build and run by hand under a sanitizer; not
 * part of `make test`.
 *
 * sl_isodatetime()/sl_commadatetime() take no destination-size parameter
 * -- the documented contract is "as large as the input, plus a byte or
 * two" -- so every input generated here is capped at INPUT_MAX and every
 * output buffer is allocated at INPUT_MAX + headroom, which satisfies
 * that contract regardless of what content is generated. Passing a
 * smaller output buffer would be testing this harness's own bug, not
 * the library's, per the same lesson learned from fuzz_payload.c.
 ***************************************************************************/

#include "../../libslink.h"
#include "fuzzcommon.h"

#include <inttypes.h>

static const char *const ISO_SEEDS[] = {
    "2024-08-03T17:23:18.0Z",
    "2024-08-03T17:23:18Z",
    "2024-08-03",
    "2024,08,03,17,23,18",
};

static const char *const SELECTOR_SEEDS[] = {
    "00BHZ", "BHZ", "EH?.D", "--BHZ", "*", "00_B_H_Z.D",
};

#define INPUT_MAX 128
#define OUTPUT_BUF (INPUT_MAX + 16)

static void
random_input (char *buf, size_t maxlen)
{
  size_t len = fz_rand_below (maxlen);
  fz_random_bytes ((uint8_t *)buf, len);
  buf[len] = '\0';
}

static void
mutated_seed_input (char *buf, size_t bufcap, const char *const *seeds, size_t nseeds)
{
  const char *seed = seeds[fz_rand_below (nseeds)];
  size_t len = strlen (seed);

  if (len >= bufcap)
    len = bufcap - 1;
  memcpy (buf, seed, len);
  buf[len] = '\0';
  fz_mutate ((uint8_t *)buf, len);
}

static void
run_datetime (long iterations)
{
  char input[INPUT_MAX + 1];
  char output[OUTPUT_BUF];
  long i;

  for (i = 0; i < iterations; i++)
  {
    int mode = (int)fz_rand_below (2);

    if (mode == 0)
      random_input (input, sizeof (input) - 1);
    else
      mutated_seed_input (input, sizeof (input), ISO_SEEDS, FZ_COUNT (ISO_SEEDS));

    sl_isodatetime (output, input);

    if (mode == 0)
      random_input (input, sizeof (input) - 1);
    else
      mutated_seed_input (input, sizeof (input), ISO_SEEDS, FZ_COUNT (ISO_SEEDS));

    sl_commadatetime (output, input);
  }
}

static void
run_selector (long iterations)
{
  char input[INPUT_MAX + 1];
  char output[OUTPUT_BUF];
  long i;

  for (i = 0; i < iterations; i++)
  {
    int mode = (int)fz_rand_below (2);
    /* Never exceeds the real destination size (sizeof(output)); varying
     * it downward from there is a legitimate truncation-handling test. */
    int v4selectorlength = (int)fz_rand_below (sizeof (output) + 1);

    if (mode == 0)
      random_input (input, sizeof (input) - 1);
    else
      mutated_seed_input (input, sizeof (input), SELECTOR_SEEDS, FZ_COUNT (SELECTOR_SEEDS));

    sl_v3to4selector (output, v4selectorlength, input);
  }
}

int
main (int argc, char **argv)
{
  long iterations = 10000000;
  uint64_t seed = fz_setup (argc, argv, &iterations);

  fz_suppress_logging ();

  printf ("fuzz_dtparse: seed=%" PRIu64 " iterations=%ld (x2 targets)\n", seed, iterations);
  fflush (stdout);

  run_datetime (iterations);
  printf ("fuzz_dtparse: sl_isodatetime()/sl_commadatetime() survived %ld iterations\n", iterations);
  fflush (stdout);

  run_selector (iterations);
  printf ("fuzz_dtparse: sl_v3to4selector() survived %ld iterations\n", iterations);

  return 0;
}
