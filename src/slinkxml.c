/***************************************************************************
 * slinkxml.c
 * INFO message handling routines
 *
 * Written by:
 *   Chad Trabant, ORFEUS Data Center/MEREDIAN Project, IRIS/DMC
 *   Andres Heinloo, GFZ Potsdam GEOFON Project
 *
 * modified: 2007.038
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
  char *rootname = ezxml_name(xmldoc);
  
  if (strcmp (rootname, "seedlink"))
    {
      sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
      return;
    }
  
  printf ("SeedLink server: %s\n"
	  "Organization   : %s\n"
	  "Start time     : %s\n",
	  ezxml_attr (xmldoc, "software"),
	  ezxml_attr (xmldoc, "organization"),
	  ezxml_attr (xmldoc, "started"));
  
}  /* End of prtinfo_identification() */


/***************************************************************************
 * prtinfo_stations():
 * Format the specified XML document into a station list.
 ***************************************************************************/
void
prtinfo_stations (ezxml_t xmldoc)
{
  ezxml_t station;
  char *rootname = ezxml_name(xmldoc);
  
  if (strcmp (rootname, "seedlink"))
    {
      sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
      return;
    }
  
  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next(station))
    {
      printf ("%-2s %-5s %s\n",
	      ezxml_attr (station, "network"),
	      ezxml_attr (station, "name"),
	      ezxml_attr (station, "description"));
    }  
}  /* End of prtinfo_stations() */


/***************************************************************************
 * prtinfo_streams():
 * Format the specified XML document into a stream list.
 ***************************************************************************/
void
prtinfo_streams (ezxml_t xmldoc)
{
  ezxml_t station, stream;
  char *rootname = ezxml_name(xmldoc);
  
  if (strcmp (rootname, "seedlink"))
    {
      sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
      return;
    }
  
  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next(station))
    {
      const char *name, *network, *stream_check;
      
      name = ezxml_attr (station, "name");
      network = ezxml_attr (station, "network");
      stream_check = ezxml_attr (station, "stream_check");
      
      if ( !strcmp (stream_check, "enabled") )
	{
	  for (stream = ezxml_child (station, "stream"); stream; stream = ezxml_next(stream))
	    {	      
	      printf ("%-2s %-5s %-2s %-3s %s %s  -  %s\n", network, name,
		      ezxml_attr (stream, "location"),
		      ezxml_attr (stream, "seedname"),
		      ezxml_attr (stream, "type"),
		      ezxml_attr (stream, "begin_time"),
		      ezxml_attr (stream, "end_time"));
	    }
	}
      else
	{
	  sl_log (0, 1, "%-2s %-5s: No stream information, stream check disabled\n",
		  network, name);
	}
    }
}  /* End of prtinfo_streams() */


/***************************************************************************
 * prtinfo_gaps():
 * Format the specified XML document into a gap list.
 ***************************************************************************/
void
prtinfo_gaps (ezxml_t xmldoc)
{
  ezxml_t station, stream, gap;
  char *rootname = ezxml_name(xmldoc);
  
  if (strcmp (rootname, "seedlink"))
    {
      sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
      return;
    }
  
  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next(station))
    {
      const char *name, *network, *stream_check;
      
      name = ezxml_attr (station, "name");
      network = ezxml_attr (station, "network");
      stream_check = ezxml_attr (station, "stream_check");
      
      if ( !strcmp (stream_check, "enabled") )
	{
	  for (stream = ezxml_child (station, "stream"); stream; stream = ezxml_next(stream))
	    {
	      const char *location, *seedname, *type;
	      
	      location = ezxml_attr (stream, "location");
	      seedname = ezxml_attr (stream, "seedname");
	      type = ezxml_attr (stream, "type");
	      
	      for (gap = ezxml_child (stream, "gap"); gap; gap = ezxml_next(gap))
		{
		  printf ("%-2s %-5s %-2s %-3s %s %s  -  %s\n", network, name,
			  location, seedname, type,
			  ezxml_attr (gap, "begin_time"),
			  ezxml_attr (gap, "end_time"));
		}
	    }
	}
      else
	{
	  sl_log (0, 1, "%-2s %-5s: No gap information, stream check disabled\n",
		  network, name);
	}
    }
}  /* End of prtinfo_gaps() */


/***************************************************************************
 * prtinfo_connections():
 * Format the specified XML document into a connection list.
 ***************************************************************************/
void
prtinfo_connections (ezxml_t xmldoc)
{
  ezxml_t station, connection;
  char *rootname = ezxml_name(xmldoc);
  
  if (strcmp (rootname, "seedlink"))
    {
      sl_log (1, 0, "XML INFO root tag is not <seedlink>, invalid data\n");
      return;
    }
  
  printf
    ("STATION  REMOTE ADDRESS        CONNECTION ESTABLISHED   TX COUNT GAPS  QLEN FLG\n");
  printf
    ("-------------------------------------------------------------------------------\n");
  /* GE TRTE  255.255.255.255:65536 2002/08/01 11:00:00.0000 12345678 1234 12345 DSE */

  for (station = ezxml_child (xmldoc, "station"); station; station = ezxml_next(station))
    {
      const char *network, *name, *end_seq;
      
      network = ezxml_attr (station, "network");
      name = ezxml_attr (station, "name");
      end_seq = ezxml_attr (station, "end_seq");
      
      for (connection = ezxml_child (station, "connection"); connection; connection = ezxml_next(connection))
	{
	  unsigned long qlen = 0;
	  int active = 0, window = 0, realtime = 0, selectors = 0, eod = 0;
	  const char *current_seq;
	  char address[25];
	  char flags[4] = { ' ', ' ', ' ', 0 };
	  
	  window = (ezxml_child (connection, "window")) ? 1 : 0;
	  selectors = (ezxml_child (connection, "selector")) ? 1 : 0;
	  
	  current_seq = ezxml_attr (connection, "current_seq");
	  
	  if (strcmp (current_seq, "unset"))
	    {
	      qlen = (strtoul (ezxml_attr (station, "end_seq"), NULL, 16) -
		      strtoul (ezxml_attr (connection, "current_seq"), NULL, 16)) & 0xffffff;
	      active = 1;
	    }
	  
	  realtime =  (strcmp (ezxml_attr (connection, "realtime"), "no")) ? 1 : 0;
	  eod = (strcmp (ezxml_attr (connection, "end_of_data"), "no")) ? 1 : 0;
	  
	  if (!active)
	    flags[0] = 'O';	/* Connection opened, but not configured */
	  else if (window)
	    flags[0] = 'W';	/* Window extraction (TIME) mode */
	  else if (!realtime)
	    flags[0] = 'D';	/* Dial-up mode */
	  else
	    flags[0] = 'R';	/* Normal real-time mode */
	  
	  if (selectors)
	    flags[1] = 'S';	/* Using selectors */
	  
	  if (eod)
	    flags[2] = 'E';	/* Connection is waiting to be closed */

	  sprintf (address, "%.15s:%.5s",
		   ezxml_attr (connection, "host"),
		   ezxml_attr (connection, "port"));
	  
	  printf ("%-2s %-5s %-21s %s %8s %4s ", network, name, address,
		  ezxml_attr (connection, "ctime"),
		  ezxml_attr (connection, "txcount"),
		  ezxml_attr (connection, "sequence_gaps"));
	  
	  if (realtime && active)
	    printf ("%5lu ", qlen);
	  else
	    printf ("    - ");

	  printf ("%s\n", flags);
	}
    }
}  /* End of prtinfo_connections() */
