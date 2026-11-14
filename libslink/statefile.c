/***************************************************************************
 * statefile.c:
 *
 * Routines to save and recover SeedLink sequence numbers to/from a file.
 *
 * This file is part of the SeedLink Library.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2024:
 * @author Chad Trabant, EarthScope Data Services
 ***************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libslink.h"
#include "slinternal.h"

#define HEADER_V2 "#V2 StationID  Sequence  [Timestamp]"

/** ************************************************************************
 * @brief Save the sequence numbers and time stamps into the given state file.
 *
 * The state file is a simple text file with one line per stream containing
 * the station identifier, sequence number, and time stamp.  This information
 * is intended to be used to re-start a connection and continue from where it
 * left off.
 *
 * The current "V2" line format is (header line and example):
 * ```
 *   #V2 StationID  Sequence  [Timestamp]
 *   IU_COLA       1234567890 2024-08-03T17:23:18.0Z
 * ```
 *
 * In multi-station mode, entries with wildcarded station IDs are written
 * with an `UNSET` sequence number and no time stamp.  Stations received
 * via such entries have their own lines and are resumed individually.
 *
 * The state is written to `<statefile>.tmp`, which then replaces
 * \a statefile, so the directory containing it must be writable.
 *
 * @param slconn    The ::SLCD connection to save state
 * @param statefile The name of the state file to write
 *
 * @returns 0 on success and -1 on error
 *
 * @sa sl_recoverstate()
 ***************************************************************************/
