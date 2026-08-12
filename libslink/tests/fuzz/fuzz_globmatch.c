/***************************************************************************
 * fuzz_globmatch.c: mutation-based dynamic testing of sl_globmatch()
 * (globmatch.c) against randomly generated patterns and strings, biased
 * toward glob metacharacters to actually exercise the backtracking and
 * character-class parsing rather than mostly hitting the literal-compare
 * fast path. Build and run by hand under a sanitizer; not part of
 * `make test`.
 ***************************************************************************/

#include "../../globmatch.h"
#include "fuzzcommon.h"

#include <inttypes.h>

#define MAX_LEN 256

/* Heavily weighted toward glob metacharacters and bracket-class syntax,
 * with a few plain letters/digits mixed in -- pure random bytes rarely
 * form a `[...]` class at all, so they'd mostly exercise the literal
 * fast path instead of the backtracking/class-parsing logic. */
static const char METACHARS[] = "*?[]!^-\\abc123";

int
main (int argc, char **argv)
{
  long iterations = 20000000;
  char pattern[MAX_LEN];
  char string[MAX_LEN];
  long i;
  uint64_t seed = fz_setup (argc, argv, &iterations);

  printf ("fuzz_globmatch: seed=%" PRIu64 " iterations=%ld\n", seed, iterations);
  fflush (stdout);

  for (i = 0; i < iterations; i++)
  {
    fz_random_from_alphabet (pattern, sizeof (pattern) - 1, METACHARS, sizeof (METACHARS) - 1);
    fz_random_from_alphabet (string, sizeof (string) - 1, METACHARS, sizeof (METACHARS) - 1);
    sl_globmatch (string, pattern);
  }

  printf ("fuzz_globmatch: survived %ld iterations\n", iterations);
  return 0;
}
