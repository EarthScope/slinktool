/***************************************************************************
 * fuzzcommon.h: shared helpers for the mutation-based dynamic-testing
 * drivers in this directory. Header-only, no dependencies beyond libc
 * (and, for fz_build_mseed_seeds()/fz_fill_input(), fixtures.h -- include
 * that before this header to get them).
 *
 * These drivers are not part of the regular `make test` suite -- they are
 * long-running by design, meant to be built and run by hand (`make fuzz`
 * in tests/, or in CI as a separate time-boxed job) under a sanitizer to
 * look for memory-safety and undefined-behavior bugs that reading alone
 * cannot guarantee to find. Each takes an optional iteration count and an
 * optional seed on argv, so a crash can be reproduced deterministically by
 * rerunning with the same seed (printed at start).
 ***************************************************************************/

#ifndef FUZZCOMMON_H
#define FUZZCOMMON_H 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../libslink.h"

/* Number of elements in a fixed-size array, for the token tables driving
 * random_of()-style generators. */
#define FZ_COUNT(x) (sizeof (x) / sizeof ((x)[0]))

/* xorshift64*, adequate for generating varied test input; not
 * cryptographic and not meant to be. */
static uint64_t fz_rng_state = 0x9e3779b97f4a7c15ULL;

static void
fz_seed (uint64_t seed)
{
  fz_rng_state = seed ? seed : 1;
}

static uint64_t
fz_rand (void)
{
  fz_rng_state ^= fz_rng_state << 13;
  fz_rng_state ^= fz_rng_state >> 7;
  fz_rng_state ^= fz_rng_state << 17;
  return fz_rng_state;
}

/* Returns a value in [0, bound). */
static uint64_t
fz_rand_below (uint64_t bound)
{
  return bound ? (fz_rand () % bound) : 0;
}

/* Not every driver uses every helper below; __attribute__((unused))
 * silences -Wunused-function for whichever ones a given driver doesn't
 * call, rather than splitting this header up per-driver. */
static void __attribute__ ((unused))
fz_random_bytes (uint8_t *buf, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    buf[i] = (uint8_t)(fz_rand () & 0xff);
}

/* Flips a handful of random bytes in an otherwise-valid buffer, so
 * mutation reaches deeper into a parser than uniform random noise would
 * on its own -- structure-aware corruption of a real record exercises
 * length/offset fields specifically, which is where the bugs are. */
static void __attribute__ ((unused))
fz_mutate (uint8_t *buf, size_t len)
{
  int flips = 1 + (int)fz_rand_below (8);
  int i;

  if (len == 0)
    return;

  for (i = 0; i < flips; i++)
    buf[fz_rand_below (len)] = (uint8_t)(fz_rand () & 0xff);
}

/* A random string built from a fixed alphabet, e.g. glob metacharacters or
 * ISO-datetime punctuation -- reaches deeper into a parser than uniform
 * random bytes, which rarely form the specific syntax under test. */
static void __attribute__ ((unused))
fz_random_from_alphabet (char *buf, size_t maxlen, const char *alphabet, size_t alphabetlen)
{
  size_t len = fz_rand_below (maxlen);
  size_t i;

  for (i = 0; i < len; i++)
    buf[i] = alphabet[fz_rand_below (alphabetlen)];
  buf[len] = '\0';
}

/* Parses "--iterations N" and "--seed N" from argv, with the given
 * defaults; validates both (a non-positive/unparsable --iterations, or an
 * unparsable --seed, is a usage error, not a silently-accepted 0- or
 * negative-iteration "clean" run), and warns on an unrecognized argument
 * instead of ignoring it outright. Returns 1 if --seed was given
 * (including as literally "0"), 0 otherwise -- distinguishing "seed not
 * supplied" from "seed given as 0" so the seed value this driver itself
 * prints is always the one actually used and thus always replayable
 * (fz_seed() remaps a raw 0 to 1 internally either way). */
static int
fz_parse_args (int argc, char **argv, long *iterations, uint64_t *seed)
{
  int i;
  int seed_given = 0;

  for (i = 1; i < argc; i++)
  {
    if (strcmp (argv[i], "--help") == 0)
    {
      printf ("usage: %s [--iterations N] [--seed N]\n", argv[0]);
      exit (0);
    }
    else if (strcmp (argv[i], "--iterations") == 0 && i + 1 < argc)
    {
      char *end = NULL;
      long value = strtol (argv[++i], &end, 10);

      if (end == argv[i] || *end != '\0' || value <= 0)
      {
        fprintf (stderr, "invalid --iterations value: %s\n", argv[i]);
        exit (1);
      }

      *iterations = value;
    }
    else if (strcmp (argv[i], "--seed") == 0 && i + 1 < argc)
    {
      char *end = NULL;
      unsigned long long value = strtoull (argv[++i], &end, 10);

      if (end == argv[i] || *end != '\0')
      {
        fprintf (stderr, "invalid --seed value: %s\n", argv[i]);
        exit (1);
      }

      *seed = (uint64_t)value;
      seed_given = 1;
    }
    else
    {
      fprintf (stderr, "warning: unrecognized argument: %s\n", argv[i]);
    }
  }

  return seed_given;
}