int
sl_savestate (SLCD *slconn, const char *statefile)
{
  SLstream *curstream;
  FILE *fp;
  char *tmppath;
  size_t tmppathsize;
  char line[200];
  int linelen;

  if (!slconn || !statefile)
    return -1;

  curstream = slconn->streams;

  /* An interrupted write leaves the previous state file in place */
  tmppathsize = strlen (statefile) + 5;
  if ((tmppath = (char *)malloc (tmppathsize)) == NULL)
  {
    sl_log_r (slconn, 2, 0, "cannot allocate memory for state file name\n");
    return -1;
  }
  snprintf (tmppath, tmppathsize, "%s.tmp", statefile);

  if ((fp = fopen (tmppath, "wb")) == NULL)
  {
    sl_log_r (slconn, 2, 0, "cannot open %s for writing, %s\n", tmppath, strerror (errno));
    free (tmppath);
    return -1;
  }

  sl_log_r (slconn, 1, 2, "saving connection state to state file\n");

  /* Write header line */
  if (fputs (HEADER_V2 "\n", fp) == EOF)
  {
    sl_log_r (slconn, 2, 0, "cannot write to state file, %s\n", strerror (errno));
    goto error;
  }

  /* Traverse stream list and write sequence numbers and time stamps*/
  while (curstream != NULL)
  {
    /* Wildcarded entries in multi-station mode are resumed via the entries
     * of the stations they matched; their own sequence numbers are not used */
    if (slconn->multistation && strpbrk (curstream->stationid, "*?"))
      linelen = snprintf (line, sizeof (line), "%s UNSET\n", curstream->stationid);
    else if (curstream->seqnum == SL_UNSETSEQUENCE)
      linelen = snprintf (line, sizeof (line), "%s UNSET %s\n", curstream->stationid,
                          curstream->timestamp);
    else
      linelen = snprintf (line, sizeof (line), "%s %" PRIu64 " %s\n", curstream->stationid,
                          curstream->seqnum, curstream->timestamp);

    if (linelen <= 0)
    {
      sl_log_r (slconn, 2, 0, "Error creating state file entry for: %s\n", curstream->stationid);
    }
    else if (fputs (line, fp) == EOF)
    {
      sl_log_r (slconn, 2, 0, "cannot write to state file, %s\n", strerror (errno));
      goto error;
    }

    curstream = curstream->next;
  }

  if (fflush (fp))
  {
    sl_log_r (slconn, 2, 0, "cannot write to state file, %s\n", strerror (errno));
    goto error;
  }

#if defined(SLP_WIN)
  if (_commit (_fileno (fp)))
#else
  if (fsync (fileno (fp)))
#endif
  {
    sl_log_r (slconn, 2, 0, "cannot sync state file, %s\n", strerror (errno));
    goto error;
  }

  if (fclose (fp))
  {
    fp = NULL;
    sl_log_r (slconn, 2, 0, "cannot close state file, %s\n", strerror (errno));
    goto error;
  }
  fp = NULL;

#if defined(SLP_WIN)
  if (!MoveFileExA (tmppath, statefile, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
  {
    sl_log_r (slconn, 2, 0, "cannot replace state file %s, error %lu\n", statefile,
              (unsigned long)GetLastError ());
    goto error;
  }
#else
  if (rename (tmppath, statefile))
  {
    sl_log_r (slconn, 2, 0, "cannot replace state file %s, %s\n", statefile, strerror (errno));
    goto error;
  }
#endif

  free (tmppath);
  return 0;

error:
  if (fp)
    fclose (fp);
  remove (tmppath);
  free (tmppath);
  return -1;
} /* End of sl_savestate() */

/** ************************************************************************
 * @brief Recover the state of a SeedLink connection from a file.
 *
 * The specified state file is read, and details of streams, selectors, etc.
 * are added to the specified ::SLCD.
 *
 * The state file format is a simple text file with one line per stream
 * containing the station identifier, sequence number, and time stamp.
 *
 * The current "V2" line format is (header line and examples):
 * ```
 * #V2 StationID  Sequence  [Timestamp]
 * IU_COLA       1234567890 2024-08-03T17:23:18.0Z
 * XX_NONE       UNSET
 * ```
 *
 * where the timestamp is optional, and the special value "UNSET" can be
 * used to indicate that the sequence number is not set.
 *
 * The supported legacy line format takes this form (no header):
 * ```
 * Network Station Sequence#  [Timestamp]
 * IU      COLA    1234567890 2024,08,03,17,23,18
 * XX      NONE    -1
 *```
 *
 * @param slconn    The ::SLCD connection to restore from a saved state
 * @param statefile The name of the state file to read
 *
 * @returns status of the operation:
 * @retval -1 : error
 * @retval  0 : completed successfully
 * @retval  1 : file could not be opened (probably not found)
 *
 * @sa sl_savestate()
 ***************************************************************************/
int
sl_recoverstate (SLCD *slconn, const char *statefile)
{
  SLstream *curstream;
  FILE *fp;

  char line[200];
  char *field[5];
  int format = 0;
  int fields;
  int idx;
  int retval = 0;

  char stationid[22] = {0};
  char timestamp[32] = {0};
  char *stationstr;
  char *sequencestr;
  char *timestr;
  char *endptr;
  char *ptr;

  uint64_t seqnum;
  int count;

  if (!slconn || !statefile)
    return -1;

  /* Open the state file */
  if ((fp = fopen (statefile, "rb")) == NULL)
  {
    if (errno == ENOENT)
    {
      sl_log_r (slconn, 1, 0, "could not find state file: %s\n", statefile);
      return 1;
    }
    else
    {
      sl_log_r (slconn, 2, 0, "could not open state file, %s\n", strerror (errno));
      return -1;
    }
  }

  sl_log_r (slconn, 1, 1, "recovering connection state from state file: %s\n", statefile);

  count = 1;

  while (fgets (line, sizeof (line), fp))
  {
    stationstr = NULL;
    sequencestr = NULL;
    timestr = NULL;

    if (sli_linetoolong (fp, line))
    {
      sl_log_r (slconn, 2, 0, "line %d of state file is too long: %.40s...\n", count, line);
      retval = -1;
      count++;
      continue;
    }

    /* Terminate at first carriage return or newline */
    if ((ptr = strchr (line, '\r')) != NULL || (ptr = strchr (line, '\n')) != NULL)
      *ptr = '\0';

    /* Store pointers to space-separated fields & convert spaces to terminators */
    for (idx = 0, fields = 0; line[idx] && fields < 5; idx++)
    {
      if (!isspace ((unsigned char)line[idx]))
      {
        if (idx == 0 || line[idx - 1] == '\0')
        {
          field[fields] = line + idx;
          fields++;
        }
      }
      else
      {
        line[idx] = '\0';
      }
    }

    /* Check for recognized version declaration */
    if (fields >= 1 && strncasecmp (field[0], "#V2", 3) == 0)
    {
      format = 2;
      count++;
      continue;
    }
    /* Skip empty or comment lines */
    if (fields == 0 || *field[0] == '#')
    {
      count++;
      continue;
    }

    /* Format V2: StationID Sequence# [Timestamp] */
    if (format == 2 && fields >= 2)
    {
      stationstr = field[0];
      sequencestr = field[1];
      timestr = (fields >= 3) ? field[2] : NULL;
    }
    /* Legacy format: NET STA Sequence# [Timestamp] */
    else if (format == 0 && fields >= 3)
    {
      /* Convert special case of legacy uni-station entry to all-station mode */
      if (strcmp (field[0], "XX") == 0 && strcmp (field[1], "UNI") == 0)
      {
        strcpy (stationid, "*");
      }
      else if ((size_t)snprintf (stationid, sizeof (stationid), "%s_%s", field[0], field[1]) >=
               sizeof (stationid))
      {
        sl_log_r (slconn, 2, 0, "station ID on line %d of state file is too long: %s_%s\n", count,
                  field[0], field[1]);
        retval = -1;
        count++;
        continue;
      }

      stationstr = stationid;
      sequencestr = field[2];
      timestr = (fields >= 4) ? field[3] : NULL;
    }
    else
    {
      sl_log_r (slconn, 2, 0, "could not parse line %d of state file: %s\n", count, line);
      retval = -1;
      count++;
      continue;
    }

    /* Validate the timestamp, converting the legacy SeedLink comma-delimited
     * date-time to ISO-compatible format if needed (already ISO in V2):
     * Example: '2021,11,19,17,23,18' => '2021-11-18T17:23:18.0Z' */
    if (timestr)
    {
      if (strlen (timestr) > sizeof (timestamp) - 2)
      {
        sl_log_r (slconn, 1, 0, "timestamp on line %d of statefile is too long: '%s'\n", count,
                  timestr);
        retval = -1;
        count++;
        continue;
      }

      if (sl_isodatetime (timestamp, timestr) != NULL)
      {
        timestr = timestamp;
      }
      else
      {
        sl_log_r (slconn, 1, 0, "could not convert timestamp on line %d of statefile: '%s'\n",
                  count, timestr);
        retval = -1;
        count++;
        continue;
      }
    }

    if (strcmp (sequencestr, "UNSET") == 0 || strcmp (sequencestr, "-1") == 0)
    {
      seqnum = SL_UNSETSEQUENCE;
    }
    else
    {
      errno = 0;
      seqnum = (uint64_t)strtoull (sequencestr, &endptr, 10);

      /* strtoull() accepts a sign and wraps negative values */
      if (!isdigit ((unsigned char)*sequencestr) || *endptr || errno == ERANGE)
      {
        sl_log_r (slconn, 2, 0,
                  "could not parse sequence number from line %d of state file: '%s'\n", count,
                  sequencestr);
        retval = -1;
        count++;
        continue;
      }
    }

    /* Set the sequence number and time stamp for a matching entry in the stream list */
    curstream = slconn->streams;
    while (curstream != NULL)
    {
      if (!strcmp (stationstr, curstream->stationid))
      {
        curstream->seqnum = seqnum;

        if (timestr)
        {
          strncpy (curstream->timestamp, timestr, sizeof (curstream->timestamp) - 1);
          curstream->timestamp[sizeof (curstream->timestamp) - 1] = '\0';
        }

        break;
      }

      curstream = curstream->next;
    }

    /* Add an entry for a station previously received via a wildcarded entry */
    if (curstream == NULL &&
        sli_add_matchedstream (slconn, stationstr, seqnum, timestr) < 0)
    {
      retval = -1;
    }

    count++;
  }

  if (ferror (fp))
  {
    sl_log_r (slconn, 2, 0, "file read error for %s\n", statefile);
    retval = -1;
  }

  if (fclose (fp))
  {
    sl_log_r (slconn, 2, 0, "could not close state file, %s\n", strerror (errno));
    retval = -1;
  }

  return retval;
} /* End of sl_recoverstate() */
