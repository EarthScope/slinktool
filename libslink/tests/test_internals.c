/***************************************************************************
 * test_internals.c: coverage for the file-static helpers in slutils.c
 * (detect(), receive_header(), update_stream()) that have no other way
 * to be unit tested directly.
 *
 * This file #includes slutils.c itself to reach its static functions.
 * It is linked against libslink.a *after* its own object, so every
 * symbol slutils.c defines is already satisfied by this translation
 * unit and slutils.o is never pulled out of the archive -- there is
 * exactly one definition of each symbol in the final binary.
 ***************************************************************************/

#include "../slutils.c"

#include "fixtures.h"
#include "slt.h"

/***** detect() *****/

static void
test_detect_ms2_with_b1000 (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel        = "BHZ";
  f.year           = 2024;
  f.day            = 216;
  f.hour           = 12;
  f.min            = 0;
  f.sec            = 0;
  f.numblockettes  = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 9 /* 2^9 = 512 */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT (payloadformat, SLPAYLOAD_MSEED2, "detect() identifies a miniSEED2 record");
  SLT_EQ_INT ((int)reclen, 512, "detect() reads the record length from the B1000 blockette");
}

static void
test_detect_ms2_without_b1000 (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel = "BHZ";
  f.year    = 2024;
  f.day     = 216;
  f.hour    = 12;
  /* numblockettes/blocketteoffset left at 0: no blockette to scan */

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);

  /* Plant a second valid fixed header at the 64-byte boundary so detect()
   * falls back to scanning for the next record. */
  fx_ms2_fixed (buf + 64, sizeof (buf) - 64, &f, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT (payloadformat, SLPAYLOAD_MSEED2, "detect() still identifies miniSEED2 without a B1000");
  SLT_EQ_INT ((int)reclen, 64, "detect() falls back to a 64-byte-boundary scan for the record length");
}

static void
test_detect_ms2_looping_blockette_chain (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);

  /* A non-1000 blockette whose "next" offset points back at (or before)
   * itself must be rejected rather than looped on forever.  The header
   * above is little-endian (matches this host), so blkt_type/next_blkt
   * are read without swapping and can be written in host-native order. */
  {
    uint16_t blkt_type = 999; /* not a B1000 */
    uint16_t next_blkt = MS2_FIXED_LENGTH; /* points at (not past) itself */

    memcpy (buf + MS2_FIXED_LENGTH, &blkt_type, 2);
    memcpy (buf + MS2_FIXED_LENGTH + 2, &next_blkt, 2);
  }

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, -1, "detect() rejects a blockette chain that loops back on itself");
}

/* The B1000 record-length field is a uint8_t exponent of two; only 6-20
 * (64 bytes to 1 MiB) are accepted as valid record lengths. */
static void
test_detect_ms2_b1000_reclen_overflow (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 32 /* out of range: >= 32 */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, -1,
             "detect() rejects an out-of-range B1000 record length exponent (too large)");
}

static void
test_detect_ms2_b1000_reclen_too_small (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 3 /* out of range: < 6 */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, -1,
             "detect() rejects an out-of-range B1000 record length exponent (too small)");
}

static void
test_detect_ms2_b1000_reclen_max_valid (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 20 /* 2^20 = 1 MiB, top of range */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, 1048576,
             "detect() accepts the top of the valid B1000 record length exponent range");
}

/* Exponents just outside the valid range (6-20) are what actually pins the
 * boundary; 3 and 32, tested above, are further out and would still be
 * correctly rejected by a guard that was mistakenly loosened to 5-21. */
static void
test_detect_ms2_b1000_reclen_just_below_min (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 5 /* one below the valid minimum, 6 */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, -1,
             "detect() rejects a B1000 record length exponent one below the valid minimum");
}

