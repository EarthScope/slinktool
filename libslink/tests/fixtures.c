/***************************************************************************
 * fixtures.c: see fixtures.h
 ***************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fixtures.h"

static void
put16 (uint8_t *p, uint16_t v, int big_endian)
{
  if (big_endian)
  {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
  }
  else
  {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
  }
}

static void
put32 (uint8_t *p, uint32_t v, int big_endian)
{
  if (big_endian)
  {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xff);
  }
  else
  {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
  }
}

static void
put64raw (uint8_t *p, uint64_t v, int big_endian)
{
  int idx;

  for (idx = 0; idx < 8; idx++)
  {
    int shift  = big_endian ? (7 - idx) * 8 : idx * 8;
    p[idx] = (uint8_t)(v >> shift);
  }
}

static void
putpad (uint8_t *p, const char *s, size_t len)
{
  size_t slen = s ? strlen (s) : 0;
  size_t idx;

  for (idx = 0; idx < len; idx++)
    p[idx] = (idx < slen) ? (uint8_t)s[idx] : (uint8_t)' ';
}

void
fx_ms2_fixed (uint8_t *buf, size_t bufsize, const MS2Fields *f, int big_endian)
{
  if (bufsize < MS2_FIXED_LENGTH)
  {
    fprintf (stderr, "fx_ms2_fixed(): buffer too small (%zu < %d)\n", bufsize,
             MS2_FIXED_LENGTH);
    abort ();
  }

  memset (buf, 0, MS2_FIXED_LENGTH);

  /* Sequence number, ASCII "000001" */
  memcpy (buf + 0, "000001", 6);
  buf[6] = 'D'; /* data quality */
  buf[7] = ' '; /* reserved */

  putpad (buf + 8, f->station, 5);
  putpad (buf + 13, f->location, 2);
  putpad (buf + 15, f->channel, 3);
  putpad (buf + 18, f->network, 2);

  put16 (buf + 20, f->year, big_endian);
  put16 (buf + 22, f->day, big_endian);
  buf[24] = f->hour;
  buf[25] = f->min;
  buf[26] = f->sec;
  buf[27] = 0; /* unused */
  put16 (buf + 28, f->fsec, big_endian);
  put16 (buf + 30, f->numsamples, big_endian);
  put16 (buf + 32, (uint16_t)f->samprate_fact, big_endian);
  put16 (buf + 34, (uint16_t)f->samprate_mult, big_endian);
  buf[36] = 0; /* activity flags */
  buf[37] = 0; /* I/O flags */
  buf[38] = 0; /* data quality flags */
  buf[39] = f->numblockettes;
  put32 (buf + 40, 0, big_endian); /* time correction */
  put16 (buf + 44, f->dataoffset, big_endian);
  put16 (buf + 46, f->blocketteoffset, big_endian);
} /* End of fx_ms2_fixed() */

void
fx_ms2_b1000 (uint8_t *buf, size_t bufsize, size_t offset,
             uint8_t encoding, uint8_t byteorder, uint8_t reclen_pow2,
             uint16_t next, int big_endian)
{
  uint8_t *b = buf + offset;

  if (offset + MS2_B1000_LENGTH > bufsize)
  {
    fprintf (stderr, "fx_ms2_b1000(): buffer too small (offset %zu + %d > %zu)\n", offset,
             MS2_B1000_LENGTH, bufsize);
    abort ();
  }

  put16 (b + 0, 1000, big_endian);
  put16 (b + 2, next, big_endian);
  b[4] = encoding;
  b[5] = byteorder;
  b[6] = reclen_pow2;
  b[7] = 0; /* reserved */
} /* End of fx_ms2_b1000() */

