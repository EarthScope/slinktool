/***************************************************************************
 * slinkxml.c
 *
 * INFO message printing routines
 *
 * Written by:
 *   Chad Trabant, ORFEUS Data Center/MEREDIAN Project, IRIS/DMC
 *   Andres Heinloo, GFZ Potsdam GEOFON Project
 ***************************************************************************/

#include <stdio.h>
#include <string.h>

#include <libslink.h>

#include "slinkxml.h"

/***************************************************************************
 * prtinfo_identification():
 * Format the specified XML document into an identification summary.
 ***************************************************************************/
void
prtinfo_identification (ezxml_t xmldoc)
{
  char *rootname = ezxml_name (xmldoc);

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  const char *software     = ezxml_attr (xmldoc, "software");
  const char *organization = ezxml_attr (xmldoc, "organization");
  const char *started      = ezxml_attr (xmldoc, "started");

  printf ("SeedLink server: %s\n"
          "Organization   : %s\n"
          "Start time     : %s\n",
          (software) ? software : "",
          (organization) ? organization : "",
          (started) ? started : "");

} /* End of prtinfo_identification() */

/***************************************************************************
 * prtinfo_stations():
 * Format the specified XML document into a station list.
 ***************************************************************************/
void
prtinfo_stations (ezxml_t xmldoc)
{
  ezxml_t station;
  char *rootname    = ezxml_name (xmldoc);
  int station_count = 0;

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next (station))
  {
    const char *network     = ezxml_attr (station, "network");
    const char *name        = ezxml_attr (station, "name");
    const char *description = ezxml_attr (station, "description");

    printf ("%-2s %-5s %s\n",
            (network) ? network : "",
            (name) ? name : "",
            (description) ? description : "");

    station_count++;
  }

  if (station_count == 0)
  {
    sl_log (0, 1, "No station information received\n");
  }
} /* End of prtinfo_stations() */

/***************************************************************************
 * prtinfo_streams():
 * Format the specified XML document into a stream list.
 ***************************************************************************/
void
prtinfo_streams (ezxml_t xmldoc)
{
  ezxml_t station, stream;
  char *rootname   = ezxml_name (xmldoc);
  int stream_count = 0;

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next (station))
  {
    const char *name    = ezxml_attr (station, "name");
    const char *network = ezxml_attr (station, "network");

    stream_count = 0;
    for (stream = ezxml_child (station, "stream"); stream; stream = ezxml_next (stream))
    {
      const char *location   = ezxml_attr (stream, "location");
      const char *seedname   = ezxml_attr (stream, "seedname");
      const char *type       = ezxml_attr (stream, "type");
      const char *begin_time = ezxml_attr (stream, "begin_time");
      const char *end_time   = ezxml_attr (stream, "end_time");

      printf ("%-2s %-5s %-2s %-3s %s %s  -  %s\n",
              (network) ? network : "",
              (name) ? name : "",
              (location) ? location : "",
              (seedname) ? seedname : "",
              (type) ? type : "",
              (begin_time) ? begin_time : "",
              (end_time) ? end_time : "");

      stream_count++;
    }

    if (stream_count == 0)
    {
      sl_log (0, 1, "%-2s %-5s: No stream information received\n",
              (network) ? network : "",
              (name) ? name : "");
    }
  }
} /* End of prtinfo_streams() */

/***************************************************************************
 * prtinfo_gaps():
 * Format the specified XML document into a gap list.
 ***************************************************************************/