static void
test_detect_ms2_b1000_reclen_just_above_max (void)
{
  uint8_t buf[128] = {0};
  MS2Fields f;
  char payloadformat;
  int64_t reclen;

  memset (&f, 0, sizeof (f));
  f.network        = "XX";
  f.station        = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (buf, sizeof (buf), &f, 0);
  fx_ms2_b1000 (buf, sizeof (buf), MS2_FIXED_LENGTH, 11, 0, 21 /* one above the valid maximum, 20 */, 0, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT ((int)reclen, -1,
             "detect() rejects a B1000 record length exponent one above the valid maximum");
}

static void
test_detect_too_short (void)
{
  uint8_t buf[SL_MIN_PAYLOAD - 1];
  char payloadformat;

  memset (buf, 0, sizeof (buf));
  SLT_EQ_INT ((int)detect ((const char *)buf, sizeof (buf), &payloadformat), -1,
             "detect() rejects a buffer shorter than SL_MIN_PAYLOAD");
}

static void
test_detect_ms3 (void)
{
  uint8_t buf[128] = {0};
  MS3Fields f;
  char payloadformat;
  int64_t reclen;
  size_t hdrlen;

  memset (&f, 0, sizeof (f));
  f.sid        = "FDSN:XX_TEST";
  f.year       = 2024;
  f.day        = 216;
  f.hour       = 1;
  f.min        = 2;
  f.sec        = 3;
  f.samplerate = 20.0;
  f.datalength = 0;

  hdrlen = fx_ms3_fixed (buf, sizeof (buf), &f, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT (payloadformat, SLPAYLOAD_MSEED3, "detect() identifies a miniSEED3 record");
  SLT_EQ_INT ((int)reclen, (int)hdrlen, "detect() computes the miniSEED3 record length from the header fields");
}

static void
test_detect_ms3_datalength_over_16_bits (void)
{
  /* detect()'s miniSEED3 branch reads the header's 32-bit data-length
   * field through HO4u(), so a record announcing more than 64KiB of data
   * payload still gets its full, correct record length. */
  uint8_t buf[128] = {0};
  MS3Fields f;
  char payloadformat;
  int64_t reclen;
  size_t hdrlen;

  memset (&f, 0, sizeof (f));
  f.sid        = "FDSN:XX_TEST";
  f.year       = 2024;
  f.day        = 216;
  f.hour       = 1;
  f.min        = 2;
  f.sec        = 3;
  f.samplerate = 20.0;
  f.datalength = 70000; /* one past the 16-bit truncation boundary */

  hdrlen = fx_ms3_fixed (buf, sizeof (buf), &f, 0);

  reclen = detect ((const char *)buf, sizeof (buf), &payloadformat);

  SLT_EQ_INT (payloadformat, SLPAYLOAD_MSEED3, "detect() identifies a miniSEED3 record");
  SLT_EQ_INT ((int)reclen, (int)(hdrlen + f.datalength),
             "detect() computes the full miniSEED3 record length for a datalength above 65535");
}

/***** receive_header() *****/

static void
test_receive_header_v3_data (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V3 + 16] = {0}; /* padded: receive_header()'s error paths may log a few bytes past the logical header, which is always safe against the real (much larger) recvbuffer */
  int rv;

  slconn->protocol = SLPROTO3X;

  memcpy (buf, "SL", 2);
  memcpy (buf + 2, "0001A2", 6); /* hex sequence number */

  rv = receive_header (slconn, buf, sizeof (buf));

  SLT_EQ_INT (rv, SLHEADSIZE_V3, "receive_header() consumes exactly SLHEADSIZE_V3 bytes for a v3 data header");
  SLT_EQ_UINT (slconn->stat->packetinfo.seqnum, 0x0001A2, "v3 data header sequence number parsed as hex");
  SLT_EQ_INT (slconn->stat->packetinfo.payloadformat, SLPAYLOAD_UNKNOWN, "v3 data header leaves payload format unknown pending detection");

  sl_freeslcd (slconn);
}

static void
test_receive_header_v3_data_bad_hex (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V3 + 16] = {0}; /* padded: receive_header()'s error paths may log a few bytes past the logical header, which is always safe against the real (much larger) recvbuffer */

  slconn->protocol = SLPROTO3X;
  memcpy (buf, "SL", 2);
  memcpy (buf + 2, "GGGGGG", 6); /* not valid hex */

  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), -1,
             "receive_header() rejects a non-hexadecimal v3 sequence number");

  sl_freeslcd (slconn);
}

static void
test_receive_header_v3_info (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V3 + 16] = {0}; /* padded: receive_header()'s error paths may log a few bytes past the logical header, which is always safe against the real (much larger) recvbuffer */

  slconn->protocol = SLPROTO3X;
  memcpy (buf, INFOSIGNATURE, 6);
  buf[6] = ' ';
  buf[7] = '*'; /* continuation flag */

  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), SLHEADSIZE_V3, "v3 INFO header (continued) parsed");
  SLT_EQ_INT (slconn->stat->packetinfo.payloadformat, SLPAYLOAD_MSEED2INFO, "the '*' flag marks a continued INFO response");
  SLT_EQ_UINT (slconn->stat->packetinfo.seqnum, SL_UNSETSEQUENCE, "INFO packets have no sequence number");

  buf[7] = ' '; /* terminated */
  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), SLHEADSIZE_V3, "v3 INFO header (terminated) parsed");
  SLT_EQ_INT (slconn->stat->packetinfo.payloadformat, SLPAYLOAD_MSEED2INFOTERM, "a non-'*' flag marks the final INFO response");

  sl_freeslcd (slconn);
}

