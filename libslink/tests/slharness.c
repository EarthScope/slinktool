/***************************************************************************
 * slharness.c:
 *
 * A small, deterministic SeedLink client used only by test_protocol.py
 * and test_tls.py.  It drives sl_collect() against a mock server and
 * prints one machine-readable line per event on stdout:
 *
 *   LOG <message>                 -- any library log/diagnostic/error message
 *   PACKET seq=<n> format=<c> subformat=<c> station=<id> length=<n> summary="..."
 *   PAYLOAD seq=<n> b64=<...>     -- raw payload bytes, base64-encoded (see below)
 *   CAP <flag>=<0|1>              -- result of an sl_hascapability() check
 *   PING serverid="..." site="..." status=<n>
 *   RESULT <sl_collect() return code, or TIMEOUT/MAXPACKETS>
 *
 * sl_payload_summary() only parses miniSEED 2/3 payloads, so "summary="
 * is empty for any other payload format (INFO, JSON, opaque, ...); the
 * PAYLOAD line carries every packet's raw bytes so a test can inspect
 * non-miniSEED content without fighting the PACKET line's quoting.
 *
 * This is test infrastructure, not library code or an example program;
 * it intentionally trades generality for being easy to drive from a
 * subprocess with exact, greppable output.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libslink.h"

#if defined(SLP_WIN)
static int
harness_setenv (const char *name, const char *value, int overwrite)
{
  if (!overwrite && getenv (name) != NULL)
    return 0;
  return _putenv_s (name, value) == 0 ? 0 : -1;
}
#else
#define harness_setenv setenv
#endif

#define MAX_STATIONS 16
#define MAX_CAP_CHECKS 16

static char g_authvalue[256] = {0};
static int  g_auth_null      = 0;
static int  g_force_tls      = 0;
static int  g_verbosity      = 1;

static char     g_station_ids[MAX_STATIONS][32];
static char     g_station_sel[MAX_STATIONS][64];
static char     g_station_ts[MAX_STATIONS][40];
static uint64_t g_station_seq[MAX_STATIONS];
static int      g_station_count = 0;

static const char *g_allstation_selectors = NULL;
static int          g_allstation_set       = 0;
static int          g_allstation_seqall    = 0;
static uint64_t     g_allstation_seq       = SL_UNSETSEQUENCE;

static void
harness_log (const char *msg)
{
  size_t len = msg ? strlen (msg) : 0;

  printf ("LOG %s", msg ? msg : "");
  if (len == 0 || msg[len - 1] != '\n')
    printf ("\n");
  fflush (stdout);
}

static const char *
harness_auth_value (const char *server, void *auth_data)
{
  (void)server;
  (void)auth_data;

  if (g_auth_null)
    return NULL; /* simulate a callback with no credentials available */

  return g_authvalue;
}

static void
usage (const char *prog)
{
  fprintf (stderr, "Usage: %s --address HOST:PORT [options]\n", prog);
}

/* Base64-encode `len` bytes of `data` into `out`, which must be at least
 * BASE64_BUFSIZE bytes (sized for SL_RECV_BUFFER_SIZE, the largest
 * payload sl_collect() can deliver). Used to dump a packet's raw
 * payload on its own output line -- see the PAYLOAD line below --
 * since payload bytes (JSON, XML, opaque) can contain quotes or
 * newlines that would break the quoted "summary=" field of the PACKET
 * line. */
#define BASE64_BUFSIZE (((SL_RECV_BUFFER_SIZE + 2) / 3) * 4 + 1)

static void
base64_encode (const unsigned char *data, size_t len, char *out)
{
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t i, o = 0;

  for (i = 0; i + 2 < len; i += 3)
  {
    uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out[o++]   = table[(v >> 18) & 0x3F];
    out[o++]   = table[(v >> 12) & 0x3F];
    out[o++]   = table[(v >> 6) & 0x3F];
    out[o++]   = table[v & 0x3F];
  }

  if (i < len)
  {
    uint32_t v   = (uint32_t)data[i] << 16;
    int      rem = (int)(len - i);

    if (rem == 2)
      v |= (uint32_t)data[i + 1] << 8;

    out[o++] = table[(v >> 18) & 0x3F];
    out[o++] = table[(v >> 12) & 0x3F];
    out[o++] = (rem == 2) ? table[(v >> 6) & 0x3F] : '=';
    out[o++] = '=';
  }

  out[o] = '\0';
}