/* Combines fz_parse_args() with seed resolution (an unsupplied seed falls
 * back to the current time; a supplied one, including 0, is used as-is)
 * and RNG seeding, the three steps every driver's main() otherwise
 * repeated identically. Returns the seed actually used, to print. */
static uint64_t
fz_setup (int argc, char **argv, long *iterations)
{
  uint64_t seed = 0;
  int seed_given = fz_parse_args (argc, argv, iterations, &seed);

  if (!seed_given)
    seed = (uint64_t)time (NULL);

  fz_seed (seed);

  return seed;
}

static void __attribute__ ((unused))
fz_noop_log (const char *message)
{
  (void)message;
}

/* Suppresses the process-global logger every sl_log()/sl_log_r() call not
 * given its own ::SLlog eventually falls back to. Without this, nearly
 * every rejected/malformed input one of these drivers generates by design
 * logs a line, and that I/O dominates runtime -- call this once at
 * startup unless a driver has a specific reason to watch the output. */
static void __attribute__ ((unused))
fz_suppress_logging (void)
{
  sl_loginit (0, fz_noop_log, NULL, fz_noop_log, NULL);
}

#ifdef SLTEST_FIXTURES_H /* fixtures.h must be included before this header */

/* Builds a pair of MAX_BUF-sized synthetic seed records -- a miniSEED2
 * record with a B1000 blockette declaring a 512-byte record length, and a
 * miniSEED3 record with a 200-byte declared data length -- for drivers
 * that mutate real records rather than starting from pure noise. Takes
 * the real buffer size as a parameter (rather than each driver hardcoding
 * its own MAX_BUF) so the seed size and the buffer size the caller
 * eventually declares to the library can't drift apart into a lie about
 * how much room a buffer actually has. */
static void __attribute__ ((unused))
fz_build_mseed_seeds (uint8_t *seed2, uint8_t *seed3, size_t bufsize)
{
  MS2Fields f2;
  MS3Fields f3;

  memset (&f2, 0, sizeof (f2));
  f2.network = "XX";
  f2.station = "TEST";
  f2.channel = "BHZ";
  f2.year = 2024;
  f2.day = 216;
  f2.numblockettes = 1;
  f2.blocketteoffset = MS2_FIXED_LENGTH;
  memset (seed2, 0, bufsize);
  fx_ms2_fixed (seed2, bufsize, &f2, 0);
  fx_ms2_b1000 (seed2, bufsize, MS2_FIXED_LENGTH, 11, 0, 9 /* 2^9 = 512 */, 0, 0);

  memset (&f3, 0, sizeof (f3));
  f3.sid = "FDSN:XX_TEST";
  f3.year = 2024;
  f3.day = 216;
  f3.samplerate = 20.0;
  f3.datalength = 200;
  memset (seed3, 0, bufsize);
  fx_ms3_fixed (seed3, bufsize, &f3, 0);
}

/* Fills buf[0..bufsize) with one iteration's worth of input: pure random
 * bytes (mode 0), a mutated copy of seed2 (mode 1), or a mutated copy of
 * seed3 (mode 2). Always fills the *entire* buffer, even in mode 0 where
 * only `len` bytes are meant to be "declared" to the library call that
 * follows -- leaving the rest uninitialized would make a run's outcome
 * depend on stale stack content the printed seed can't reproduce, and
 * would only be caught by chance under a memory-checking sanitizer that
 * tracks initialization (MSan), not the ASan+UBSan build these drivers are
 * meant to run under. Returns the mode selected, in case a caller's
 * declared length should also depend on it. */
static int __attribute__ ((unused))
fz_fill_input (uint8_t *buf, size_t bufsize, size_t len, const uint8_t *seed2, const uint8_t *seed3)
{
  int mode = (int)fz_rand_below (3);

  if (mode == 0)
  {
    fz_random_bytes (buf, bufsize);
  }
  else if (mode == 1)
  {
    memcpy (buf, seed2, bufsize);
    fz_mutate (buf, len);
  }
  else
  {
    memcpy (buf, seed3, bufsize);
    fz_mutate (buf, len);
  }

  return mode;
}

#endif /* SLTEST_FIXTURES_H */

#endif /* FUZZCOMMON_H */