static void
test_receive_header_v3_bad_signature (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V3 + 16] = {0};

  slconn->protocol = SLPROTO3X;
  memcpy (buf, "XX", 2);

  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), -1, "an unrecognized v3 header signature is rejected");

  sl_freeslcd (slconn);
}

static void
test_receive_header_v4 (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V4 + 16] = {0};
  uint32_t payloadlength = 1234;
  uint64_t seqnum        = 9876543210ULL;
  uint8_t stationidlen   = 7;
  int i;

  slconn->protocol = SLPROTO40;
  memcpy (buf, SIGNATURE_V4, 2);
  buf[2] = SLPAYLOAD_MSEED3;
  buf[3] = 0;

  /* v4 numeric fields are wire-native (little-endian); write the bytes
   * explicitly so this test doesn't depend on the host's own byte order. */
  for (i = 0; i < 4; i++)
    buf[4 + i] = (uint8_t)(payloadlength >> (8 * i));
  for (i = 0; i < 8; i++)
    buf[8 + i] = (uint8_t)(seqnum >> (8 * i));
  buf[16] = stationidlen;

  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), SLHEADSIZE_V4, "v4 header consumes exactly SLHEADSIZE_V4 bytes");
  SLT_EQ_INT (slconn->stat->packetinfo.payloadformat, SLPAYLOAD_MSEED3, "v4 payload format field read");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadlength, payloadlength, "v4 payload length field read");
  SLT_EQ_UINT (slconn->stat->packetinfo.seqnum, seqnum, "v4 sequence number field read");
  SLT_EQ_UINT (slconn->stat->packetinfo.stationidlength, stationidlen, "v4 station id length field read");

  sl_freeslcd (slconn);
}

static void
test_receive_header_v4_bad_signature (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V4 + 16] = {0};

  slconn->protocol = SLPROTO40;
  memcpy (buf, "XX", 2);

  SLT_EQ_INT (receive_header (slconn, buf, sizeof (buf)), -1, "an unrecognized v4 header signature is rejected");

  sl_freeslcd (slconn);
}

static void
test_receive_header_insufficient_bytes (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[SLHEADSIZE_V3 + 16] = {0};

  slconn->protocol = SLPROTO3X;
  memcpy (buf, "SL000000", SLHEADSIZE_V3);

  SLT_EQ_INT (receive_header (slconn, buf, SLHEADSIZE_V3 - 1), -1,
             "fewer bytes available than the protocol's header size is rejected");

  sl_freeslcd (slconn);
}

/***** receive_payload() *****/

static void
test_receive_payload_v4_zero_length (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t wire[128]  = {0};
  char    plbuffer[128] = {0};
  int64_t rv;

  slconn->protocol = SLPROTO40;
  slconn->stat->packetinfo.payloadlength    = 0;
  slconn->stat->packetinfo.payloadcollected = 0;

  rv = receive_payload (slconn, plbuffer, sizeof (plbuffer), wire, sizeof (wire));

  SLT_EQ_INT ((int)rv, 0, "a v4 header declaring a 0-byte payload consumes nothing");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadcollected, 0,
              "payloadcollected stays 0 for a declared 0-byte v4 payload");

  sl_freeslcd (slconn);
}

static void
test_receive_payload_v3_detects_length (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t wire[128]  = {0};
  char    plbuffer[128] = {0};
  MS2Fields f;
  int64_t rv;

  memset (&f, 0, sizeof (f));
  f.network         = "XX";
  f.station         = "TEST";
  f.channel         = "BHZ";
  f.year            = 2024;
  f.day             = 216;
  f.numblockettes   = 1;
  f.blocketteoffset = MS2_FIXED_LENGTH;

  fx_ms2_fixed (wire, sizeof (wire), &f, 0);
  fx_ms2_b1000 (wire, sizeof (wire), MS2_FIXED_LENGTH, 11, 0, 9 /* 2^9 = 512 */, 0, 0);

  slconn->protocol = SLPROTO3X;
  slconn->stat->packetinfo.payloadlength    = 0;
  slconn->stat->packetinfo.payloadcollected = 0;

  rv = receive_payload (slconn, plbuffer, sizeof (plbuffer), wire, sizeof (wire));

  SLT_EQ_INT ((int)rv, sizeof (wire),
             "once detected, a v3 payload consumes all of what's available toward it");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadcollected, sizeof (wire),
              "payloadcollected tracks the bytes consumed");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadlength, 512,
              "payloadlength is set from the record length detected in the B1000 blockette");
  SLT_EQ_INT (slconn->stat->packetinfo.payloadformat, SLPAYLOAD_MSEED2,
             "payloadformat is set once detected");

  sl_freeslcd (slconn);
}

