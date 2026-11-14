/***************************************************************************
 * A SeedLink client for data stream inspection, data collection and server
 * testing.
 *
 * Connects to a SeedLink server, configures a connection in uni-station
 * or multi-station mode and collects data and/or server details (INFO).  Detailed
 * information about the data received can be printed and the data can
 * be saved to files.
 *
 * Written by Chad Trabant,
 *   ORFEUS/EC-Project MEREDIAN
 *   IRIS Data Management Center
 *   EarthScope Data Services
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

#include <libmseed.h>
#include <libslink.h>

#include "slinkinfo.h"

#define PACKAGE "slinktool"
#define VERSION "5.0.0"

static uint8_t verbose     = 0;    /* flag to control general verbosity */
static uint8_t formatlevel = 0;    /* flag to control format verbosity */
static uint8_t pingonly    = 0;    /* flag to control ping function */
static uint8_t ppackets    = 0;    /* flag to control printing of data packets */
static uint8_t psamples    = 0;    /* flag to control printing of data samples */
static int stateint        = 0;    /* packet interval to save statefile */
static char *statefile     = NULL; /* state file for saving/restoring the seq. no. */
static char *dumpfile      = NULL; /* output file for data dump */
static FILE *outfile       = NULL; /* the descriptor for the dumpfile */
static FILE *infofile      = NULL; /* destination for INFO output */

static SLCD *slconn; /* connection parameters */

static char plbuffer[10485760]; /* 10 MiB payload buffer */

/* Query types */
static enum {
  INFO_REQUEST_NONE,
  INFO_REQUEST_RAW,
  INFO_REQUEST_FORMAT
} info_request = INFO_REQUEST_NONE;

/* Functions internal to this source file */
static void packet_handler (SLCD *slconn, const SLpacketinfo *packetinfo,
                            const char *payload, uint32_t payloadlength);
static int info_handler_mseed (MS3Record *msr, int terminate);
static int read_input (char *buffer, size_t size, int noecho);
static const char *auth_value_userpass (const char *server, void *data);
static const char *auth_value_token (const char *server, void *data);
static void auth_finish (const char *server, void *data);
static int parameter_proc (SLCD *slconn, int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static int normalize_time (const char *input, char *output, size_t outputsize);
static void print_samples (MS3Record *msr, int maxlines);
static int ping_server (SLCD *slconn);
static void print_stderr (const char *message);
static int print_json (const char *json, uint32_t jsonlength, int indent);
static void usage (void);

static char auth_buffer[1024] = {0};

int
main (int argc, char **argv)
{
  const SLpacketinfo *packetinfo = NULL;
  int status;
  int exitstatus = 0;

  uint64_t packetcnt = 0;

  /* Allocate and initialize a new connection description */
  slconn = sl_initslcd (PACKAGE, VERSION);

  /* Configure authentication via SEEDLINK_USERNAME and SEEDLINK_PASSWORD
   * environment variables if they are set */
  if (getenv ("SEEDLINK_USERNAME") && getenv ("SEEDLINK_PASSWORD"))
  {
    sl_set_auth_envvars (slconn, "SEEDLINK_USERNAME", "SEEDLINK_PASSWORD");
  }

  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (slconn, argc, argv) < 0)
  {
    sl_log (2, 0, "parameter processing failed.\n");
    return -1;
  }

  /* Set signal handlers to trigger clean connection shutdown */
  if (sl_set_termination_handler (slconn) < 0)
  {
    sl_log (2, 0, "Failed to set termination handler\n");
    return -1;
  }

  /* Print important parameters if verbose enough */
  if (verbose >= 3)
    sl_printslcd (slconn);

  /* Only do a ping if requested */
  if (pingonly)
    exit (ping_server (slconn));

  /* Loop with the connection manager */
  while ((status = sl_collect (slconn, &packetinfo,
                               plbuffer, (uint32_t)sizeof (plbuffer))) != SLTERMINATE)
  {
    if (status == SLPACKET)
    {
      packet_handler (slconn, packetinfo, plbuffer, packetinfo->payloadcollected);
    }
    else if (status == SLTOOLARGE)
    {
      sl_log (2, 0, "received payload length %u too large for max buffer of %zu\n",
              packetinfo->payloadlength, sizeof (plbuffer));
      exitstatus = 1;
      break;
    }
    else if (status == SLAUTHFAIL)
    {
      sl_log (2, 0, "authentication failed\n");
      exitstatus = 1;
      break;
    }
    else if (status == SLNOPACKET)
    {
      /* Only reachable in non-blocking mode, which this program does not use */
      sl_usleep (500000);
    }

    if (statefile && stateint)
    {
      if (++packetcnt >= (uint64_t)stateint)
      {
        sl_savestate (slconn, statefile);
        packetcnt = 0;
      }
    }

    /* An INFO query only: quit if no streams and INFO complete */
    if (status == SLPACKET &&
        slconn->streams == NULL &&
        (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM ||
         (packetinfo->payloadformat == SLPAYLOAD_JSON &&
          (packetinfo->payloadsubformat == SLPAYLOAD_JSON_INFO ||
           packetinfo->payloadsubformat == SLPAYLOAD_JSON_ERROR))))
      break;
  }

  /* Shutdown */
  sl_disconnect (slconn);

  if (dumpfile)
    fclose (outfile);

  if (statefile)
    sl_savestate (slconn, statefile);

  sl_freeslcd (slconn);

  return exitstatus;
} /* End of main() */

