/***************************************************************************
 * fuzz_statefile.c: mutation-based dynamic testing of sl_recoverstate()
 * (statefile.c) against malformed and randomly generated state-file
 * content, covering both the legacy and "V2" line formats plus pure
 * random/binary garbage. Build and run by hand under a sanitizer; not
 * part of `make test`.
 *
 * Writes generated content to a single reused file (named uniquely per
 * process so concurrent runs, e.g. several seeds in parallel, can't
 * clobber each other and lose seed-based reproducibility) rather than a
 * fresh mkstemp() path per iteration, since the per-iteration file I/O
 * this target requires (sl_recoverstate() takes a path, not a buffer) is
 * already the throughput bottleneck without adding tempfile churn on top
 * of it.
 ***************************************************************************/

#include "../../libslink.h"
#include "fuzzcommon.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <unistd.h>

#define MAX_LINE 512
#define MAX_LINES 40

static const char *const STATION_TOKENS[] = {
    "XX_TEST", "IU_COLA", "*", "XX", "UNI", "TOOLONGSTATIONIDENTIFIERVALUE",
};

static const char *const SEQ_TOKENS[] = {
    "0", "1234567890", "UNSET", "-1", "99999999999999999999", "not-a-number", "",
};

static const char *const TIMESTAMP_TOKENS[] = {
    "2024-08-03T17:23:18.0Z",
    "2021,11,19,17,23,18",
    "2024-13-99T99:99:99.0Z",
    "not-a-timestamp",
    "2024-08-03T17:23:18.000000000000000000000000000000Z",
    "",
};

static const char *
random_of (const char *const *tokens, size_t count)
{
  return tokens[fz_rand_below (count)];
}

/* Appends formatted text at buf+pos, clamped to bufcap. snprintf()'s
 * return value is the length that *would* have been written, which can
 * exceed the remaining capacity; adding that in unclamped would underflow
 * the next call's `bufcap - pos` to a huge size_t and turn it into a
 * write past the end of buf -- a stack smash in this harness, not the
 * library under test. Returns the new position, capped at bufcap. */
static size_t
append (uint8_t *buf, size_t pos, size_t bufcap, const char *fmt, ...)
{
  va_list ap;
  int n;

  if (pos >= bufcap)
    return bufcap;

  va_start (ap, fmt);
  n = vsnprintf ((char *)buf + pos, bufcap - pos, fmt, ap);
  va_end (ap);

  if (n < 0)
    return pos;

  if ((size_t)n > bufcap - pos)
    return bufcap;

  return pos + (size_t)n;
}

/* Fills buf with one iteration's worth of generated file content: a mix
 * of a V2 header, legacy/V2-shaped lines with randomly chosen (and
 * sometimes mismatched) field counts, comment/blank lines, and raw
 * random bytes -- possibly including embedded NULs and lines far longer
 * than statefile.c's 200-byte line buffer. Returns the length written. */
static size_t
generate_content (uint8_t *buf, size_t bufcap)
{
  size_t pos = 0;
  int nlines = 1 + (int)fz_rand_below (MAX_LINES);
  int has_v2_header = (int)fz_rand_below (2);
  int i;

  if (has_v2_header)
    pos = append (buf, pos, bufcap, "#V2 StationID  Sequence  [Timestamp]\n");

  for (i = 0; i < nlines && pos + MAX_LINE < bufcap; i++)
  {
    int kind = (int)fz_rand_below (5);

    switch (kind)
    {
    case 0: /* legacy-shaped: NET STA SEQ [TIMESTAMP] */
      pos = append (buf, pos, bufcap, "%s %s %s %s\n",
                   random_of (STATION_TOKENS, FZ_COUNT (STATION_TOKENS)),
                   random_of (STATION_TOKENS, FZ_COUNT (STATION_TOKENS)),
                   random_of (SEQ_TOKENS, FZ_COUNT (SEQ_TOKENS)),
                   random_of (TIMESTAMP_TOKENS, FZ_COUNT (TIMESTAMP_TOKENS)));
      break;

    case 1: /* V2-shaped: StationID SEQ [TIMESTAMP] */
      pos = append (buf, pos, bufcap, "%s %s %s\n",
                   random_of (STATION_TOKENS, FZ_COUNT (STATION_TOKENS)),
                   random_of (SEQ_TOKENS, FZ_COUNT (SEQ_TOKENS)),
                   random_of (TIMESTAMP_TOKENS, FZ_COUNT (TIMESTAMP_TOKENS)));
      break;

    case 2: /* comment or blank */
      pos = append (buf, pos, bufcap, "%s\n", fz_rand_below (2) ? "# a comment" : "");
      break;

    case 3: /* a line far longer than the internal 200-byte line buffer */
    {
      size_t len = 200 + fz_rand_below (MAX_LINE - 200 - 1);
      size_t j;
      for (j = 0; j < len && pos + 1 < bufcap; j++, pos++)
        buf[pos] = (uint8_t)(fz_rand () & 0x7f); /* stay printable-ish, no embedded newline */
      if (pos < bufcap)
        buf[pos++] = '\n';
      break;
    }

    default: /* raw random bytes, possibly including embedded NULs */
    {
      size_t len = fz_rand_below (MAX_LINE);
      size_t j;
      for (j = 0; j < len && pos + 1 < bufcap; j++, pos++)
        buf[pos] = (uint8_t)(fz_rand () & 0xff);
      if (pos < bufcap)
        buf[pos++] = '\n';
      break;
    }
    }
  }

  return pos;
}

/* Returns 1 on success, 0 on failure (logged) -- a failure here must stop
 * the iteration from silently "testing" a stale or missing file and still
 * reporting a clean run. */
static int
write_statefile (const char *path, const uint8_t *buf, size_t len)
{
  FILE *fp = fopen (path, "wb");

  if (!fp)
  {
    fprintf (stderr, "fuzz_statefile: fopen(%s) failed: %s\n", path, strerror (errno));
    return 0;
  }

  if (fwrite (buf, 1, len, fp) != len)
  {
    fprintf (stderr, "fuzz_statefile: short write to %s\n", path);
    fclose (fp);
    return 0;
  }

  if (fclose (fp) != 0)
  {
    fprintf (stderr, "fuzz_statefile: fclose(%s) failed: %s\n", path, strerror (errno));
    return 0;
  }

  return 1;
}

static SLCD *
make_slcd_with_streams (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  sl_add_stream (slconn, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "IU_COLA", NULL, SL_UNSETSEQUENCE, NULL);
  return slconn;
}

int
main (int argc, char **argv)
{
  long iterations = 200000;
  uint8_t buf[4096];
  char path[64];
  SLCD *slconn;
  long i;
  uint64_t seed = fz_setup (argc, argv, &iterations);

  fz_suppress_logging ();

  /* Unique per process (not per seed): two runs with different seeds at
   * the same time must not share a path either. */
  snprintf (path, sizeof (path), "/tmp/fuzz_statefile_input.%ld.txt", (long)getpid ());

  printf ("fuzz_statefile: seed=%" PRIu64 " iterations=%ld\n", seed, iterations);
  fflush (stdout);

  slconn = make_slcd_with_streams ();

  for (i = 0; i < iterations; i++)
  {
    size_t len = generate_content (buf, sizeof (buf));

    if (!write_statefile (path, buf, len))
      break;

    sl_recoverstate (slconn, path);
  }

  sl_freeslcd (slconn);
  remove (path);

  printf ("fuzz_statefile: survived %ld iterations\n", iterations);
  return 0;
}