static void
test_receive_payload_v3_no_b1000_multi_chunk (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t wire[128]     = {0};
  char    plbuffer[64]  = {0}; /* Exactly the true record length, no headroom */
  MS2Fields f;
  int64_t rv;

  memset (&f, 0, sizeof (f));
  f.network = "XX";
  f.station = "TEST";
  f.channel = "BHZ";
  f.year    = 2024;
  f.day     = 216;
  /* numblockettes/blocketteoffset left at 0: no B1000 to read a length from */

  fx_ms2_fixed (wire, sizeof (wire), &f, 0);

  /* A second valid fixed header at the 64-byte boundary marks the true end
   * of the first record, for detect()'s fallback scan to find. */
  fx_ms2_fixed (wire + 64, sizeof (wire) - 64, &f, 0);

  slconn->protocol = SLPROTO3X;
  slconn->stat->packetinfo.payloadlength    = 0;
  slconn->stat->packetinfo.payloadcollected = 0;

  /* First chunk: below SL_MIN_PAYLOAD, and too little of the buffer for
   * detect() to see the next record's header. Nothing is copied or
   * consumed while waiting for more to arrive. */
  rv = receive_payload (slconn, plbuffer, sizeof (plbuffer), wire, 40);

  SLT_EQ_INT ((int)rv, 0, "an undetectable chunk consumes nothing rather than guessing");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadlength, 0,
              "payload length stays unknown until the next header is visible");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadcollected, 0,
              "nothing is collected while length detection is pending");

  /* Second chunk: the rest of the buffer has arrived, exposing the next
   * record's header at offset 64 to detect()'s fallback scan. */
  rv = receive_payload (slconn, plbuffer, sizeof (plbuffer), wire, sizeof (wire));

  SLT_EQ_INT ((int)rv, 64, "the record is consumed up to its detected length, not beyond it");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadlength, 64,
              "payload length comes from the offset of the following record's header");
  SLT_EQ_UINT (slconn->stat->packetinfo.payloadcollected, 64,
              "payloadcollected lands exactly on the detected length, with no overshoot");

  sl_freeslcd (slconn);
}

/***** update_stream() *****/

static void
setup_ms2_payload (uint8_t *buf, size_t bufsize, const char *net, const char *sta,
                   const char *loc, const char *chan, uint64_t seqnum,
                   SLpacketinfo *pi, uint8_t stationidlength, const char *stationid)
{
  MS2Fields f;

  memset (&f, 0, sizeof (f));
  f.network       = net;
  f.station       = sta;
  f.location      = loc;
  f.channel       = chan;
  f.year          = 2024;
  f.day           = 216;
  f.hour          = 10;
  f.min           = 20;
  f.sec           = 30;
  f.samprate_fact = 20;
  f.samprate_mult = 1;

  fx_ms2_fixed (buf, bufsize, &f, 0);

  memset (pi, 0, sizeof (*pi));
  pi->seqnum           = seqnum;
  pi->payloadformat    = SLPAYLOAD_MSEED2;
  pi->payloadlength    = MS2_FIXED_LENGTH;
  pi->stationidlength  = stationidlength;

  if (stationid)
    strcpy (pi->stationid, stationid);
}

static void
test_update_stream_multistation_match (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[64] = {0};
  SLstream *s;

  sl_add_stream (slconn, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);
  sl_add_stream (slconn, "XX_TST2", "BHZ", SL_UNSETSEQUENCE, NULL);

  setup_ms2_payload (buf, sizeof (buf), "XX", "TEST", "00", "BHZ", 555,
                     &slconn->stat->packetinfo, 7, "XX_TEST");

  SLT_EQ_INT (update_stream (slconn, (const char *)buf), 0, "update_stream() finds a matching stream");

  s = slconn->streams;
  while (s && strcmp (s->stationid, "XX_TEST") != 0)
    s = s->next;

  SLT_NOT_NULL (s, "XX_TEST is still in the list");
  SLT_EQ_UINT (s->seqnum, 555, "the matching stream's sequence number is updated");
  SLT_ASSERT (strlen (s->timestamp) > 0, "the matching stream's timestamp is updated");

  sl_freeslcd (slconn);
}