/***************************************************************************
 * packet_handler:
 * Process a received packet based on packet type.
 ***************************************************************************/
static void
packet_handler (SLCD *slconn, const SLpacketinfo *packetinfo,
                const char *payload, uint32_t payloadlength)
{
  (void)slconn; /* Unused for now */
  static MS3Record *msr = NULL;
  char timestamp[64]    = {0};
  int64_t now_nano;
  time_t now_sec;
  int64_t now_milli;
  struct tm *timep;

  /* Build a current local time string */
  now_nano  = sl_nstime ();
  now_sec   = (time_t)(now_nano / 1000000000);
  now_milli = (now_nano - ((int64_t)now_sec * 1000000000)) / 1000000;
  timep     = localtime (&now_sec);

  if (timep)
    snprintf (timestamp, sizeof (timestamp), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
              timep->tm_year + 1900, timep->tm_mon + 1, timep->tm_mday,
              timep->tm_hour, timep->tm_min, timep->tm_sec, (int)now_milli);

  sl_log (0, 1, "%s (local), seq %" PRIu64 ", Received %u bytes of payload format %s\n",
          timestamp, packetinfo->seqnum, payloadlength,
          sl_formatstr (packetinfo->payloadformat, packetinfo->payloadsubformat));

  /* Handle miniSEED payload packets */
  if (packetinfo->payloadformat == SLPAYLOAD_MSEED2 ||
      packetinfo->payloadformat == SLPAYLOAD_MSEED3)
  {
    int parse_status = msr3_parse (payload, payloadlength, &msr,
                                   (psamples) ? MSF_UNPACKDATA : 0, verbose);

    /* On failure, 'msr' may still point at the previously parsed record */
    if (parse_status != 0 || msr == NULL)
    {
      sl_log (2, 0, "cannot parse miniSEED record\n");
      return;
    }

    if (ppackets)
    {
      if (ppackets < 2)
      {
        /* Create single line summary and include latency */
        ms_nstime2timestr_n (msr->starttime, timestamp, sizeof (timestamp), ISOMONTHDAY_Z, NANO_MICRO);

        sl_log (0, 0, "%s, %d, %d, %" PRId64 " samples, %g Hz, %s (latency ~%.1f)\n",
                msr->sid, msr->pubversion, msr->reclen, msr->samplecnt,
                msr3_sampratehz (msr), timestamp, msr3_host_latency (msr));
      }
      else
      {
        msr3_print (msr, ppackets - 1);
      }
    }

    if (psamples)
      print_samples (msr, (psamples >= 2) ? 0 : 6);
  }
  /* Handle miniSEED-encoded INFO packets */
  else if (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFO ||
           packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM)
  {
    int parse_status = msr3_parse (payload, payloadlength, &msr, MSF_UNPACKDATA, verbose);

    /* On failure, 'msr' may still point at the previously parsed record */
    if (parse_status != 0 || msr == NULL)
    {
      sl_log (2, 0, "cannot parse miniSEED record\n");
      return;
    }

    if (info_handler_mseed (msr, (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM) ? 1 : 0) == -2)
    {
      sl_log (2, 1, "processing of INFO record failed\n");
    }
  }
  else if (packetinfo->payloadformat == SLPAYLOAD_JSON)
  {
    if (info_request == INFO_REQUEST_RAW)
    {
      print_json (payload, payloadlength, 0);
    }
    else if (packetinfo->payloadsubformat == SLPAYLOAD_JSON_ERROR)
    {
      print_info_json (infofile, payload, payloadlength, 0);
    }
    else
    {
      print_info_json (infofile, payload, payloadlength, formatlevel);
    }
  }
  /* Handle XML payloads (distinct from the legacy miniSEED-encoded XML INFO
   * handled above); no formatted parser is implemented for this format */
  else if (packetinfo->payloadformat == SLPAYLOAD_XML)
  {
    if (info_request == INFO_REQUEST_RAW)
    {
      fprintf (infofile, "%.*s\n", (int)payloadlength, payload);
    }
    else
    {
      sl_log (1, 1, "Formatted parsing of XML payloads is not supported, use '-i' for raw output\n");
    }
  }
  else
  {
    sl_log (1, 1, "Unsupported payload type: %s\n",
            sl_formatstr (packetinfo->payloadformat, packetinfo->payloadsubformat));
  }

  /* Write miniSEED data packets to dumpfile if defined */
  if (dumpfile &&
      (packetinfo->payloadformat == SLPAYLOAD_MSEED2 ||
       packetinfo->payloadformat == SLPAYLOAD_MSEED3))
  {
    if (fwrite (payload, payloadlength, 1, outfile) == 0)
      sl_log (2, 0, "fwrite(): error writing data to %s\n", dumpfile);
  }
} /* End of packet_handler() */

