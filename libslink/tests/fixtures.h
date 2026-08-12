/***************************************************************************
 * fixtures.h:
 *
 * Synthetic miniSEED 2 and 3 record builders, and small filesystem
 * helpers, shared across the C test binaries.
 *
 * All build functions write raw header bytes only; no data samples are
 * generated since sl_payload_info()/detect() never inspect sample data.
 ***************************************************************************/

#ifndef SLTEST_FIXTURES_H
#define SLTEST_FIXTURES_H 1

#include <stddef.h>
#include <stdint.h>

#include "libslink.h"

#define MS2_FIXED_LENGTH 48
#define MS2_B1000_LENGTH 8
#define MS3_FIXED_LENGTH 40

/** Fields for a synthetic miniSEED 2 fixed header */
typedef struct
{
  const char *network;  /* up to 2 chars */
  const char *station;  /* up to 5 chars */
  const char *location; /* up to 2 chars */
  const char *channel;  /* up to 3 chars */
  uint16_t    year;
  uint16_t    day;
  uint8_t     hour;
  uint8_t     min;
  uint8_t     sec;
  uint16_t    fsec;
  uint16_t    numsamples;
  int16_t     samprate_fact;
  int16_t     samprate_mult;
  uint8_t     numblockettes;
  uint16_t    dataoffset;
  uint16_t    blocketteoffset;
} MS2Fields;

/** Fields for a synthetic miniSEED 3 fixed header */
typedef struct
{
  const char *sid;
  uint16_t    year;
  uint16_t    day;
  uint8_t     hour;
  uint8_t     min;
  uint8_t     sec;
  uint32_t    nsec;
  double      samplerate;
  uint32_t    numsamples;
  uint8_t     pubversion;
  uint32_t    datalength;
} MS3Fields;

/* Write a MS2_FIXED_LENGTH-byte fixed header into buf (bufsize >= 48).
 * If big_endian is non-zero, multi-byte fields are written in big-endian
 * (network) order; otherwise little-endian. */
void fx_ms2_fixed (uint8_t *buf, size_t bufsize, const MS2Fields *f, int big_endian);

/* Write an 8-byte B1000 blockette at buf+offset (bufsize permitting).
 * reclen_pow2 is the record length exponent, e.g. 9 for a 512-byte record. */
void fx_ms2_b1000 (uint8_t *buf, size_t bufsize, size_t offset,
                   uint8_t encoding, uint8_t byteorder, uint8_t reclen_pow2,
                   uint16_t next, int big_endian);

/* Write a MS3_FIXED_LENGTH + strlen(f->sid)-byte header into buf.
 * Returns the total header length written, or 0 if buf is too small. */
size_t fx_ms3_fixed (uint8_t *buf, size_t bufsize, const MS3Fields *f, int big_endian);

/* Create a temp file with the given content, returning a path the caller
 * must fx_unlink().  Aborts the process on failure (test fixture, not
 * library code, so a hard failure here is a test infrastructure bug). */
char *fx_write_tempfile (const char *content);

/* Remove a file created by fx_write_tempfile() and free the path. */
void fx_unlink (char *path);

/* Find a stream by station id in slconn->streams, or NULL if not present. */
SLstream *fx_find_stream (SLCD *slconn, const char *stationid);

/* A named crash probe, run in a fork+exec'd copy of the calling binary
 * (see fx_probe_survives()) so a regression reports a clean TAP failure
 * for one test instead of crashing the whole binary. */
typedef void (*FxProbeFn) (void);

typedef struct
{
  const char *name;
  FxProbeFn   fn;
} FxProbe;

/* Run the named probe in a fork+exec'd copy of argv0 -- a fresh process
 * image, so nothing the parent's allocator (sanitizer-instrumented or
 * not) was doing is ever inherited into it, unlike a bare fork() of a
 * process that has already done real allocator work.
 *
 * Returns 1 if the probe exited cleanly (code 0), 0 if it crashed, exited
 * non-zero, or fork() itself failed. */
int fx_probe_survives (const char *argv0, const char *probe_name);

/* If invoked as "<self> --probe NAME", run the matching entry of probes[]
 * and exit -- this call does not return in that case. Otherwise returns
 * normally so main() can continue with its regular test run. Call this
 * first thing in main(), before parsing any other arguments. */
void fx_dispatch_probe (int argc, char **argv, const FxProbe *probes, size_t nprobes);

#endif /* SLTEST_FIXTURES_H */