static void
test_update_stream_no_match (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[64] = {0};

  sl_add_stream (slconn, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);

  setup_ms2_payload (buf, sizeof (buf), "XX", "NONE", "00", "BHZ", 1,
                     &slconn->stat->packetinfo, 7, "XX_NONE");

  SLT_EQ_INT (update_stream (slconn, (const char *)buf), -1, "update_stream() reports no match for an unconfigured station");
  SLT_EQ_UINT (slconn->streams->seqnum, SL_UNSETSEQUENCE, "the unrelated configured stream is left untouched");

  sl_freeslcd (slconn);
}

static void
test_update_stream_all_station (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[64] = {0};

  sl_set_allstation_params (slconn, NULL, SL_UNSETSEQUENCE, NULL);

  setup_ms2_payload (buf, sizeof (buf), "XX", "ANY", "00", "BHZ", 777,
                     &slconn->stat->packetinfo, 6, "XX_ANY");

  SLT_EQ_INT (update_stream (slconn, (const char *)buf), 0, "update_stream() always matches in all-station mode");
  SLT_EQ_UINT (slconn->streams->seqnum, 777, "the all-station entry's sequence number is updated");

  sl_freeslcd (slconn);
}

static void
test_update_stream_v3_stationid_extraction (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[64] = {0};

  sl_add_stream (slconn, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);

  /* stationidlength == 0 simulates the v3 case: the header carries no
   * station id, so it must be extracted from the payload's FDSN source id. */
  setup_ms2_payload (buf, sizeof (buf), "XX", "TEST", "00", "BHZ", 42,
                     &slconn->stat->packetinfo, 0, NULL);

  SLT_EQ_INT (update_stream (slconn, (const char *)buf), 0, "update_stream() extracts the station id from the payload when absent");
  SLT_EQ_STR (slconn->stat->packetinfo.stationid, "XX_TEST", "the extracted station id is NET_STA from the FDSN source id");
  SLT_EQ_UINT (slconn->streams->seqnum, 42, "the stream matched via the extracted station id is updated");

  sl_freeslcd (slconn);
}

static void
test_update_stream_info_packet_skipped (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  uint8_t buf[64] = {0};

  sl_add_stream (slconn, "XX_TEST", "BHZ", 100, NULL);

  slconn->stat->packetinfo.payloadformat = SLPAYLOAD_MSEED2INFO;

  SLT_EQ_INT (update_stream (slconn, (const char *)buf), 0, "update_stream() is a no-op for INFO packets");
  SLT_EQ_UINT (slconn->streams->seqnum, 100, "an INFO packet does not disturb existing stream state");
  SLT_EQ_STR (slconn->streams->timestamp, "", "an INFO packet does not disturb the stream's timestamp either");

  sl_freeslcd (slconn);
}

int
main (void)
{
  SLT_RUN (test_detect_ms2_with_b1000);
  SLT_RUN (test_detect_ms2_without_b1000);
  SLT_RUN (test_detect_ms2_looping_blockette_chain);
  SLT_RUN (test_detect_ms2_b1000_reclen_overflow);
  SLT_RUN (test_detect_ms2_b1000_reclen_too_small);
  SLT_RUN (test_detect_ms2_b1000_reclen_max_valid);
  SLT_RUN (test_detect_ms2_b1000_reclen_just_below_min);
  SLT_RUN (test_detect_ms2_b1000_reclen_just_above_max);
  SLT_RUN (test_detect_too_short);
  SLT_RUN (test_detect_ms3);
  SLT_RUN (test_detect_ms3_datalength_over_16_bits);

  SLT_RUN (test_receive_header_v3_data);
  SLT_RUN (test_receive_header_v3_data_bad_hex);
  SLT_RUN (test_receive_header_v3_info);
  SLT_RUN (test_receive_header_v3_bad_signature);
  SLT_RUN (test_receive_header_v4);
  SLT_RUN (test_receive_header_v4_bad_signature);
  SLT_RUN (test_receive_header_insufficient_bytes);

  SLT_RUN (test_receive_payload_v4_zero_length);
  SLT_RUN (test_receive_payload_v3_detects_length);
  SLT_RUN (test_receive_payload_v3_no_b1000_multi_chunk);

  SLT_RUN (test_update_stream_multistation_match);
  SLT_RUN (test_update_stream_no_match);
  SLT_RUN (test_update_stream_all_station);
  SLT_RUN (test_update_stream_v3_stationid_extraction);
  SLT_RUN (test_update_stream_info_packet_skipped);

  return SLT_REPORT ();
}