void
prtinfo_gaps (ezxml_t xmldoc)
{
  ezxml_t station, stream, gap;
  char *rootname = ezxml_name (xmldoc);
  int gap_count  = 0;

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next (station))
  {
    const char *name    = ezxml_attr (station, "name");
    const char *network = ezxml_attr (station, "network");

    gap_count = 0;
    for (stream = ezxml_child (station, "stream"); stream; stream = ezxml_next (stream))
    {
      const char *location = ezxml_attr (stream, "location");
      const char *seedname = ezxml_attr (stream, "seedname");
      const char *type     = ezxml_attr (stream, "type");

      for (gap = ezxml_child (stream, "gap"); gap; gap = ezxml_next (gap))
      {
        const char *begin_time = ezxml_attr (gap, "begin_time");
        const char *end_time   = ezxml_attr (gap, "end_time");

        printf ("%-2s %-5s %-2s %-3s %s %s  -  %s\n",
                (network) ? network : "",
                (name) ? name : "",
                (location) ? location : "",
                (seedname) ? seedname : "",
                (type) ? type : "",
                (begin_time) ? begin_time : "",
                (end_time) ? end_time : "");
      }

      gap_count++;
    }

    if (gap_count == 0)
    {
      sl_log (0, 1, "%-2s %-5s: No gap information received\n",
              (network) ? network : "",
              (name) ? name : "");
    }
  }
} /* End of prtinfo_gaps() */

/***************************************************************************
 * prtinfo_connections():
 * Format the specified XML document into a connection list.
 ***************************************************************************/
void
prtinfo_connections (ezxml_t xmldoc)
{
  ezxml_t station, connection;
  char *rootname = ezxml_name (xmldoc);

  if (rootname == NULL || strcmp (rootname, "seedlink"))
  {
    sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
    return;
  }

  printf ("STATION  REMOTE ADDRESS        CONNECTION ESTABLISHED   TX COUNT GAPS  QLEN FLG\n");
  printf ("-------------------------------------------------------------------------------\n");
  /* GE TRTE  255.255.255.255:65536 2002/08/01 11:00:00.0000 12345678 1234 12345 DSE */

  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next (station))
  {
    const char *network = ezxml_attr (station, "network");
    const char *name    = ezxml_attr (station, "name");

    for (connection = ezxml_child (station, "connection"); connection; connection = ezxml_next (connection))
    {
      unsigned long qlen = 0;
      int active = 0, window = 0, realtime = 0, selectors = 0, eod = 0;
      const char *current_seq;
      char address[256];
      char flags[4] = {' ', ' ', ' ', 0};

      window    = (ezxml_child (connection, "window")) ? 1 : 0;
      selectors = (ezxml_child (connection, "selector")) ? 1 : 0;

      current_seq = ezxml_attr (connection, "current_seq");

      if (strcmp (current_seq, "unset"))
      {
        qlen = (strtoul (ezxml_attr (station, "end_seq"), NULL, 16) -
                strtoul (ezxml_attr (connection, "current_seq"), NULL, 16)) &
               0xffffff;
        active = 1;
      }

      realtime = (strcmp (ezxml_attr (connection, "realtime"), "no")) ? 1 : 0;
      eod      = (strcmp (ezxml_attr (connection, "end_of_data"), "no")) ? 1 : 0;

      if (!active)
        flags[0] = 'O'; /* Connection opened, but not configured */
      else if (window)
        flags[0] = 'W'; /* Window extraction (TIME) mode */
      else if (!realtime)
        flags[0] = 'D'; /* Dial-up mode */
      else
        flags[0] = 'R'; /* Normal real-time mode */

      if (selectors)
        flags[1] = 'S'; /* Using selectors */

      if (eod)
        flags[2] = 'E'; /* Connection is waiting to be closed */

      const char *host = ezxml_attr (connection, "host");
      const char *port = ezxml_attr (connection, "port");

      sprintf (address, "%s:%s",
               (host) ? host : "",
               (port) ? port : "");

      const char *ctime         = ezxml_attr (connection, "ctime");
      const char *txcount       = ezxml_attr (connection, "txcount");
      const char *sequence_gaps = ezxml_attr (connection, "sequence_gaps");

      printf ("%-2s %-5s %-21s %s %8s %4s ",
              (network) ? network : "",
              (name) ? name : "",
              address,
              (ctime) ? ctime : "",
              (txcount) ? txcount : "",
              (sequence_gaps) ? sequence_gaps : "");

      if (realtime && active)
        printf ("%5lu ", qlen);
      else
        printf ("    - ");

      printf ("%s\n", flags);
    }
  }
} /* End of prtinfo_connections() */