size_t
fx_ms3_fixed (uint8_t *buf, size_t bufsize, const MS3Fields *f, int big_endian)
{
  size_t sidlen = f->sid ? strlen (f->sid) : 0;
  size_t total  = MS3_FIXED_LENGTH + sidlen;
  uint64_t ratebits;

  if (bufsize < total)
    return 0;

  memset (buf, 0, total);

  buf[0] = 'M';
  buf[1] = 'S';
  buf[2] = 3;    /* format version */
  buf[3] = 0;    /* flags */

  put32 (buf + 4, f->nsec, big_endian);
  put16 (buf + 8, f->year, big_endian);
  put16 (buf + 10, f->day, big_endian);
  buf[12] = f->hour;
  buf[13] = f->min;
  buf[14] = f->sec;
  buf[15] = 0; /* encoding */

  memcpy (&ratebits, &f->samplerate, sizeof (ratebits));
  put64raw (buf + 16, ratebits, big_endian);

  put32 (buf + 24, f->numsamples, big_endian);
  put32 (buf + 28, 0, big_endian); /* CRC */
  buf[32] = f->pubversion;
  buf[33] = (uint8_t)sidlen;
  put16 (buf + 34, 0, big_endian);           /* extra header length */
  put32 (buf + 36, f->datalength, big_endian);

  if (sidlen)
    memcpy (buf + MS3_FIXED_LENGTH, f->sid, sidlen);

  return total;
} /* End of fx_ms3_fixed() */

char *
fx_write_tempfile (const char *content)
{
  char *path = strdup ("/tmp/libslink_test_XXXXXX");
  int fd;
  FILE *fp;

  if (!path)
  {
    fprintf (stderr, "fx_write_tempfile(): out of memory\n");
    abort ();
  }

  fd = mkstemp (path);

  if (fd < 0)
  {
    fprintf (stderr, "fx_write_tempfile(): mkstemp failed: %s\n", strerror (errno));
    abort ();
  }

  fp = fdopen (fd, "wb");

  if (!fp)
  {
    fprintf (stderr, "fx_write_tempfile(): fdopen failed\n");
    abort ();
  }

  if (content && fwrite (content, 1, strlen (content), fp) != strlen (content))
  {
    fprintf (stderr, "fx_write_tempfile(): short write\n");
    abort ();
  }

  fclose (fp);

  return path;
} /* End of fx_write_tempfile() */

void
fx_unlink (char *path)
{
  if (path)
  {
    remove (path);
    free (path);
  }
} /* End of fx_unlink() */

SLstream *
fx_find_stream (SLCD *slconn, const char *stationid)
{
  SLstream *cur = slconn->streams;

  while (cur)
  {
    if (strcmp (cur->stationid, stationid) == 0)
      return cur;
    cur = cur->next;
  }

  return NULL;
} /* End of fx_find_stream() */

int
fx_probe_survives (const char *argv0, const char *probe_name)
{
  pid_t pid = fork ();

  if (pid == 0)
  {
    /* Child: silence the library's own error logging for this probe,
     * then replace this process image entirely via exec(). */
    close (STDERR_FILENO);
    execl (argv0, argv0, "--probe", probe_name, (char *)NULL);
    _exit (127); /* only reached if execl() itself failed */
  }

  if (pid > 0)
  {
    int status;
    waitpid (pid, &status, 0);
    return WIFEXITED (status) && WEXITSTATUS (status) == 0;
  }

  return 0; /* fork() failed; treat as a failure to avoid a false pass */
} /* End of fx_probe_survives() */

void
fx_dispatch_probe (int argc, char **argv, const FxProbe *probes, size_t nprobes)
{
  size_t idx;

  if (argc < 3 || strcmp (argv[1], "--probe") != 0)
    return;

  for (idx = 0; idx < nprobes; idx++)
  {
    if (strcmp (argv[2], probes[idx].name) == 0)
    {
      probes[idx].fn ();
      exit (0); /* the probe itself calls _exit() if it wants a non-zero code */
    }
  }

  fprintf (stderr, "unknown probe: %s\n", argv[2]);
  exit (127);
} /* End of fx_dispatch_probe() */