/***************************************************************************
 * info_handler_mseed:
 *
 * Process XML-based INFO packets encoded in miniSEED text records.
 *
 * Returns:
 * -2 = Errors
 * -1 = XML is terminated
 *  0 = XML is not terminated
 ***************************************************************************/
static int
info_handler_mseed (MS3Record *msr, int terminate)
{
  static char *xml_buffer  = NULL;
  static size_t xml_length = 0;

  char channel[11];
  char *xml_bit;
  int xml_bitsize;

  if (!msr)
    return -2;

  xml_bit     = (char *)msr->datasamples;
  xml_bitsize = (int)msr->numsamples;

  /* Only append if unpacked, non-empty sample data is present */
  if (xml_bit != NULL && xml_bitsize > 0)
  {
    /* Buffer size sanity check: 10MiB limit */
    if ((xml_length + xml_bitsize) > 10485760)
    {
      sl_log (2, 0, "%s(): XML buffer beyond sanity limit\n", __func__);

      free (xml_buffer);
      xml_buffer = NULL;
      xml_length = 0;

      return -2;
    }

    char *newbuffer;

    /* Text up to any NUL in the record */
    xml_bitsize = (int)strnlen (xml_bit, (size_t)xml_bitsize);

    /* Grow XML string buffer, include room (+1) for NULL terminator */
    if ((newbuffer = realloc (xml_buffer, (xml_length + xml_bitsize + 1))) == NULL)
    {
      sl_log (2, 0, "%s(): XML buffer memory allocation error\n", __func__);

      free (xml_buffer);
      xml_buffer = NULL;
      xml_length = 0;

      return -2;
    }
    xml_buffer = newbuffer;

    /* Append new XML to buffer */
    memcpy (xml_buffer + xml_length, xml_bit, xml_bitsize);
    xml_length += xml_bitsize;
    xml_buffer[xml_length] = '\0';
  }

  /* Check for an error condition */
  channel[0] = '\0';
  ms_sid2nslc_n (msr->sid, NULL, 0, NULL, 0, NULL, 0, channel, sizeof (channel));

  if (!strncmp (channel, "ERR", 3))
  {
    sl_log (2, 0, "INFO type requested is not enabled\n");

    free (xml_buffer);
    xml_buffer = NULL;
    xml_length = 0;

    return -2;
  }

  /* Process the XML if terminated */
  if (terminate)
  {
    /* Parse the XML for formatted output */
    if (xml_buffer == NULL)
    {
      sl_log (2, 0, "INFO response contained no XML\n");
    }
    else if (info_request == INFO_REQUEST_FORMAT)
    {
      print_info_xml (infofile, xml_buffer, xml_length, formatlevel);
    }
    /* Dump the raw XML */
    else
    {
      fprintf (infofile, "%s\n", xml_buffer);
    }

    /* Clean up */
    info_request = INFO_REQUEST_NONE;

    free (xml_buffer);
    xml_buffer = NULL;
    xml_length = 0;

    return -1;
  }

  return 0;
} /* End of info_handler_mseed() */

/***************************************************************************
 * read_input:
 *
 * Read a line from standard input into 'buffer' of 'size' bytes, removing
 * the trailing newline.  Terminal echo is disabled if 'noecho' is set and
 * standard input is a terminal.
 *
 * Returns 0 on success and -1 on failure.
 ***************************************************************************/
static int
read_input (char *buffer, size_t size, int noecho)
{
  char *result;
#ifndef _WIN32
  struct termios saved;
  struct termios quiet;
  int restore = 0;

  if (noecho && isatty (STDIN_FILENO) && tcgetattr (STDIN_FILENO, &saved) == 0)
  {
    quiet = saved;
    quiet.c_lflag &= ~(tcflag_t)ECHO;
    restore = (tcsetattr (STDIN_FILENO, TCSAFLUSH, &quiet) == 0);
  }
#else
  (void)noecho;
#endif

  result = fgets (buffer, (int)size, stdin);

#ifndef _WIN32
  if (restore)
  {
    tcsetattr (STDIN_FILENO, TCSAFLUSH, &saved);
    fprintf (stderr, "\n");
  }
#endif

  if (result == NULL)
    return -1;

  buffer[strcspn (buffer, "\r\n")] = '\0';

  return 0;
} /* End of read_input() */

