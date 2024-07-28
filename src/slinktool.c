/***************************************************************************
 * A SeedLink client for data stream inspection, data collection and server
 * testing.
 *
 * Connects to a SeedLink server, configures a connection using either
 * and collects data and/or server details (INFO).  Detailed
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

//TODO
// convert from sl_dtime() to sl_nstime()
// Add JSON INFO parsing routines akin to XML ones to pretty-print the output, print_json use elsewise
// Add -F for INFO FORMATS

#ifndef SLP_WIN
#include <signal.h>
#endif

#include <libslink.h>
#include <libmseed.h>
#include "slinkxml.h"

#define PACKAGE "slinktool"
#define VERSION "5.0.0DEV"

static short int verbose  = 0; /* flag to control general verbosity */
static short int pingonly = 0; /* flag to control ping function */
static short int ppackets = 0; /* flag to control printing of data packets */
static short int psamples = 0; /* flag to control printing of data samples */
static int stateint       = 0; /* packet interval to save statefile */
static char *statefile    = 0; /* state file for saving/restoring the seq. no. */
static char *dumpfile     = 0; /* output file for data dump */
static FILE *outfile      = 0; /* the descriptor for the dumpfile */

static SLCD *slconn; /* connection parameters */

#define MAX_PAYLOAD_SIZE 10485760       /* maximum payload in bytes, 10 MiB */
static char plbuffer[MAX_PAYLOAD_SIZE]; /* payload buffer */

/* Query types */
static enum {
  SLTNoQuery,
  SLTIDQuery,
  SLTStationQuery,
  SLTStreamQuery,
  SLTGapQuery,
  SLTConnectionQuery,
  SLTGenericQuery,
  SLTKeepAliveQuery
} slt_query = SLTNoQuery;

/* Functions internal to this source file */
static void packet_handler (const SLpacketinfo *packetinfo,
                            const char *payload, uint32_t payloadlength);
static int info_handler_mseed (MS3Record *msr, int terminate);

static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static void print_samples (MS3Record *msr, int maxlines);
static int ping_server (SLCD *slconn);
static void print_stderr (const char *message);
static int print_json (const char *json, uint32_t jsonlength, int indent);
static void report_environ ();
static void usage (void);

#ifndef SLP_WIN
static void term_handler (int sig);
#endif

int
main (int argc, char **argv)
{
  const SLpacketinfo *packetinfo = NULL;
  int status;

  uint64_t packetcnt = 0;

#ifndef SLP_WIN
  /* Signal handling, use POSIX calls with standardized semantics */
  struct sigaction sa;

  sa.sa_flags = SA_RESTART;
  sigemptyset (&sa.sa_mask);

  sa.sa_handler = term_handler;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);

  sa.sa_handler = SIG_IGN;
  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);
#endif

  /* Allocate and initialize a new connection description */
  slconn = sl_newslcd (PACKAGE, VERSION);

  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
  {
    sl_log (2, 0, "parameter processing failed.\n");
    return -1;
  }

  /* Print important parameters if verbose enough */
  if (verbose >= 3)
    report_environ ();

  /* Only do a ping if requested */
  if (pingonly)
    exit (ping_server (slconn));

  /* Loop with the connection manager */
  while ((status = sl_collect (slconn, &packetinfo,
                               plbuffer, (uint32_t)sizeof (plbuffer))) != SLTERMINATE)
  {
    if (status == SLPACKET)
    {
      packet_handler (packetinfo, plbuffer, packetinfo->payloadcollected);
    }
    else if (status == SLTOOLARGE)
    {
      sl_log (2, 0, "received payload length %u too large for max buffer of %zu\n",
              packetinfo->payloadlength, sizeof (plbuffer));
      break;
    }

    if (statefile && stateint)
    {
      if (++packetcnt >= stateint)
      {
        sl_savestate (slconn, statefile);
        packetcnt = 0;
      }
    }

    /* An INFO query only: quit if no streams and INFO complete */
    if (status == SLPACKET &&
        slconn->streams == NULL &&
        (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM ||
         packetinfo->payloadformat == SLPAYLOAD_JSON_INFO))
      break;
  }

  /* Shutdown */
  if (slconn->link != -1)
    sl_disconnect (slconn);

  if (dumpfile)
    fclose (outfile);

  if (statefile)
    sl_savestate (slconn, statefile);

  return 0;
} /* End of main() */