/* Parse "STATIONID[:SELECTORS[:SEQNUM[:TIMESTAMP]]]" into the next slot
 * of the g_station_* arrays. */
static void
add_station_spec (const char *arg)
{
  char spec[256];
  char *stationid, *selectors = NULL, *seqstr = NULL, *tsstr = NULL, *cp;
  int idx = g_station_count;

  if (idx >= MAX_STATIONS)
  {
    fprintf (stderr, "too many --station options (max %d)\n", MAX_STATIONS);
    exit (2);
  }

  strncpy (spec, arg, sizeof (spec) - 1);
  spec[sizeof (spec) - 1] = '\0';

  stationid = spec;
  if ((cp = strchr (stationid, ':')))
  {
    *cp       = '\0';
    selectors = cp + 1;
    if ((cp = strchr (selectors, ':')))
    {
      *cp    = '\0';
      seqstr = cp + 1;
      if ((cp = strchr (seqstr, ':')))
      {
        *cp   = '\0';
        tsstr = cp + 1;
      }
    }
  }

  strncpy (g_station_ids[idx], stationid, sizeof (g_station_ids[0]) - 1);
  g_station_sel[idx][0] = '\0';
  g_station_ts[idx][0]  = '\0';
  g_station_seq[idx]    = SL_UNSETSEQUENCE;

  if (selectors && *selectors)
    strncpy (g_station_sel[idx], selectors, sizeof (g_station_sel[0]) - 1);
  if (tsstr && *tsstr)
    strncpy (g_station_ts[idx], tsstr, sizeof (g_station_ts[0]) - 1);
  if (seqstr && *seqstr)
    g_station_seq[idx] = (strcmp (seqstr, "ALL") == 0) ? SL_ALLDATASEQUENCE
                                                        : strtoull (seqstr, NULL, 10);

  g_station_count++;
}