/***************************************************************************
 * auth_value_userpass:
 *
 * A callback function registered at SLCD.auth_value() that should return
 * a string to be submitted with the SeedLink AUTH command.
 *
 * In this case, the function prompts the user for a username and password
 * for interactive input.
 *
 * Returns authorization value string on success, and NULL on failure
 ***************************************************************************/
static const char *
auth_value_userpass (const char *server, void *data)
{
  (void)data; /* User-supplied data is not used in this case */
  char username[256] = {0};
  char password[256] = {0};
  const char *value  = NULL;
  int printed;

  fprintf (stderr, "Enter username for %s: ", server);
  if (read_input (username, sizeof (username), 0))
  {
    fprintf (stderr, "%s() failed to read username\n", __func__);
    goto cleanup;
  }

  fprintf (stderr, "Enter password: ");
  if (read_input (password, sizeof (password), 1))
  {
    fprintf (stderr, "%s() failed to read password\n", __func__);
    goto cleanup;
  }

  /* Create AUTH value of "USERPASS <username> <password>" */
  printed = snprintf (auth_buffer, sizeof (auth_buffer),
                      "USERPASS %s %s",
                      username, password);

  if (printed < 0 || (size_t)printed >= sizeof (auth_buffer))
  {
    fprintf (stderr, "%s() Auth value is too large (%d bytes)\n", __func__, printed);
    memset (auth_buffer, 0, sizeof (auth_buffer));
    goto cleanup;
  }

  value = auth_buffer;

cleanup:
  memset (username, 0, sizeof (username));
  memset (password, 0, sizeof (password));

  return value;
}

/***************************************************************************
 * auth_value_token:
 *
 * A callback function registered at SLCD.auth_value() that should return
 * a string to be submitted with the SeedLink AUTH command.
 *
 * In this case, the function prompts the user for a JWT token for
 * interactive input.
 *
 * Returns authorization value string on success, and NULL on failure
 ***************************************************************************/
static const char *
auth_value_token (const char *server, void *data)
{
  (void)data; /* User-supplied data is not used in this case */
  char token[4096]  = {0};
  const char *value = NULL;
  int printed;

  fprintf (stderr, "Enter token for %s: ", server);
  if (read_input (token, sizeof (token), 1))
  {
    fprintf (stderr, "%s() failed to read token\n", __func__);
    goto cleanup;
  }

  /* Create AUTH value of "JWT <token>" */
  printed = snprintf (auth_buffer, sizeof (auth_buffer),
                      "JWT %s",
                      token);

  if (printed < 0 || (size_t)printed >= sizeof (auth_buffer))
  {
    fprintf (stderr, "%s() Auth value is too large (%d bytes)\n", __func__, printed);
    memset (auth_buffer, 0, sizeof (auth_buffer));
    goto cleanup;
  }

  value = auth_buffer;

cleanup:
  memset (token, 0, sizeof (token));

  return value;
}

/***************************************************************************
 * auth_finish:
 *
 * A callback function registered at SLCD.auth_finish() that is called
 * after the AUTH command has been sent to the server.
 *
 * In this case, the function clears the memory used to store the
 * username and password populated by auth_value().
 ***************************************************************************/
static void
auth_finish (const char *server, void *data)
{
  (void)server; /* Server name is not used in this case */
  (void)data;   /* User-supplied data is not used in this case */

  /* Clear memory used to store auth value */
  memset (auth_buffer, 0, sizeof (auth_buffer));
}