/***************************************************************************
 * packet_handler:
 * Process a received packet based on packet type.
 ***************************************************************************/
static void
packet_handler (const SLpacketinfo *packetinfo,
                const char *payload, uint32_t payloadlength)
{
  static MS3Record *msr = NULL;
  char timestamp[64] = {0};
  int64_t now_nano;
  time_t now_sec;
  int64_t now_milli;
  struct tm *timep;

  /* Build a current local time string */
  now_nano  = sl_nstime ();
  now_sec   = (time_t)(now_nano / 1000000000);
  now_milli = (now_nano - ((int64_t)now_sec * 1000000000)) / 1000000;
  timep     = localtime (&now_sec);

  snprintf (timestamp, sizeof (timestamp), "%04d-%02d-%02dT%02d:%02d:%02d.%03d",
            timep->tm_year + 1900, timep->tm_mon + 1, timep->tm_mday,
            timep->tm_hour, timep->tm_min, timep->tm_sec, (int)now_milli);

  sl_log (0, 1, "%s (local), seq %" PRIu64 ", Received %u bytes of payload format %s\n",
          timestamp, packetinfo->seqnum, payloadlength,
          sl_formatstr(packetinfo->payloadformat, packetinfo->payloadsubformat));

  /* Handle miniSEED payload packets */
  if (packetinfo->payloadformat == SLPAYLOAD_MSEED2 ||
      packetinfo->payloadformat == SLPAYLOAD_MSEED3)
  {
    msr3_parse (payload, payloadlength, &msr,
                (psamples) ? MSF_UNPACKDATA : 0, 0);

    if (msr == NULL)
    {
      sl_log (2, 0, "cannot parse miniSEED record\n");
      return;
    }

    if (ppackets)
      msr3_print (msr, ppackets - 1);

    if (psamples)
      print_samples (msr, (ppackets >= 1) ? 0 : 6);
  }
  /* Handle miniSEED-encoded INFO packets */
  else if (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFO ||
           packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM)
  {
    msr3_parse (payload, payloadlength, &msr, MSF_UNPACKDATA, 0);

    if (msr == NULL)
    {
      sl_log (2, 0, "cannot parse miniSEED record\n");
      return;
    }

    if (info_handler_mseed (msr, (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM) ? 1 : 0) == -2)
    {
      sl_log (2, 1, "processing of INFO record failed\n");
    }
  }
  else if (packetinfo->payloadformat == SLPAYLOAD_JSON_INFO)
  {
    print_json (payload, payloadlength, 0);
  }
  //TODO add SLPAYLOAD_JSON support?
  //TODO add SLPAYLOAD_XML support?
  else
  {
    sl_log (1, 1, "Unsupported payload type: %c\n", packetinfo->payloadformat);
  }

  /* Write packet to dumpfile if defined */
  if (dumpfile)
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
  static char *xml_buffer = 0;
  static int xml_size     = 0;

  char channel[11];
  char *xml_bit;
  int xml_bitsize;
  ezxml_t xmldoc;

  if (!msr)
    return -2;

  xml_bit     = (char *)msr->datasamples;
  xml_bitsize = (int)msr->numsamples;

  /* Buffer size sanity check: 10MiB limit */
  if ((xml_size + xml_bitsize) > 10485760)
  {
    sl_log (2, 0, "%s(): XML buffer beyond sanity limit\n", __func__);

    if (xml_buffer)
      free (xml_buffer);
    xml_buffer = 0;
    xml_size   = 0;

    return -2;
  }

  /* Grow XML string buffer, include room (+1) for NULL terminator */
  if ((xml_buffer = realloc (xml_buffer, (xml_size + xml_bitsize + 1))) == NULL)
  {
    sl_log (2, 0, "%s(): XML buffer memory allocation error\n", __func__);
    return -2;
  }

  /* First character is terminator for initial buffer allocation */
  if (xml_size == 0)
  {
    *xml_buffer = '\0';
  }

  /* Append new XML to buffer */
  strncat (xml_buffer, xml_bit, xml_bitsize);
  xml_size += xml_bitsize;

  /* Check for an error condition */
  ms_sid2nslc (msr->sid, NULL, NULL, NULL, channel);

  if (!strncmp (channel, "ERR", 3))
  {
    sl_log (2, 0, "INFO type requested is not enabled\n");

    if (xml_buffer)
      free (xml_buffer);
    xml_buffer = 0;
    xml_size   = 0;

    return -2;
  }

  /* Process the XML if terminated */
  if (terminate)
  {
    /* Parse the XML if not dumping the raw XML */
    if (slt_query != SLTGenericQuery)
    {
      if ((xmldoc = ezxml_parse_str (xml_buffer, xml_size)) == NULL)
      {
        sl_log (2, 0, "%s(): XML parse error\n", __func__);

        if (xml_buffer)
          free (xml_buffer);
        xml_buffer = 0;
        xml_size   = 0;

        return -2;
      }

      switch (slt_query)
      {
      case SLTIDQuery:
        prtinfo_identification (xmldoc);
        break;
      case SLTStationQuery:
        prtinfo_stations (xmldoc);
        break;
      case SLTStreamQuery:
        prtinfo_streams (xmldoc);
        break;
      case SLTGapQuery:
        prtinfo_gaps (xmldoc);
        break;
      case SLTConnectionQuery:
        prtinfo_connections (xmldoc);
        break;
      default:
        sl_log (2, 0, "%s(): unrecognized INFO query: %d\n", __func__, slt_query);
        break;
      }

      ezxml_free (xmldoc);
    }
    else
    {
      fprintf (stdout, "%s\n", xml_buffer);
    }

    /* Clean up */
    slt_query = SLTNoQuery;

    if (xml_buffer)
      free (xml_buffer);
    xml_buffer = 0;
    xml_size   = 0;

    return -1;
  }

  return 0;
} /* End of info_handler_mseed() */