int
main (int argc, char **argv)
{
  SLCD *slconn;
  char plbuffer[SL_RECV_BUFFER_SIZE];
  const SLpacketinfo *packetinfo;
  char summary[256];

  const char *address      = NULL;
  const char *statefile     = NULL;
  const char *infolevel     = NULL;
  const char *authenv_user  = NULL;
  const char *authenv_pass  = NULL;
  const char *timewindow_start = NULL;
  const char *timewindow_end   = NULL;
  const char *cap_checks[MAX_CAP_CHECKS];
  int cap_check_count        = 0;
  int max_packets            = 20;
  double timeout_seconds     = 10.0;
  int protocol                = 0; /* 0 = unset/auto */
  int nonblock                = 0;
  int dialup                  = 0;
  int batch                   = 0;
  int stop_on_nopacket        = 0;
  int do_ping                 = 0;
  int keepalive                = 0;
  int iotimeout                 = -1;
  int idletimeout               = -1;
  int reconnectdelay            = -1;
  int resume_flag                = -1;
  int lastpkttime_flag            = -1;
  int have_auth                    = 0;
  int argi;

  for (argi = 1; argi < argc; argi++)
  {
    const char *a = argv[argi];

    if (strcmp (a, "--address") == 0 && argi + 1 < argc)
      address = argv[++argi];
    else if (strcmp (a, "--v3") == 0)
      protocol = SLPROTO3X;
    else if (strcmp (a, "--v4") == 0)
      protocol = SLPROTO40;
    else if (strcmp (a, "--max-packets") == 0 && argi + 1 < argc)
      max_packets = atoi (argv[++argi]);
    else if (strcmp (a, "--timeout-seconds") == 0 && argi + 1 < argc)
      timeout_seconds = atof (argv[++argi]);
    else if (strcmp (a, "--nonblock") == 0)
      nonblock = 1;
    else if (strcmp (a, "--stop-on-nopacket") == 0)
      stop_on_nopacket = 1;
    else if (strcmp (a, "--dialup") == 0)
      dialup = 1;
    else if (strcmp (a, "--batch") == 0)
      batch = 1;
    else if (strcmp (a, "--keepalive") == 0 && argi + 1 < argc)
      keepalive = atoi (argv[++argi]);
    else if (strcmp (a, "--iotimeout") == 0 && argi + 1 < argc)
      iotimeout = atoi (argv[++argi]);
    else if (strcmp (a, "--idletimeout") == 0 && argi + 1 < argc)
      idletimeout = atoi (argv[++argi]);
    else if (strcmp (a, "--reconnectdelay") == 0 && argi + 1 < argc)
      reconnectdelay = atoi (argv[++argi]);
    else if (strcmp (a, "--no-resume") == 0)
      resume_flag = 0;
    else if (strcmp (a, "--no-lastpkttime") == 0)
      lastpkttime_flag = 0;
    else if (strcmp (a, "--info") == 0 && argi + 1 < argc)
      infolevel = argv[++argi];
    else if (strcmp (a, "--statefile") == 0 && argi + 1 < argc)
      statefile = argv[++argi];
    else if (strcmp (a, "--station") == 0 && argi + 1 < argc)
      add_station_spec (argv[++argi]);
    else if (strcmp (a, "--allstation") == 0 && argi + 1 < argc)
    {
      g_allstation_selectors = argv[++argi];
      g_allstation_set       = 1;
    }
    else if (strcmp (a, "--seq-all") == 0)
      g_allstation_seqall = 1;
    else if (strcmp (a, "--allstation-seq") == 0 && argi + 1 < argc)
      g_allstation_seq = strtoull (argv[++argi], NULL, 10);
    else if (strcmp (a, "--time-start") == 0 && argi + 1 < argc)
      timewindow_start = argv[++argi];
    else if (strcmp (a, "--time-end") == 0 && argi + 1 < argc)
      timewindow_end = argv[++argi];
    else if (strcmp (a, "--auth-value") == 0 && argi + 1 < argc)
    {
      strncpy (g_authvalue, argv[++argi], sizeof (g_authvalue) - 1);
      have_auth = 1;
    }
    else if (strcmp (a, "--auth-null") == 0)
    {
      g_auth_null = 1;
      have_auth   = 1;
    }
    else if (strcmp (a, "--auth-env") == 0 && argi + 1 < argc)
    {
      char *cp;
      authenv_user = argv[++argi];
      if ((cp = strchr (authenv_user, ':')))
      {
        *cp          = '\0';
        authenv_pass = cp + 1;
      }
    }
    else if (strcmp (a, "--ca-file") == 0 && argi + 1 < argc)
      harness_setenv ("LIBSLINK_CA_CERT_FILE", argv[++argi], 1);
    else if (strcmp (a, "--ca-path") == 0 && argi + 1 < argc)
      harness_setenv ("LIBSLINK_CA_CERT_PATH", argv[++argi], 1);
    else if (strcmp (a, "--tls") == 0)
      g_force_tls = 1;
    else if (strcmp (a, "--cap") == 0 && argi + 1 < argc)
    {
      if (cap_check_count >= MAX_CAP_CHECKS)
      {
        fprintf (stderr, "too many --cap options (max %d)\n", MAX_CAP_CHECKS);
        exit (2);
      }
      cap_checks[cap_check_count++] = argv[++argi];
    }
    else if (strcmp (a, "--ping") == 0)
      do_ping = 1;
    else if (strcmp (a, "--verbose") == 0)
      g_verbosity = 2;
    else
    {
      usage (argv[0]);
      fprintf (stderr, "Unknown or incomplete option: %s\n", a);
      return 2;
    }
  }

  if (!address)
  {
    usage (argv[0]);
    return 2;
  }

  slconn = sl_initslcd ("slharness", "1.0");

  sl_loginit_r (slconn, g_verbosity, harness_log, "", harness_log, "");

  sl_set_serveraddress (slconn, address);

  if (g_force_tls)
    sl_set_tlsmode (slconn, 1);

  if (protocol)
    sl_set_protocol (slconn, (LIBPROTOCOL)protocol);

  if (keepalive > 0)
    sl_set_keepalive (slconn, keepalive);
  if (iotimeout >= 0)
    sl_set_iotimeout (slconn, iotimeout);
  if (idletimeout >= 0)
    sl_set_idletimeout (slconn, idletimeout);
  if (reconnectdelay >= 0)
    sl_set_reconnectdelay (slconn, reconnectdelay);
  if (nonblock)
    sl_set_blockingmode (slconn, 1);
  if (dialup)
    sl_set_dialupmode (slconn, 1);
  if (batch)
    sl_set_batchmode (slconn, 1);
  if (resume_flag == 0)
    slconn->resume = 0;
  if (lastpkttime_flag == 0)
    slconn->lastpkttime = 0;

  if (have_auth)
    sl_set_auth_params (slconn, harness_auth_value, NULL, NULL);
  if (authenv_user && authenv_pass)
    sl_set_auth_envvars (slconn, authenv_user, authenv_pass);

  if (g_allstation_set)
  {
    sl_set_allstation_params (slconn,
                              (g_allstation_selectors && *g_allstation_selectors) ? g_allstation_selectors : NULL,
                              (g_allstation_seqall) ? SL_ALLDATASEQUENCE : g_allstation_seq, NULL);
  }

  if (timewindow_start || timewindow_end)
    sl_set_timewindow (slconn, timewindow_start, timewindow_end);

  {
    int i;
    for (i = 0; i < g_station_count; i++)
    {
      sl_add_stream (slconn, g_station_ids[i],
                    (g_station_sel[i][0]) ? g_station_sel[i] : NULL,
                    g_station_seq[i],
                    (g_station_ts[i][0]) ? g_station_ts[i] : NULL);
    }
  }

  if (statefile)
    sl_recoverstate (slconn, statefile);

  if (infolevel)
    sl_request_info (slconn, infolevel);

  if (do_ping)
  {
    char serverid[100] = {0};
    char site[100]     = {0};
    int rv             = sl_ping (slconn, serverid, site);
    char *cp;

    /* sl_recvresp() does not strip the trailing CRLF, and sl_ping() copies
     * its buffers verbatim; trim so the output stays one clean line. */
    if ((cp = strpbrk (serverid, "\r\n")))
      *cp = '\0';
    if ((cp = strpbrk (site, "\r\n")))
      *cp = '\0';

    printf ("PING serverid=\"%s\" site=\"%s\" status=%d\n", serverid, site, rv);
    fflush (stdout);

    sl_freeslcd (slconn);
    return 0;
  }

  {
    int64_t start_ns  = sl_nstime ();
    int packets_seen  = 0;
    int result;

    for (;;)
    {
      double elapsed = (double)(sl_nstime () - start_ns) / 1e9;

      if (elapsed > timeout_seconds)
      {
        printf ("RESULT TIMEOUT\n");
        fflush (stdout);
        break;
      }

      result = sl_collect (slconn, &packetinfo, plbuffer, sizeof (plbuffer));

      if (result == SLPACKET)
      {
        if (packetinfo->payloadformat == SLPAYLOAD_MSEED2 ||
            packetinfo->payloadformat == SLPAYLOAD_MSEED3)
        {
          sl_payload_summary (slconn->log, packetinfo, plbuffer, packetinfo->payloadlength,
                              summary, sizeof (summary));
        }
        else
        {
          summary[0] = '\0';
        }

        printf ("PACKET seq=%llu format=%c subformat=%c station=%s length=%u summary=\"%s\"\n",
               (unsigned long long)packetinfo->seqnum,
               (packetinfo->payloadformat >= 32 && packetinfo->payloadformat < 127) ? packetinfo->payloadformat : '?',
               (packetinfo->payloadsubformat >= 32 && packetinfo->payloadsubformat < 127) ? packetinfo->payloadsubformat : '?',
               packetinfo->stationid, packetinfo->payloadlength, summary);

        {
          static char b64buf[BASE64_BUFSIZE];

          base64_encode ((const unsigned char *)plbuffer, packetinfo->payloadlength, b64buf);
          printf ("PAYLOAD seq=%llu b64=%s\n",
                 (unsigned long long)packetinfo->seqnum, b64buf);
        }
        fflush (stdout);

        packets_seen++;

        if (packets_seen >= max_packets)
        {
          printf ("RESULT MAXPACKETS\n");
          fflush (stdout);
          break;
        }
      }
      else if (result == SLNOPACKET)
      {
        if (stop_on_nopacket)
        {
          printf ("RESULT %d\n", result);
          fflush (stdout);
          break;
        }
      }
      else
      {
        printf ("RESULT %d\n", result);
        fflush (stdout);
        break;
      }
    }
  }

  if (statefile)
    sl_savestate (slconn, statefile);

  {
    int i;
    for (i = 0; i < cap_check_count; i++)
      printf ("CAP %s=%d\n", cap_checks[i], sl_hascapability (slconn, (char *)cap_checks[i]) > 0);
  }
  fflush (stdout);

  sl_disconnect (slconn);
  sl_freeslcd (slconn);

  return 0;
}