/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
parameter_proc (SLCD *slconn, int argcount, char **argvec)
{
  char starttimestr[32] = {0};
  char endtimestr[32]   = {0};
  int error             = 0;
  int optind;

  char *server_address = NULL;
  char *streamfile     = NULL;
  char *multiselect    = NULL;
  char *selectors      = NULL;

  char info_cmd[100] = {0};
  char *info_type    = NULL;
  int printed;

  char *timewin   = NULL;
  char *timestart = NULL;
  char *timeend   = NULL;
  char *tptr;

  if (argcount <= 1)
    error++;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
  {
    if (strcmp (argvec[optind], "-V") == 0)
    {
      fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
      exit (0);
    }
    else if (strcmp (argvec[optind], "-h") == 0)
    {
      usage ();
      exit (0);
    }
    else if (strncmp (argvec[optind], "-v", 2) == 0)
    {
      verbose += strspn (&argvec[optind][1], "v");
    }
    else if (strcmp (argvec[optind], "-P") == 0)
    {
      pingonly = 1;
    }
    else if (strncmp (argvec[optind], "-p", 2) == 0)
    {
      ppackets += strspn (&argvec[optind][1], "p");
    }
    else if (strcmp (argvec[optind], "-T") == 0)
    {
      sl_set_tlsmode (slconn, 1);
    }
    else if (strcmp (argvec[optind], "-Ap") == 0)
    {
      sl_set_auth_params (slconn, auth_value_userpass, auth_finish, NULL);
    }
    else if (strcmp (argvec[optind], "-At") == 0)
    {
      sl_set_auth_params (slconn, auth_value_token, auth_finish, NULL);
    }
    else if (strcmp (argvec[optind], "-3") == 0)
    {
      sl_set_protocol (slconn, SLPROTO3X);
    }
    else if (strcmp (argvec[optind], "-4") == 0)
    {
      sl_set_protocol (slconn, SLPROTO40);
    }
    else if (strncmp (argvec[optind], "-u", 2) == 0)
    {
      psamples += strspn (&argvec[optind][1], "u");
    }
    else if (strcmp (argvec[optind], "-d") == 0)
    {
      sl_set_dialupmode (slconn, 1);
    }
    else if (strcmp (argvec[optind], "-b") == 0)
    {
      sl_set_batchmode (slconn, 1);
    }
    else if (strcmp (argvec[optind], "-nt") == 0)
    {
      sl_set_idletimeout (slconn, atoi (getoptval (argcount, argvec, optind++)));
    }
    else if (strcmp (argvec[optind], "-nd") == 0)
    {
      sl_set_reconnectdelay (slconn, atoi (getoptval (argcount, argvec, optind++)));
    }
    else if (strcmp (argvec[optind], "-k") == 0)
    {
      sl_set_keepalive (slconn, atoi (getoptval (argcount, argvec, optind++)));
    }
    else if (strcmp (argvec[optind], "-o") == 0)
    {
      dumpfile = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-l") == 0)
    {
      streamfile = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-s") == 0)
    {
      selectors = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-S") == 0)
    {
      multiselect = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-x") == 0)
    {
      statefile = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-i") == 0)
    {
      info_type    = getoptval (argcount, argvec, optind++);
      info_request = INFO_REQUEST_RAW;
    }
    else if (strcmp (argvec[optind], "-F") == 0)
    {
      info_type    = getoptval (argcount, argvec, optind++);
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strcmp (argvec[optind], "-I") == 0)
    {
      info_type    = "ID";
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strcmp (argvec[optind], "-L") == 0)
    {
      info_type    = "STATIONS";
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strcmp (argvec[optind], "-Q") == 0)
    {
      info_type    = "STREAMS";
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strcmp (argvec[optind], "-G") == 0)
    {
      info_type    = "GAPS";
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strcmp (argvec[optind], "-C") == 0)
    {
      info_type    = "CONNECTIONS";
      info_request = INFO_REQUEST_FORMAT;
    }
    else if (strncmp (argvec[optind], "-f", 2) == 0)
    {
      formatlevel += strspn (&argvec[optind][1], "f");
    }
    else if (strcmp (argvec[optind], "-ts") == 0)
    {
      timestart = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-te") == 0)
    {
      timeend = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-tw") == 0)
    {
      timewin = getoptval (argcount, argvec, optind++);
    }
    else if (strncmp (argvec[optind], "-", 1) == 0)
    {
      fprintf (stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
    else if (server_address == NULL)
    {
      server_address = argvec[optind];
    }
    else
    {
      fprintf (stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
  }

  /* Make sure a server was specified */
  if (server_address == NULL)
  {
    fprintf (stderr, "No SeedLink server specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Usage: %s [options] [host][:][port]\n\n", PACKAGE);
    fprintf (stderr, "Try '-h' for detailed help\n");
    exit (1);
  }

  sl_set_serveraddress (slconn, server_address);

  /* Initialize the verbosity for the sl_log function */
  sl_loginit (verbose, NULL, NULL, NULL, NULL);
  infofile = stdout;

  /* Open dumpfile if requested */
  if (dumpfile)
  {
    if (!strcmp (dumpfile, "-"))
    {
      /* Re-direct all messages to standard error */
      sl_loginit (verbose, &print_stderr, NULL, &print_stderr, NULL);
      infofile = stderr;

      outfile = stdout;
      setvbuf (stdout, NULL, _IONBF, 0);
    }
    else if ((outfile = fopen (dumpfile, "ab")) != NULL)
    {
      setvbuf (outfile, NULL, _IONBF, 0);
    }
    else
    {
      sl_log (2, 0, "cannot open dumpfile: %s\n", dumpfile);
      exit (1);
    }
  }

  /* Report the program version */
  sl_log (1, 1, "%s version: %s\n", PACKAGE, VERSION);

  /* If errors then report the usage message and quit */
  if (error)
  {
    usage ();
    exit (1);
  }

  /* Make sure we print basic packet details if printing samples */
  if (psamples && ppackets == 0)
    ppackets = 1;

  /* Load the stream list from a file if specified */
  if (streamfile && sl_add_streamlist_file (slconn, streamfile, selectors) < 0)
    return -1;

  if (timestart && normalize_time (timestart, starttimestr, sizeof (starttimestr)))
  {
    sl_log (2, 0, "start time not in recognized format: '%s'\n", timestart);
    return -1;
  }

  if (timeend && normalize_time (timeend, endtimestr, sizeof (endtimestr)))
  {
    sl_log (2, 0, "end time not in recognized format: '%s'\n", timeend);
    return -1;
  }

  /* DEPRECATED, legacy support for time window argument */
  if (timewin)
  {
    char *startptr;
    char *endptr;

    sl_log (1, 0, "option -tw is deprecated, use -ts and -te\n");

    if ((startptr = strdup (timewin)) == NULL)
    {
      sl_log (2, 0, "cannot allocate memory\n");
      return -1;
    }

    if ((endptr = strchr (startptr, ':')) == NULL)
    {
      sl_log (2, 0, "time window (-tw) not in start:[end] format\n");
      free (startptr);
      return -1;
    }

    /* Terminate begin time part and increment pointer to end time start */
    *endptr = '\0';
    endptr++;

    if (startptr[0] != '\0' && normalize_time (startptr, starttimestr, sizeof (starttimestr)))
    {
      sl_log (2, 0, "start time not in recognized format: '%s'\n", startptr);
      free (startptr);
      return -1;
    }

    if (endptr[0] != '\0' && normalize_time (endptr, endtimestr, sizeof (endtimestr)))
    {
      sl_log (2, 0, "end time not in recognized format: '%s'\n", endptr);
      free (startptr);
      return -1;
    }

    free (startptr);
  }

  if (starttimestr[0] != '\0' || endtimestr[0] != '\0')
  {
    sl_set_timewindow (slconn,
                       starttimestr[0] ? starttimestr : NULL,
                       endtimestr[0] ? endtimestr : NULL);
  }

  /* Configure an INFO request */
  if (info_request != INFO_REQUEST_NONE)
  {
    /* Convert station and stream pattern delimiter in multiselect to a space */
    if (multiselect && (tptr = strchr (multiselect, ':')) != NULL)
      *tptr = ' ';

    if (multiselect)
      printed = snprintf (info_cmd, sizeof (info_cmd), "%s %s", info_type, multiselect);
    else
      printed = snprintf (info_cmd, sizeof (info_cmd), "%s", info_type);

    if (printed < 0 || (size_t)printed >= sizeof (info_cmd))
    {
      sl_log (2, 0, "INFO request is too long, maximum is %zu characters\n", sizeof (info_cmd) - 1);
      return -1;
    }

    if (sl_request_info (slconn, info_cmd))
    {
      sl_log (2, 0, "cannot request INF: %s\n", info_cmd);
      return -1;
    }
  }
  /* If not INFO, parse the 'multiselect' string following '-S' */
  else if (multiselect)
  {
    if (sl_add_streamlist (slconn, multiselect, selectors) == -1)
      return -1;
  }
  else if (slconn->streams == NULL && slconn->info == NULL)
  { /* No 'streams' array or INFO requested, assuming uni-station mode */
    sl_set_allstation_params (slconn, selectors, SL_UNSETSEQUENCE, NULL);
  }

  /* Attempt to recover sequence numbers from state file */
  if (statefile)
  {
    /* Check if interval was specified for state saving */
    if ((tptr = strchr (statefile, ':')) != NULL)
    {
      char *tail;
      unsigned long interval;

      *tptr++ = '\0';

      interval = strtoul (tptr, &tail, 0);

      if (*tptr == '\0' || *tail || interval > 1000000000UL)
      {
        sl_log (2, 0, "state saving interval specified incorrectly\n");
        return -1;
      }

      stateint = (int)interval;
    }

    if (sl_recoverstate (slconn, statefile) < 0)
    {
      sl_log (2, 0, "state recovery failed\n");
    }
  }

  return 0;
} /* End of parameter_proc() */

/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if (argvec == NULL || argvec[argopt] == NULL)
  {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
  }

  /* Special case of '-o -' usage */
  if ((argopt + 1) < argcount && strcmp (argvec[argopt], "-o") == 0)
    if (strcmp (argvec[argopt + 1], "-") == 0)
      return argvec[argopt + 1];

  if ((argopt + 1) < argcount && *argvec[argopt + 1] != '-')
    return argvec[argopt + 1];

  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);

  return NULL; /* To stop compiler warnings about no return */
} /* End of getoptval() */

/***************************************************************************
 * normalize_time:
 * Convert an ISO or SeedLink-style (comma) time string to a normalized
 * ISO time string in 'output' of 'outputsize' bytes.
 *
 * Returns 0 on success and -1 on unrecognized or too-long input.
 ***************************************************************************/
static int
normalize_time (const char *input, char *output, size_t outputsize)
{
  nstime_t nstime;

  /* sl_isodatetime() may append a 'Z' to the input */
  if (strlen (input) + 2 > outputsize)
    return -1;

  sl_isodatetime (output, input);

  if ((nstime = ms_timestr2nstime (output)) == NSTERROR)
    return -1;

  if (ms_nstime2timestr_n (nstime, output, outputsize, ISOMONTHDAY_Z, NANO_MICRO_NONE) == NULL)
    return -1;

  return 0;
} /* End of normalize_time() */

/***************************************************************************
 * print_samples:
 *
 * For binary sample types, 6 samples will be printed per line of output.
 * For text samples, 70 characters will be printed per line of output.
 *
 * Output will be limited to the first 'maxlines' lines of samples.
 * Set maxlines to zero to print all samples.
 *
 * Print payload samples in the supplied record with a simple format.
 ***************************************************************************/
static void
print_samples (MS3Record *msr, int maxlines)
{
  int line, lines, col, cnt;
  int32_t *idata;
  float *fdata;
  double *ddata;
  char *tdata;

  if (!msr || !msr->datasamples)
    return;

  if (msr->sampletype == 'i' || msr->sampletype == 'f' || msr->sampletype == 'd')
  {
    idata = (int32_t *)msr->datasamples;
    fdata = (float *)msr->datasamples;
    ddata = (double *)msr->datasamples;

    /* Print 6 binary samples per line */
    lines    = (msr->numsamples / 6) + 1;
    maxlines = (maxlines <= 0) ? lines : maxlines;

    for (cnt = 0, line = 0;
         line < lines && line < maxlines;
         line++)
    {
      for (col = 0; col < 6; col++)
      {
        if (cnt < msr->numsamples)
        {
          if (msr->sampletype == 'i')
          {
            sl_log (0, 0, "%10d  ", idata[cnt++]);
          }
          else if (msr->sampletype == 'f')
          {
            sl_log (0, 0, "%10g  ", fdata[cnt++]);
          }
          else if (msr->sampletype == 'd')
          {
            sl_log (0, 0, "%10g  ", ddata[cnt++]);
          }
        }
      }
      sl_log (0, 0, "\n");
    }

    /* Print ellipsis if not all samples were printed */
    if (line < lines)
      sl_log (0, 0, "...\n");
  }
  else if (msr->sampletype == 't')
  {
    tdata = (char *)msr->datasamples;

    /* Print 70 character samples per line */
    lines    = (msr->numsamples + 69) / 70;
    maxlines = (maxlines <= 0) ? lines : maxlines;

    for (cnt = 0, line = 0;
         line < lines && line < maxlines;
         line++)
    {
      sl_log (0, 0, "%.70s\n", &tdata[cnt]);
      cnt += 70;
    }

    /* Print ellipsis if not all samples were printed */
    if (line < lines)
      sl_log (0, 0, "...\n");
  }
  else
  {
    sl_log (0, 0, "Unrecognized sample type: %c\n", msr->sampletype);
  }

  return;
} /* End of print_samples() */

/***************************************************************************
 * ping_server:
 *
 * Ping a server and print the server ID and site.
 *
 * Returns 0 on success, and 1 on failure.
 ***************************************************************************/
static int
ping_server (SLCD *slconn)
{
  /* sl_ping() only populates these on success (retval == 0), using an
   * internal strcpy() with no length limit, so 100 bytes is the required
   * minimum size; leave uninitialized otherwise. */
  char serverid[100];
  char site[100];
  char *cp;
  int retval;

  retval = sl_ping (slconn, serverid, site);

  if (retval == 0)
  {
    /* Truncate server ID and site strings at carriage return or new line */
    if ((cp = strchr (serverid, '\r')) != NULL ||
        (cp = strchr (serverid, '\n')) != NULL)
      *cp = '\0';
    if ((cp = strchr (site, '\r')) != NULL ||
        (cp = strchr (site, '\n')) != NULL)
      *cp = '\0';

    sl_log (0, 0, "%s\n%s\n", serverid, site);
  }
  else if (retval == -1)
  {
    sl_log (1, 0, "Bad response from server, not SeedLink?\n");
    retval = 1;
  }
  else if (retval == -2)
  {
    sl_log (1, 0, "Could not open network connection\n");
    retval = 1;
  }

  return retval;
} /* End of ping_server() */

/***************************************************************************
 * print_stderr:
 * Print the given message to standard error.
 ***************************************************************************/
static void
print_stderr (const char *message)
{
  fprintf (stderr, "%s", message);
  return;
}

/***************************************************************************
 * print_json:
 *
 * print simply-formatted JSON by inserting indentation, spaces and newlines.
 *
 * Returns 0 on success and -1 on error.
 ***************************************************************************/
static int
print_json (const char *json, uint32_t jsonlength, int indent)
{
  uint32_t idx;
  int instring = 0;
  int escaped  = 0;

  if (!json)
    return -1;

  /* Print JSON character-by-character, inserting
   * indentation, spaces and newlines for readability. */
  sl_log (0, 0, "%*s", indent, "");
  for (idx = 0; idx < jsonlength; idx++)
  {
    /* Toggle "in string" flag for unescaped double quotes */
    if (escaped)
      escaped = 0;
    else if (instring && json[idx] == '\\')
      escaped = 1;
    else if (json[idx] == '"')
      instring = (instring) ? 0 : 1;

    if (!instring)
    {
      if (json[idx] == ':')
      {
        sl_log (0, 0, ": ");
      }
      else if (json[idx] == ',')
      {
        sl_log (0, 0, ",\n%*s", indent, "");
      }
      else if (json[idx] == '{')
      {
        indent += 2;
        sl_log (0, 0, "{\n%*s", indent, "");
      }
      else if (json[idx] == '[')
      {
        indent += 2;
        sl_log (0, 0, "[\n%*s", indent, "");
      }
      else if (json[idx] == '}')
      {
        indent -= 2;
        sl_log (0, 0, "\n%*s}", indent, "");
      }
      else if (json[idx] == ']')
      {
        indent -= 2;
        sl_log (0, 0, "\n%*s]", indent, "");
      }
      else
      {
        sl_log (0, 0, "%c", json[idx]);
      }
    }
    else
    {
      sl_log (0, 0, "%c", json[idx]);
    }
  }
  sl_log (0, 0, "\n");

  return 0;
} /* End of print_json() */

/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Usage: %s [options] [host][:][port]\n\n", PACKAGE);
  fprintf (stderr,
           " ## General program options ##\n"
           " -V              report program version\n"
           " -h              show this usage message\n"
           " -v              be more verbose, multiple flags can be used\n"
           " -P              ping the server, report the server ID and exit\n"
           " -p              print details of data packets, multiple flags can be used\n"
           " -u              print unpacked samples of data packets, -uu for all samples\n"
           " -T              enable secure TLS connection, already enabled if port 18500\n"
           " -Ap             prompt for user and password authentication details (v4 only)\n"
           " -At             prompt for JWT authentication token (v4 only)\n"
           " -3 or -4        use SeedLink 3.x or 4.0 protocol explicitly\n"
           "\n"
           " -nd delay       network re-connect delay (seconds), default 30\n"
           " -nt timeout     network timeout (seconds), re-establish connection if no\n"
           "                   data/keepalives are received in this time, default 600\n"
           " -k interval     send keepalive (heartbeat) packets this often (seconds)\n"
           " -x sfile[:int]  save/restore stream state information to this file\n"
           " -d              configure the connection in dial-up mode\n"
           " -b              configure the connection in batch mode (SLv3)\n"
           "\n"
           " ## Data stream selection ##\n"
           " -s selectors    selectors for uni-station or default for multi-station mode\n"
           " -l listfile     read a stream list from this file for multi-station mode\n"
           " -S streams      define a stream list for multi-station mode\n"
           "   'streams' = 'stream1[:selectors1],stream2[:selectors2],...'\n"
           "        'stream' is in NET_STA format, for example:\n"
           "        -S \"IU_KONO:B_H_E B_H_N,GE_WLF,MN_AQU:H_H_?:3\"\n"
           "\n"
           " -ts starttime   specify a start time\n"
           " -te endtime     specify an end time\n"
           "\n"
           " ## Data saving options ##\n"
           " -o outfile      write all received records to this file\n"
           "\n"
           " ## Data server information ## (requires SeedLink >= 3)\n"
           " -f              increase level of details included in formatted output\n"
           " -i type         request info, print in raw form\n"
           " -F type         request info, parse and print formatted form\n"
           "                   Standard info types are:\n"
           "                   ID, CAPABILITIES, STATIONS, STREAMS, CONNECTIONS, FORMATS, GAPS, ALL\n"
           " -I              equivalent to -F ID\n"
           " -L              equivalent to -F STATIONS\n"
           " -Q              equivalent to -F STREAMS\n"
           " -G              equivalent to -F GAPS\n"
           " -C              equivalent to -F CONNECTIONS\n"
           "\n"
           " [host][:][port] Address of the SeedLink server in host:port format\n"
           "                   Default host is 'localhost' and default port is '18000'\n");

} /* End of usage() */