/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  nstime_t nstime;
  char timestr[64];
  int error = 0;
  int optind;

  char *streamfile  = NULL; /* stream list file for configuring streams */
  char *multiselect = NULL;
  char *selectors   = NULL;
  char *timewin     = NULL;
  char *timestart   = NULL;
  char *timeend     = NULL;
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
    else if (strncmp (argvec[optind], "-u", 2) == 0)
    {
      psamples = strspn (&argvec[optind][1], "u");
    }
    else if (strcmp (argvec[optind], "-d") == 0)
    {
      slconn->dialup = 1;
    }
    else if (strcmp (argvec[optind], "-b") == 0)
    {
      slconn->batchmode = 1;
    }
    else if (strcmp (argvec[optind], "-nt") == 0)
    {
      slconn->netto = atoi (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-nd") == 0)
    {
      slconn->netdly = atoi (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-k") == 0)
    {
      slconn->keepalive = atoi (getoptval (argcount, argvec, optind++));
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
      if (sl_request_info (slconn, getoptval (argcount, argvec, optind++)) == 0)
        slt_query = SLTGenericQuery;
    }
    else if (strcmp (argvec[optind], "-I") == 0)
    {
      if (sl_request_info (slconn, "ID") == 0)
        slt_query = SLTIDQuery;
    }
    else if (strcmp (argvec[optind], "-L") == 0)
    {
      if (sl_request_info (slconn, "STATIONS") == 0)
        slt_query = SLTStationQuery;
    }
    else if (strcmp (argvec[optind], "-Q") == 0)
    {
      if (sl_request_info (slconn, "STREAMS") == 0)
        slt_query = SLTStreamQuery;
    }
    else if (strcmp (argvec[optind], "-G") == 0)
    {
      if (sl_request_info (slconn, "GAPS") == 0)
        slt_query = SLTGapQuery;
    }
    else if (strcmp (argvec[optind], "-C") == 0)
    {
      if (sl_request_info (slconn, "CONNECTIONS") == 0)
        slt_query = SLTConnectionQuery;
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
    else if (!slconn->sladdr)
    {
      slconn->sladdr = argvec[optind];
    }
    else
    {
      fprintf (stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
  }

  /* Make sure a server was specified */
  if (!slconn->sladdr)
  {
    fprintf (stderr, "No SeedLink server specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Usage: %s [options] [host][:][port]\n\n", PACKAGE);
    fprintf (stderr, "Try '-h' for detailed help\n");
    exit (1);
  }

  /* Initialize the verbosity for the sl_log function */
  sl_loginit (verbose, NULL, NULL, NULL, NULL);

  /* Open dumpfile if requested */
  if (dumpfile)
  {
    if (!strcmp (dumpfile, "-"))
    {
      /* Re-direct all messages to standard error */
      sl_loginit (verbose, &print_stderr, NULL, &print_stderr, NULL);

      outfile = stdout;
      setvbuf (stdout, NULL, _IONBF, 0);
    }
    else if ((outfile = fopen (dumpfile, "a+b")) != NULL)
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
  if (streamfile)
    sl_read_streamlist (slconn, streamfile, selectors);

  if (timestart)
  {
    /* Parse and normalize time string */
    if ((nstime = ms_timestr2nstime (timestart)) == NSTERROR)
    {
      sl_log (2, 0, "start time not in recognized format: '%s' \n", timestart);
      return -1;
    }

    ms_nstime2timestr (nstime, timestr, ISOMONTHDAY_Z, NANO_MICRO_NONE);
    slconn->begin_time = strdup (timestr);
  }

  if (timeend)
  {
    /* Parse and normalize time string */
    if ((nstime = ms_timestr2nstime (timeend)) == NSTERROR)
    {
      sl_log (2, 0, "end time not in recognized format: '%s' \n", timeend);
      return -1;
    }

    ms_nstime2timestr (nstime, timestr, ISOMONTHDAY_Z, NANO_MICRO_NONE);
    slconn->end_time = strdup (timestr);
  }

  /* DEPRECATED, legacy support for time window argument */
  if (timewin)
  {
    char *startptr = strdup (timewin);
    char *endptr;

    if ((endptr = strchr (startptr, ':')) == NULL)
    {
      sl_log (2, 0, "time window (-tw) not in start:[end] format\n");
      return -1;
    }

    /* Terminate begin time part and increment pointer to end time start */
    *endptr = '\0';
    endptr++;

    if (startptr[0] != '\0')
    {
      sl_isodatetime (timestr, startptr); /* Convert SeedLink-style (comma) time string */

      if ((nstime = ms_timestr2nstime (timestr)) == NSTERROR)
      {
        sl_log (2, 0, "start time not in recognized format: '%s' \n", startptr);
        return -1;
      }

      ms_nstime2timestr (nstime, timestr, ISOMONTHDAY_Z, NANO_MICRO_NONE);
      slconn->begin_time = strdup (timestr);
    }

    if (endptr[0] != '\0')
    {
      sl_isodatetime (timestr, endptr); /* Convert SeedLink-style (comma) time string */

      if ((nstime = ms_timestr2nstime (timestr)) == NSTERROR)
      {
        sl_log (2, 0, "end time not in recognized format: '%s' \n", endptr);
        return -1;
      }

      ms_nstime2timestr (nstime, timestr, ISOMONTHDAY_Z, NANO_MICRO_NONE);
      slconn->end_time = strdup (timestr);
    }

    free (startptr);
  }

  /* Parse the 'multiselect' string following '-S' */
  if (multiselect)
  {
    if (sl_parse_streamlist (slconn, multiselect, selectors) == -1)
      return -1;
  }
  else if (slconn->streams == NULL && slconn->info == NULL)
  { /* No 'streams' array, assuming uni-station mode */
    sl_setuniparams (slconn, selectors, -1, 0);
  }

  /* Attempt to recover sequence numbers from state file */
  if (statefile)
  {
    /* Check if interval was specified for state saving */
    if ((tptr = strchr (statefile, ':')) != NULL)
    {
      char *tail;

      *tptr++ = '\0';

      stateint = (unsigned int)strtoul (tptr, &tail, 0);

      if (*tail || (stateint < 0 || stateint > 1e9))
      {
        sl_log (2, 0, "state saving interval specified incorrectly\n");
        return -1;
      }
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
 * print_samples:
 *
 * For binary sample types, 6 samples will be printed per line of output.
 * For text samples, 70 characters will be printed per line of output.
 *
 * Output will be limited to the first 'maxlines' lines of samples.
 * Set maxlines to zero to print all samples.
 *
 * Print paylod samples in the supplied record with a simple format.
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

    /* Print ellipsis is not all samples were printed */
    if (line < lines)
      sl_log (0, 0, "...\n");
  }
  else if (msr->sampletype == 't')
  {
    tdata = (char *)msr->datasamples;

    /* Print 70 character samples per line */
    lines    = (msr->numsamples / 70) + 1;
    maxlines = (maxlines <= 0) ? lines : maxlines;

    for (cnt = 0, line = 0;
         line < lines && line < maxlines;
         line++)
    {
      for (col = 0; col < 6; col++)
      {
        if (cnt < msr->numsamples)
        {
          sl_log (0, 0, "%.70s", &tdata[cnt]);
          cnt += 70;
        }
      }
      sl_log (0, 0, "\n");
    }

    /* Print ellipsis is not all samples were printed */
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
  char serverid[100];
  char site[100];
  int retval;

  retval = sl_ping (slconn, serverid, site);

  if (retval == 0)
  {
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
  int idx;
  int instring = 0;

  if (!json)
    return -1;

  /* Print JSON character-by-character, inserting
   * indentation, spaces and newlines for readability. */
  sl_log (0, 0, "%*s", indent, "");
  for (idx = 0; idx < jsonlength; idx++)
  {
    /* Toggle "in string" flag for double quotes */
    if (json[idx] == '"')
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
}  /* End of print_json() */

/***************************************************************************
 * report_environ:
 * Report (print) the state of global variables, intended for testing.
 ***************************************************************************/
static void
report_environ ()
{
  SLstream *curstream;

  sl_log (1, 0, "verbose:\t%d\n", verbose);
  sl_log (1, 0, "pingonly:\t%d\n", pingonly);

  if (dumpfile)
    sl_log (1, 0, "dumpfile:\t%s\n", dumpfile);
  else
    sl_log (1, 0, "'dumpfile' not defined\n");

  if (statefile)
    sl_log (1, 0, "statefile:\t%s\n", statefile);
  else
    sl_log (1, 0, "'statefile' not defined\n");

  if (slconn->sladdr)
    sl_log (1, 0, "sladdr:\t%s\n", slconn->sladdr);
  else
    sl_log (1, 0, "'slconn->sladdr' not defined\n");

  if (slconn->begin_time)
    sl_log (1, 0, "slconn->begin_time:\t%s\n", slconn->begin_time);
  else
    sl_log (1, 0, "'slconn->begin_time' not defined\n");
  if (slconn->end_time)
    sl_log (1, 0, "slconn->end_time:\t%s\n", slconn->end_time);
  else
    sl_log (1, 0, "'slconn->end_time' not defined\n");

  sl_log (1, 0, "slconn->dialup:\t%d\n", slconn->dialup);
  sl_log (1, 0, "slconn->multistation:\t%d\n", slconn->multistation);

  if (slconn->info)
    sl_log (1, 0, "slconn->info:\t%s\n", slconn->info);
  else
    sl_log (1, 0, "'slconn->info' not defined\n");

  sl_log (1, 0, "keepalive:\t%d\n", slconn->keepalive);
  sl_log (1, 0, "nettimeout:\t%d\n", slconn->netto);
  sl_log (1, 0, "netdelay:\t%d\n", slconn->netdly);

  sl_log (1, 0, "slconn->protocol:\t%s\n", sl_protocol_details(slconn->protocol, NULL, NULL));
  sl_log (1, 0, "slconn->server_protocols:\t%u (%s %s)\n",
          slconn->server_protocols,
          (slconn->server_protocols & SLPROTO3X) ? sl_protocol_details(SLPROTO3X, NULL, NULL) : "",
          (slconn->server_protocols & SLPROTO40) ? sl_protocol_details(SLPROTO40, NULL, NULL) : "");

  sl_log (1, 0, "slconn->link:\t%d\n", slconn->link);

  curstream = slconn->streams;

  sl_log (1, 0, "'streams' array:\n");
  while (curstream != NULL)
  {
    sl_log (1, 0, "Sta - netstaid: %s\n", curstream->netstaid);

    if (curstream->selectors)
      sl_log (1, 0, "Sta - selectors: %s\n", curstream->selectors);
    else
      sl_log (1, 0, "'selectors' not defined\n");

    sl_log (1, 0, "Sta - seqnum: %" PRIu64 "\n", curstream->seqnum);

    if (curstream->timestamp[0] != '\0')
      sl_log (1, 0, "Sta - timestamp: %s\n", curstream->timestamp);
    else
      sl_log (1, 0, "'timestamp' not defined\n");

    curstream = curstream->next;
  }
} /* End of report_environ() */

#ifndef SLP_WIN
/***************************************************************************
 * term_handler:
 * Signal handler routine to set the termination flag.
 ***************************************************************************/
static void
term_handler (int sig)
{
  sl_terminate (slconn);
}
#endif

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
           " -u              print unpacked samples of data packets\n\n"
           " -nd delay       network re-connect delay (seconds), default 30\n"
           " -nt timeout     network timeout (seconds), re-establish connection if no\n"
           "                   data/keepalives are received in this time, default 600\n"
           " -k interval     send keepalive (heartbeat) packets this often (seconds)\n"
           " -x sfile[:int]  save/restore stream state information to this file\n"
           " -d              configure the connection in dial-up mode\n"
           " -b              configure the connection in batch mode\n"
           "\n"
           " ## Data stream selection ##\n"
           " -s selectors    selectors for uni-station or default for multi-station mode\n"
           " -l listfile     read a stream list from this file for multi-station mode\n"
           " -S streams      define a stream list for multi-station mode\n"
           "   'streams' = 'stream1[:selectors1],stream2[:selectors2],...'\n"
           "        'stream' is in NET_STA format, for example:\n"
           "        -S \"IU_KONO:B_H_E B_H_N,GE_WLF,MN_AQU:H_H_?\"\n"
           "\n"
           " -ts starttime   specify a start time\n"
           " -te endtime     specify an end time\n"
           "\n"
           " ## Data saving options ##\n"
           " -o outfile      write all received records to this file\n"
           "\n"
           " ## Data server  information ## (requires SeedLink >= 3)\n"
           " -i type         send info request, type is one of the following:\n"
           "                   ID, CAPABILITIES, STATIONS, STREAMS, GAPS, CONNECTIONS, ALL\n"
           "                   the returned raw XML is displayed when using this option\n"
           " -I              print formatted server id and version\n"
           " -L              print formatted station list (if supported by server)\n"
           " -Q              print formatted stream list (if supported by server)\n"
           " -G              print formatted gap list (if supported by server)\n"
           " -C              print formatted connection list (if supported by server)\n"
           "\n"
           " [host][:][port] Address of the SeedLink server in host:port format\n"
           "                   Default host is 'localhost' and default port is '18000'\n");

} /* End of usage() */
