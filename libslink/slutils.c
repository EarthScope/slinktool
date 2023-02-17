/***************************************************************************
 * slutils.c
 *
 * Routines for managing a connection with a SeedLink server
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
 * Copyright (C) 2022:
 * @author Chad Trabant, EarthScope Data Services
 ***************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globmatch.h"
#include "libslink.h"
#include "slplatform.h"
#include "mseedformat.h"

/* Function(s) only used in this source file */
static int update_stream (SLCD *slconn, const SLpacket *slpack);
static int detect (const char *record, uint64_t recbuflen, uint8_t *formatversion);

/***************************************************************************
 * sl_collect:
 *
 * Routine to manage a connection to a SeedLink server based on the values
 * given in the slconn struct and collect data.
 *
 * Designed to run in a tight loop at the heart of a client program, this
 * function will return every time a packet is received.
 *
 * Returns SLPACKET when something is received and sets the slpack
 * pointer to the received packet.  When the connection was closed by
 * the server or the termination sequence completed SLTERMINATE is
 * returned and the slpack pointer is set to NULL.
 ***************************************************************************/
int
sl_collect (SLCD *slconn, SLpacket **slpack)
{
  int bytesread;
  double current_time;
  char retpacket;
  int bufferlen;
  uint8_t formatversion = 0;

  /* For select()ing during the read loop */
  struct timeval select_tv;
  fd_set select_fd;
  int select_ret;

  *slpack = NULL;

  /* Check if the info was set */
  if (slconn->info != NULL)
  {
    slconn->stat->query_mode = InfoQuery;
  }

  /* If the connection is not up check the SLCD and reset the timing variables */
  if (slconn->link == -1)
  {
    if (sl_checkslcd (slconn))
    {
      sl_log_r (slconn, 2, 0, "problems with the connection description\n");
      return SLTERMINATE;
    }

    slconn->stat->netto_trig     = -1; /* Init net timeout trigger to reset state */
    slconn->stat->keepalive_trig = -1; /* Init keepalive trigger to reset state */
  }

  /* Start the primary loop  */
  while (1)
  {
    if (!slconn->terminate)
    {
      if (slconn->link == -1)
      {
        slconn->stat->sl_state = SL_DOWN;
      }

      /* Check for network timeout */
      if (slconn->stat->sl_state == SL_DATA && slconn->netto && slconn->stat->netto_trig > 0)
      {
        sl_log_r (slconn, 1, 0, "network timeout (%ds), reconnecting in %ds\n",
                  slconn->netto, slconn->netdly);
        slconn->link              = sl_disconnect (slconn);
        slconn->stat->sl_state    = SL_DOWN;
        slconn->stat->netto_trig  = -1;
        slconn->stat->netdly_trig = -1;
      }

      /* Check if a keepalive packet needs to be sent */
      if (slconn->stat->sl_state == SL_DATA && !slconn->stat->expect_info &&
          slconn->keepalive && slconn->stat->keepalive_trig > 0)
      {
        sl_log_r (slconn, 1, 2, "sending keepalive request\n");

        if (sl_send_info (slconn, "ID", 3) != -1)
        {
          slconn->stat->query_mode     = KeepAliveQuery;
          slconn->stat->expect_info    = 1;
          slconn->stat->keepalive_trig = -1;
        }
      }

      /* Check if an in-stream INFO request needs to be sent */
      if (slconn->stat->sl_state == SL_DATA && !slconn->stat->expect_info && slconn->info)
      {
        if (sl_send_info (slconn, slconn->info, 1) != -1)
        {
          slconn->stat->query_mode  = InfoQuery;
          slconn->stat->expect_info = 1;
        }
        else
        {
          slconn->stat->query_mode = NoQuery;
        }

        slconn->info = NULL;
      }

      /* Throttle the loop while delaying */
      if (slconn->stat->sl_state == SL_DOWN && slconn->stat->netdly_trig > 0)
      {
        slp_usleep (500000);
      }

      /* Connect to remote SeedLink */
      if (slconn->stat->sl_state == SL_DOWN && slconn->stat->netdly_trig == 0)
      {
        if (sl_connect (slconn, 1) != -1)
        {
          slconn->stat->sl_state = SL_UP;
        }
        slconn->stat->netto_trig     = -1;
        slconn->stat->netdly_trig    = -1;
        slconn->stat->keepalive_trig = -1;
      }

      /* Negotiate/configure the connection */
      if (slconn->stat->sl_state == SL_UP)
      {
        int slconfret = 0;

        /* Only send query if a query is set and no streams are defined,
         * if streams are defined we'll send the query after configuration. */
        if (slconn->info && slconn->streams == NULL)
        {
          if (sl_send_info (slconn, slconn->info, 1) != -1)
          {
            slconn->stat->query_mode  = InfoQuery;
            slconn->stat->expect_info = 1;
          }
          else
          {
            slconn->stat->query_mode  = NoQuery;
            slconn->stat->expect_info = 0;
          }

          slconn->info = NULL;
        }
        else
        {
          slconfret                 = sl_configlink (slconn);
          slconn->stat->expect_info = 0;
        }

        if (slconfret != -1)
        {
          slconn->stat->recptr   = 0; /* initialize the data buffer pointers */
          slconn->stat->sendptr  = 0;
          slconn->stat->sl_state = SL_DATA;
        }
        else
        {
          sl_log_r (slconn, 2, 0, "negotiation with remote SeedLink failed\n");
          slconn->link              = sl_disconnect (slconn);
          slconn->stat->netdly_trig = -1;
        }
      }
    }
    else /* We are terminating */
    {
      if (slconn->link != -1)
      {
        slconn->link = sl_disconnect (slconn);
      }

      slconn->stat->sl_state = SL_DOWN;
    }

    /* Process data in buffer */
    while (slconn->stat->recptr - slconn->stat->sendptr >= SLHEADSIZE + SLRECSIZEMIN)
    {
      bufferlen = slconn->stat->recptr - slconn->stat->sendptr;
      retpacket = 1;

      slconn->stat->slpack.slhead = &slconn->stat->databuf[slconn->stat->sendptr];
      slconn->stat->slpack.msrecord = &slconn->stat->databuf[slconn->stat->sendptr + SLHEADSIZE];
      slconn->stat->slpack.reclen = detect (slconn->stat->slpack.msrecord, bufferlen, &formatversion);

      /* Return error if no miniSEED could be detected */
      if (slconn->stat->slpack.reclen < 0)
      {
        sl_log_r (slconn, 2, 0, "%s(): non-miniSEED packet received!?! Terminating.\n", __func__);
        return SLTERMINATE;
      }

      /* Stop processing if the buffer contains miniSEED but not enough data */
      if (slconn->stat->slpack.reclen == 0 ||
          (slconn->stat->slpack.reclen > 0 && slconn->stat->slpack.reclen + SLHEADSIZE > bufferlen))
      {
        break;
      }

      /* Process an INFO packet */
      if (!memcmp (slconn->stat->slpack.slhead, INFOSIGNATURE, 6))
      {
        char terminator;

        terminator = (slconn->stat->slpack.slhead[SLHEADSIZE - 1] != '*');

        if (!slconn->stat->expect_info)
        {
          sl_log_r (slconn, 2, 0, "unexpected INFO packet received, skipping\n");
        }
        else
        {
          if (terminator)
          {
            slconn->stat->expect_info = 0;
          }

          /* Keep alive packets are not returned */
          if (slconn->stat->query_mode == KeepAliveQuery)
          {
            retpacket = 0;

            if (!terminator)
            {
              sl_log_r (slconn, 2, 0, "non-terminated keep-alive packet received!?!\n");
            }
            else
            {
              sl_log_r (slconn, 1, 2, "keepalive packet received\n");
            }
          }
        }

        if (slconn->stat->query_mode != NoQuery)
        {
          slconn->stat->query_mode = NoQuery;
        }
      }
      else /* Update the stream chain entry if not an INFO packet */
      {
        if ((update_stream (slconn, &slconn->stat->slpack)) == -1)
        {
          /* If updating didn't work the packet is broken */
          retpacket = 0;
        }
      }

      /* Increment the send pointer */
      slconn->stat->sendptr += (SLHEADSIZE + slconn->stat->slpack.reclen);

      /* Return packet */
      if (retpacket)
      {
        *slpack = &slconn->stat->slpack;
        return SLPACKET;
      }
    } /* Done processing data in recv buffer */

    /* A trap door for terminating, all complete data packets from the buffer
       have been sent to the caller */
    if (slconn->terminate)
    {
      return SLTERMINATE;
    }

    /* After processing the packet buffer shift the data */
    if (slconn->stat->sendptr)
    {
      memmove (slconn->stat->databuf,
               &slconn->stat->databuf[slconn->stat->sendptr],
               slconn->stat->recptr - slconn->stat->sendptr);

      slconn->stat->recptr -= slconn->stat->sendptr;
      slconn->stat->sendptr = 0;
    }

    /* Catch cases where the data stream stopped */
    if ((slconn->stat->recptr - slconn->stat->sendptr) == 7 &&
        !strncmp (&slconn->stat->databuf[slconn->stat->sendptr], "ERROR\r\n", 7))
    {
      sl_log_r (slconn, 2, 0, "SeedLink server reported an error with the last command\n");
      slconn->link = sl_disconnect (slconn);
      return SLTERMINATE;
    }

    if ((slconn->stat->recptr - slconn->stat->sendptr) == 3 &&
        !strncmp (&slconn->stat->databuf[slconn->stat->sendptr], "END", 3))
    {
      sl_log_r (slconn, 1, 1, "End of buffer or selected time window\n");
      slconn->link = sl_disconnect (slconn);
      return SLTERMINATE;
    }

    /* Read incoming data if connection is up */
    if (slconn->stat->sl_state == SL_DATA)
    {
      /* Check for more available data from the socket */
      bytesread = 0;

      /* Poll the server */
      FD_ZERO (&select_fd);
      FD_SET ((unsigned int)slconn->link, &select_fd);
      select_tv.tv_sec  = 0;
      select_tv.tv_usec = 500000; /* Block up to 0.5 seconds */

      select_ret = select ((slconn->link + 1), &select_fd, NULL, NULL, &select_tv);

      /* Check the return from select(), an interrupted system call error
         will be reported if a signal handler was used.  If the terminate
         flag is set this is not an error. */
      if (select_ret > 0)
      {
        if (!FD_ISSET (slconn->link, &select_fd))
        {
          sl_log_r (slconn, 2, 0, "select() reported data but socket not in set!\n");
        }
        else
        {
          bytesread = sl_recvdata (slconn, (void *)&slconn->stat->databuf[slconn->stat->recptr],
                                   BUFSIZE - slconn->stat->recptr, slconn->sladdr);
        }
      }
      else if (select_ret < 0 && !slconn->terminate)
      {
        sl_log_r (slconn, 2, 0, "select() error: %s\n", slp_strerror ());
        slconn->link              = sl_disconnect (slconn);
        slconn->stat->netdly_trig = -1;
      }

      if (bytesread < 0) /* read() failed */
      {
        slconn->link              = sl_disconnect (slconn);
        slconn->stat->netdly_trig = -1;
      }
      else if (bytesread > 0) /* Data is here, process it */
      {
        slconn->stat->recptr += bytesread;

        /* Reset the timeout and keepalive timers */
        slconn->stat->netto_trig     = -1;
        slconn->stat->keepalive_trig = -1;
      }
    }

    /* Update timing variables */
    current_time = sl_dtime ();

    /* Network timeout timing logic */
    if (slconn->netto)
    {
      if (slconn->stat->netto_trig == -1) /* reset timer */
      {
        slconn->stat->netto_time = current_time;
        slconn->stat->netto_trig = 0;
      }
      else if (slconn->stat->netto_trig == 0 &&
               (current_time - slconn->stat->netto_time) > slconn->netto)
      {
        slconn->stat->netto_trig = 1;
      }
    }

    /* Keepalive/heartbeat interval timing logic */
    if (slconn->keepalive)
    {
      if (slconn->stat->keepalive_trig == -1) /* reset timer */
      {
        slconn->stat->keepalive_time = current_time;
        slconn->stat->keepalive_trig = 0;
      }
      else if (slconn->stat->keepalive_trig == 0 &&
               (current_time - slconn->stat->keepalive_time) > slconn->keepalive)
      {
        slconn->stat->keepalive_trig = 1;
      }
    }

    /* Network delay timing logic */
    if (slconn->netdly)
    {
      if (slconn->stat->netdly_trig == -1) /* reset timer */
      {
        slconn->stat->netdly_time = current_time;
        slconn->stat->netdly_trig = 1;
      }
      else if (slconn->stat->netdly_trig == 1 &&
               (current_time - slconn->stat->netdly_time) > slconn->netdly)
      {
        slconn->stat->netdly_trig = 0;
      }
    }
  } /* End of primary loop */
} /* End of sl_collect() */

/***************************************************************************
 * sl_collect_nb:
 *
 * A wrapper for sl_collect_nb_size() to operate with up to the maximum
 * miniSEED record size.
 ***************************************************************************/
int
sl_collect_nb (SLCD *slconn, SLpacket **slpack)
{
  return sl_collect_nb_size (slconn, slpack, SLRECSIZEMAX);
}

/***************************************************************************
 * sl_collect_nb_size:
 *
 * Routine to manage a connection to a SeedLink server based on the values
 * given in the slconn struct and collect data.
 *
 * Designed to run in a tight loop at the heart of a client program, this
 * function is a non-blocking version sl_collect().  SLNOPACKET will be
 * returned when no data is available.
 *
 * Returns SLPACKET when something is received and sets the slpack
 * pointer to the received packet.  Returns SLNOPACKET when no packets
 * have been received, slpack is set to NULL.  When the connection was
 * closed by the server or the termination sequence completed
 * SLTERMINATE is returned and the slpack pointer is set to NULL.
 *
 * 6 Apr 2012 - Jacob Crummey:
 * Added sl_collect_nb_size() to allow for selecting SeedLink record
 * packet size.  Possible values for slrecsize are 128, 256, 512.
 * There is no error checking, so the value of slrecsize must be
 * checked before passing it to sl_collect_nb_size().
 *
 * 'slrecsize' has been re-purposed to be 'maxrecsize' and used to allow
 * the caller to specify a maximum record size to return.
 ***************************************************************************/
int
sl_collect_nb_size (SLCD *slconn, SLpacket **slpack, int maxrecsize)
{
  int bytesread;
  double current_time;
  char retpacket;
  int bufferlen;
  uint8_t formatversion = 0;


  *slpack = NULL;

  /* Check if the info was set */
  if (slconn->info != NULL)
  {
    slconn->stat->query_mode = InfoQuery;
  }

  /* If the connection is not up check the SLCD and reset the timing variables */
  if (slconn->link == -1)
  {
    if (sl_checkslcd (slconn))
    {
      sl_log_r (slconn, 2, 0, "problems with the connection description\n");
      return SLTERMINATE;
    }

    slconn->stat->netto_trig     = -1; /* Init net timeout trigger to reset state */
    slconn->stat->keepalive_trig = -1; /* Init keepalive trigger to reset state */
  }

  /* Start the main sequence  */
  if (!slconn->terminate)
  {
    if (slconn->link == -1)
    {
      slconn->stat->sl_state = SL_DOWN;
    }

    /* Check for network timeout */
    if (slconn->stat->sl_state == SL_DATA &&
        slconn->netto && slconn->stat->netto_trig > 0)
    {
      sl_log_r (slconn, 1, 0, "network timeout (%ds), reconnecting in %ds\n",
                slconn->netto, slconn->netdly);
      slconn->link              = sl_disconnect (slconn);
      slconn->stat->sl_state    = SL_DOWN;
      slconn->stat->netto_trig  = -1;
      slconn->stat->netdly_trig = -1;
    }

    /* Check if a keepalive packet needs to be sent */
    if (slconn->stat->sl_state == SL_DATA && !slconn->stat->expect_info &&
        slconn->keepalive && slconn->stat->keepalive_trig > 0)
    {
      sl_log_r (slconn, 1, 2, "sending keepalive request\n");

      if (sl_send_info (slconn, "ID", 3) != -1)
      {
        slconn->stat->query_mode     = KeepAliveQuery;
        slconn->stat->expect_info    = 1;
        slconn->stat->keepalive_trig = -1;
      }
    }

    /* Check if an in-stream INFO request needs to be sent */
    if (slconn->stat->sl_state == SL_DATA &&
        !slconn->stat->expect_info && slconn->info)
    {
      if (sl_send_info (slconn, slconn->info, 1) != -1)
      {
        slconn->stat->query_mode  = InfoQuery;
        slconn->stat->expect_info = 1;
      }
      else
      {
        slconn->stat->query_mode = NoQuery;
      }

      slconn->info = NULL;
    }

    /* Throttle the loop while delaying */
    if (slconn->stat->sl_state == SL_DOWN && slconn->stat->netdly_trig > 0)
    {
      slp_usleep (500000);
    }

    /* Connect to remote SeedLink */
    if (slconn->stat->sl_state == SL_DOWN && slconn->stat->netdly_trig == 0)
    {
      if (sl_connect (slconn, 1) != -1)
      {
        slconn->stat->sl_state = SL_UP;
      }
      slconn->stat->netto_trig     = -1;
      slconn->stat->netdly_trig    = -1;
      slconn->stat->keepalive_trig = -1;
    }

    /* Negotiate/configure the connection */
    if (slconn->stat->sl_state == SL_UP)
    {
      int slconfret = 0;

      /* Only send query if a query is set and no streams are defined,
       * if streams are defined we'll send the query after configuration.
       */
      if (slconn->info && slconn->streams == NULL)
      {
        if (sl_send_info (slconn, slconn->info, 1) != -1)
        {
          slconn->stat->query_mode  = InfoQuery;
          slconn->stat->expect_info = 1;
        }
        else
        {
          slconn->stat->query_mode  = NoQuery;
          slconn->stat->expect_info = 0;
        }

        slconn->info = NULL;
      }
      else
      {
        slconfret                 = sl_configlink (slconn);
        slconn->stat->expect_info = 0;
      }

      if (slconfret != -1)
      {
        slconn->stat->recptr   = 0; /* initialize the data buffer pointers */
        slconn->stat->sendptr  = 0;
        slconn->stat->sl_state = SL_DATA;
      }
      else
      {
        sl_log_r (slconn, 2, 0, "negotiation with remote SeedLink failed\n");
        slconn->link              = sl_disconnect (slconn);
        slconn->stat->netdly_trig = -1;
      }
    }
  }
  else /* We are terminating */
  {
    if (slconn->link != -1)
    {
      slconn->link = sl_disconnect (slconn);
    }

    slconn->stat->sl_state = SL_DATA;
  }

  /* Process data in buffer */
  while (slconn->stat->recptr - slconn->stat->sendptr >= SLHEADSIZE + SLRECSIZEMIN)
  {
    bufferlen = slconn->stat->recptr - slconn->stat->sendptr;
    retpacket = 1;

    slconn->stat->slpack.slhead = &slconn->stat->databuf[slconn->stat->sendptr];
    slconn->stat->slpack.msrecord = &slconn->stat->databuf[slconn->stat->sendptr + SLHEADSIZE];
    slconn->stat->slpack.reclen = detect (slconn->stat->slpack.msrecord, bufferlen, &formatversion);

    /* Return error if no miniSEED could be detected */
    if (slconn->stat->slpack.reclen < 0)
    {
      sl_log_r (slconn, 2, 0, "%s(): non-miniSEED packet received!?! Terminating.\n", __func__);
      return SLTERMINATE;
    }

    /* Stop processing if the buffer contains miniSEED but not enough data */
    if (slconn->stat->slpack.reclen == 0 ||
        (slconn->stat->slpack.reclen > 0 && slconn->stat->slpack.reclen + SLHEADSIZE  > bufferlen))
    {
      break;
    }

    /* Process an INFO packet */
    if (!memcmp (slconn->stat->slpack.slhead, INFOSIGNATURE, 6))
    {
      char terminator;

      terminator = (slconn->stat->slpack.slhead[SLHEADSIZE - 1] != '*');

      if (!slconn->stat->expect_info)
      {
        sl_log_r (slconn, 2, 0, "unexpected INFO packet received, skipping\n");
      }
      else
      {
        if (terminator)
        {
          slconn->stat->expect_info = 0;
        }

        /* Keep alive packets are not returned */
        if (slconn->stat->query_mode == KeepAliveQuery)
        {
          retpacket = 0;

          if (!terminator)
          {
            sl_log_r (slconn, 2, 0, "non-terminated keep-alive packet received!?!\n");
          }
          else
          {
            sl_log_r (slconn, 1, 2, "keepalive packet received\n");
          }
        }
      }

      if (slconn->stat->query_mode != NoQuery)
      {
        slconn->stat->query_mode = NoQuery;
      }
    }
    else /* Update the stream chain entry if not an INFO packet */
    {
      if ((update_stream (slconn, &slconn->stat->slpack)) == -1)
      {
        /* If updating didn't work the packet is broken */
        retpacket = 0;
      }
    }

    /* Increment the send pointer */
    slconn->stat->sendptr += (SLHEADSIZE + slconn->stat->slpack.reclen);

    /* Return packet */
    if (retpacket)
    {
      *slpack = &slconn->stat->slpack;
      return SLPACKET;
    }
  }

  /* A trap door for terminating, all complete data packets from the buffer
     have been sent to the caller */
  if (slconn->terminate)
  {
    return SLTERMINATE;
  }

  /* After processing the packet buffer shift the data */
  if (slconn->stat->sendptr)
  {
    memmove (slconn->stat->databuf,
             &slconn->stat->databuf[slconn->stat->sendptr],
             slconn->stat->recptr - slconn->stat->sendptr);

    slconn->stat->recptr -= slconn->stat->sendptr;
    slconn->stat->sendptr = 0;
  }

  /* Catch cases where the data stream stopped */
  if ((slconn->stat->recptr - slconn->stat->sendptr) == 7 &&
      !strncmp (&slconn->stat->databuf[slconn->stat->sendptr], "ERROR\r\n", 7))
  {
    sl_log_r (slconn, 2, 0, "SeedLink server reported an error with the last command\n");
    slconn->link = sl_disconnect (slconn);
    return SLTERMINATE;
  }

  if ((slconn->stat->recptr - slconn->stat->sendptr) == 3 &&
      !strncmp (&slconn->stat->databuf[slconn->stat->sendptr], "END", 3))
  {
    sl_log_r (slconn, 1, 1, "End of buffer or selected time window\n");
    slconn->link = sl_disconnect (slconn);
    return SLTERMINATE;
  }

  /* Process data in our buffer and then read incoming data */
  if (slconn->stat->sl_state == SL_DATA)
  {
    /* Check for more available data from the socket */
    bytesread = 0;

    bytesread = sl_recvdata (slconn, (void *)&slconn->stat->databuf[slconn->stat->recptr],
                             BUFSIZE - slconn->stat->recptr, slconn->sladdr);

    if (bytesread < 0 && !slconn->terminate) /* read() failed */
    {
      slconn->link              = sl_disconnect (slconn);
      slconn->stat->netdly_trig = -1;
    }
    else if (bytesread > 0) /* Data is here, process it */
    {
      slconn->stat->recptr += bytesread;

      /* Reset the timeout and keepalive timers */
      slconn->stat->netto_trig     = -1;
      slconn->stat->keepalive_trig = -1;
    }
  }

  /* Update timing variables */
  current_time = sl_dtime ();

  /* Network timeout timing logic */
  if (slconn->netto)
  {
    if (slconn->stat->netto_trig == -1) /* reset timer */
    {
      slconn->stat->netto_time = current_time;
      slconn->stat->netto_trig = 0;
    }
    else if (slconn->stat->netto_trig == 0 &&
             (current_time - slconn->stat->netto_time) > slconn->netto)
    {
      slconn->stat->netto_trig = 1;
    }
  }

  /* Keepalive/heartbeat interval timing logic */
  if (slconn->keepalive)
  {
    if (slconn->stat->keepalive_trig == -1) /* reset timer */
    {
      slconn->stat->keepalive_time = current_time;
      slconn->stat->keepalive_trig = 0;
    }
    else if (slconn->stat->keepalive_trig == 0 &&
             (current_time - slconn->stat->keepalive_time) > slconn->keepalive)
    {
      slconn->stat->keepalive_trig = 1;
    }
  }

  /* Network delay timing logic */
  if (slconn->netdly)
  {
    if (slconn->stat->netdly_trig == -1) /* reset timer */
    {
      slconn->stat->netdly_time = current_time;
      slconn->stat->netdly_trig = 1;
    }
    else if (slconn->stat->netdly_trig == 1 &&
             (current_time - slconn->stat->netdly_time) > slconn->netdly)
    {
      slconn->stat->netdly_trig = 0;
    }
  }

  /* Non-blocking and no data was returned */
  return SLNOPACKET;

} /* End of sl_collect_nb() */

/***************************************************************************
 * update_stream:
 *
 * Update the appropriate stream chain entries given a miniSEED
 * record.
 *
 * Returns 0 if successfully updated and -1 if not found or error.
 ***************************************************************************/
static int
update_stream (SLCD *slconn, const SLpacket *slpack)
{
  SLstream *curstream;
  struct sl_fsdh_s fsdh;
  int seqnum;
  int swapflag = 0;
  int updates  = 0;
  char net[11];
  char sta[11];

  if ((seqnum = sl_sequence (slpack)) == -1)
  {
    sl_log_r (slconn, 2, 0, "%s(): could not determine sequence number\n", __func__);
    return -1;
  }

  /* Copy fixed header */
  memcpy (&fsdh, slpack->msrecord, sizeof (struct sl_fsdh_s));

  /* Check to see if byte swapping is needed (bogus year makes good test) */
  if ((fsdh.start_time.year < 1900) || (fsdh.start_time.year > 2050))
    swapflag = 1;

  /* Change byte order? */
  if (swapflag)
  {
    sl_gswap2 (&fsdh.start_time.year);
    sl_gswap2 (&fsdh.start_time.day);
  }

  curstream = slconn->streams;

  /* Generate some "clean" net and sta strings */
  if (curstream != NULL)
  {
    sl_strncpclean (net, fsdh.network, 2);
    sl_strncpclean (sta, fsdh.station, 5);
  }

  /* For uni-station mode */
  if (curstream != NULL)
  {
    if (strcmp (curstream->net, UNINETWORK) == 0 &&
        strcmp (curstream->sta, UNISTATION) == 0)
    {
      int month = 0;
      int mday  = 0;

      sl_doy2md (fsdh.start_time.year,
                 fsdh.start_time.day,
                 &month, &mday);

      curstream->seqnum = seqnum;

      snprintf (curstream->timestamp, sizeof(curstream->timestamp),
                "%04d,%02d,%02d,%02d,%02d,%02d",
                fsdh.start_time.year,
                month,
                mday,
                fsdh.start_time.hour,
                fsdh.start_time.min,
                fsdh.start_time.sec);

      return 0;
    }
  }

  /* For multi-station mode, search the stream chain and update all matching entries */
  while (curstream != NULL)
  {
    /* Use glob matching to match wildcarded network and station codes */
    if (sl_globmatch (net, curstream->net) &&
        sl_globmatch (sta, curstream->sta))
    {
      int month = 0;
      int mday  = 0;

      sl_doy2md (fsdh.start_time.year,
                 fsdh.start_time.day,
                 &month, &mday);

      curstream->seqnum = seqnum;

      snprintf (curstream->timestamp, sizeof(curstream->timestamp),
                "%04d,%02d,%02d,%02d,%02d,%02d",
                fsdh.start_time.year,
                month,
                mday,
                fsdh.start_time.hour,
                fsdh.start_time.min,
                fsdh.start_time.sec);

      updates++;
    }

    curstream = curstream->next;
  }

  /* If no updates then no match was found */
  if (updates == 0)
    sl_log_r (slconn, 2, 0, "unexpected data received: %.2s %.6s\n", net, sta);

  return (updates == 0) ? -1 : 0;
} /* End of update_stream() */

/***************************************************************************
 * sl_newslcd:
 *
 * Allocate, initialze and return a pointer to a new SLCD struct.
 *
 * Returns allocated SLCD struct on success, NULL on error.
 ***************************************************************************/
SLCD *
sl_newslcd (void)
{
  SLCD *slconn;

  slconn = (SLCD *)malloc (sizeof (SLCD));

  if (slconn == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return NULL;
  }

  /* Set defaults */
  slconn->streams    = NULL;
  slconn->sladdr     = NULL;
  slconn->begin_time = NULL;
  slconn->end_time   = NULL;

  slconn->resume       = 1;
  slconn->multistation = 0;
  slconn->dialup       = 0;
  slconn->batchmode    = 0;
  slconn->lastpkttime  = 1;
  slconn->terminate    = 0;

  slconn->keepalive = 0;
  slconn->iotimeout = 60;
  slconn->netto     = 600;
  slconn->netdly    = 30;

  slconn->link         = -1;
  slconn->info         = NULL;
  slconn->protocol_ver = 0.0;

  /* Allocate the associated persistent state struct */
  slconn->stat = (SLstat *)malloc (sizeof (SLstat));

  if (slconn->stat == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    free (slconn);
    return NULL;
  }

  slconn->stat->recptr      = 0;
  slconn->stat->sendptr     = 0;
  slconn->stat->slpack.slhead = NULL;
  slconn->stat->slpack.msrecord = NULL;
  slconn->stat->expect_info = 0;

  slconn->stat->netto_trig     = -1;
  slconn->stat->netdly_trig    = 0;
  slconn->stat->keepalive_trig = -1;

  slconn->stat->netto_time     = 0.0;
  slconn->stat->netdly_time    = 0.0;
  slconn->stat->keepalive_time = 0.0;

  slconn->stat->sl_state   = SL_DOWN;
  slconn->stat->query_mode = NoQuery;

  slconn->log = NULL;

  return slconn;
} /* End of sl_newslconn() */

/***************************************************************************
 * sl_freeslcd:
 *
 * Free all memory associated with a SLCD struct including the
 * associated stream chain and persistent connection state.
 *
 ***************************************************************************/
void
sl_freeslcd (SLCD *slconn)
{
  SLstream *curstream;
  SLstream *nextstream;

  curstream = slconn->streams;

  /* Traverse the stream chain and free memory */
  while (curstream != NULL)
  {
    nextstream = curstream->next;

    if (curstream->selectors != NULL)
      free (curstream->selectors);
    free (curstream);

    curstream = nextstream;
  }

  if (slconn->sladdr != NULL)
    free (slconn->sladdr);

  if (slconn->begin_time != NULL)
    free (slconn->begin_time);

  if (slconn->end_time != NULL)
    free (slconn->end_time);

  if (slconn->stat != NULL)
    free (slconn->stat);

  if (slconn->log != NULL)
    free (slconn->log);

  free (slconn);
} /* End of sl_freeslcd() */

/***************************************************************************
 * sl_addstream:
 *
 * Add a new stream entry to the stream chain for the given SLCD
 * struct.  No checking is done for duplicate streams.
 *
 *  - selectors should be 0 if there are none to use
 *  - seqnum should be -1 to start at the next data
 *  - timestamp should be 0 if it should not be used
 *
 * Returns 0 if successfully added or -1 on error.
 ***************************************************************************/
int
sl_addstream (SLCD *slconn, const char *net, const char *sta,
              const char *selectors, int seqnum,
              const char *timestamp)
{
  SLstream *curstream;
  SLstream *newstream;
  SLstream *laststream = NULL;

  curstream = slconn->streams;

  /* Sanity, check for a uni-station mode entry */
  if (curstream)
  {
    if (strcmp (curstream->net, UNINETWORK) == 0 &&
        strcmp (curstream->sta, UNISTATION) == 0)
    {
      sl_log_r (slconn, 2, 0, "%s(): uni-station mode already configured!\n", __func__);
      return -1;
    }
  }

  /* Search the stream chain */
  while (curstream != NULL)
  {
    laststream = curstream;
    curstream  = curstream->next;
  }

  newstream = (SLstream *)malloc (sizeof (SLstream));

  if (newstream == NULL)
  {
    sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  newstream->net = strdup (net);
  newstream->sta = strdup (sta);

  if (selectors == 0 || selectors == NULL)
    newstream->selectors = 0;
  else
    newstream->selectors = strdup (selectors);

  newstream->seqnum = seqnum;

  if (timestamp == 0 || timestamp == NULL)
    newstream->timestamp[0] = '\0';
  else
    strncpy (newstream->timestamp, timestamp, 20);

  newstream->next = NULL;

  if (slconn->streams == NULL)
  {
    slconn->streams = newstream;
  }
  else if (laststream)
  {
    laststream->next = newstream;
  }

  slconn->multistation = 1;

  return 0;
} /* End of sl_addstream() */

/***************************************************************************
 * sl_setuniparams:
 *
 * Set the parameters for a uni-station mode connection for the
 * given SLCD struct.  If the stream entry already exists, overwrite
 * the previous settings.
 * Also sets the multistation flag to 0 (false).
 *
 *  - selectors should be 0 if there are none to use
 *  - seqnum should be -1 to start at the next data
 *  - timestamp should be 0 if it should not be used
 *
 * Returns 0 if successfully added or -1 on error.
 ***************************************************************************/
int
sl_setuniparams (SLCD *slconn, const char *selectors,
                 int seqnum, const char *timestamp)
{
  SLstream *newstream;

  newstream = slconn->streams;

  if (newstream == NULL)
  {
    newstream = (SLstream *)malloc (sizeof (SLstream));

    if (newstream == NULL)
    {
      sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
      return -1;
    }
  }
  else if (strcmp (newstream->net, UNINETWORK) != 0 ||
           strcmp (newstream->sta, UNISTATION) != 0)
  {
    sl_log_r (slconn, 2, 0, "%s(): multi-station mode already configured!\n", __func__);
    return -1;
  }

  newstream->net = UNINETWORK;
  newstream->sta = UNISTATION;

  if (selectors == 0 || selectors == NULL)
    newstream->selectors = 0;
  else
    newstream->selectors = strdup (selectors);

  newstream->seqnum = seqnum;

  if (timestamp == 0 || timestamp == NULL)
    newstream->timestamp[0] = '\0';
  else
    strncpy (newstream->timestamp, timestamp, 20);

  newstream->next = NULL;

  slconn->streams = newstream;

  slconn->multistation = 0;

  return 0;
} /* End of sl_setuniparams() */

/***************************************************************************
 * sl_request_info:
 *
 * Add an INFO request to the SeedLink Connection Description.
 *
 * Returns 0 if successful and -1 if error.
 ***************************************************************************/
int
sl_request_info (SLCD *slconn, const char *infostr)
{
  if (slconn->info != NULL)
  {
    sl_log_r (slconn, 2, 0, "Cannot make '%.15s' INFO request, one is already pending\n",
              infostr);
    return -1;
  }
  else
  {
    slconn->info = infostr;
    return 0;
  }
} /* End of sl_request_info() */

/***************************************************************************
 * sl_sequence:
 *
 * Check for 'SL' signature and sequence number from buffer containing
 * a SeedLink packet.
 *
 * Returns the packet sequence number of the SeedLink packet on success,
 *   0 for INFO packets or -1 on error.
 ***************************************************************************/
int
sl_sequence (const SLpacket *slpack)
{
  int seqnum;
  char seqstr[7], *sqtail;

  if (memcmp (slpack->slhead, SIGNATURE, 2))
    return -1;

  if (!memcmp (slpack->slhead, INFOSIGNATURE, 6))
    return 0;

  memcpy (seqstr, slpack->slhead + 2, 6);
  seqstr[6] = '\0';
  seqnum    = strtoul (seqstr, &sqtail, 16);

  if (seqnum & ~0xffffff || *sqtail)
    return -1;

  return seqnum;
} /* End of sl_sequence() */

/***************************************************************************
 * sl_packettype:
 *
 * Check the type of packet.  First check for an INFO packet then check for
 * the first 'important' blockette found in the data record.  If none of
 * the known marker blockettes are found then it is a regular data record.
 *
 * Returns the packet type; defined in libslink.h.
 ***************************************************************************/
int
sl_packettype (const SLpacket *slpack)
{
  int b2000 = 0;
  struct sl_fsdh_s *fsdh;

  uint16_t num_samples;
  int16_t samprate_fact;
  int16_t begin_blockette;
  uint16_t blkt_type;
  uint16_t next_blkt;

  const struct sl_blkt_head_s *p;
  fsdh = (struct sl_fsdh_s *)slpack->msrecord;

  /* Check for an INFO packet */
  if (!memcmp (slpack->slhead, INFOSIGNATURE, 6))
  {
    /* Check if it is terminated */
    if (slpack->slhead[SLHEADSIZE - 1] != '*')
      return SLINFT;
    else
      return SLINF;
  }

  num_samples     = (uint16_t)ntohs (fsdh->num_samples);
  samprate_fact   = (int16_t)ntohs (fsdh->samprate_fact);
  begin_blockette = (int16_t)ntohs (fsdh->begin_blockette);

  p = (const struct sl_blkt_head_s *)((const char *)fsdh +
                                      begin_blockette);

  blkt_type = (uint16_t)ntohs (p->blkt_type);
  next_blkt = (uint16_t)ntohs (p->next_blkt);

  do
  {
    if (((const char *)p) - ((const char *)fsdh) >
        MAX_HEADER_SIZE)
    {
      return SLNUM;
    }

    if (blkt_type >= 200 && blkt_type <= 299)
      return SLDET;
    if (blkt_type >= 300 && blkt_type <= 399)
      return SLCAL;
    if (blkt_type >= 500 && blkt_type <= 599)
      return SLTIM;
    if (blkt_type == 2000)
      b2000 = 1;

    p = (const struct sl_blkt_head_s *)((const char *)fsdh +
                                        next_blkt);

    blkt_type = (uint16_t)ntohs (p->blkt_type);
    next_blkt = (uint16_t)ntohs (p->next_blkt);
  } while ((const struct sl_fsdh_s *)p != fsdh);

  if (samprate_fact == 0)
  {
    if (num_samples != 0)
      return SLMSG;
    if (b2000)
      return SLBLK;
  }

  return SLDATA;
} /* End of sl_packettype() */

/***************************************************************************
 * sl_terminate:
 *
 * Set the terminate flag in the SLCD.
 ***************************************************************************/
void
sl_terminate (SLCD *slconn)
{
  sl_log_r (slconn, 1, 1, "Terminating connection\n");

  slconn->terminate = 1;
} /* End of sl_terminate() */

/***************************************************************/ /**
 * @brief Detect miniSEED record in buffer
 *
 * Determine if the buffer contains a miniSEED data record by
 * verifying known signatures (fields with known limited values).
 *
 * If miniSEED 2.x is detected, search the record up to recbuflen
 * bytes for a 1000 blockette. If no blockette 1000 is found, search
 * at 64-byte offsets for the fixed section of the next header,
 * thereby implying the record length.
 *
 * @param[in] record Buffer to test for record
 * @param[in] recbuflen Length of buffer
 * @param[out] formatversion Major version of format detected, 0 if unknown
 *
 * @retval -1 Data record not detected or error
 * @retval 0 Data record detected but could not determine length
 * @retval >0 Size of the record in bytes
 *********************************************************************/
static int
detect (const char *record, uint64_t recbuflen, uint8_t *formatversion)
{
  uint8_t swapflag = 0; /* Byte swapping flag */
  uint8_t foundlen = 0; /* Found record length */
  int32_t reclen = -1; /* Size of record in bytes */

  uint16_t blkt_offset; /* Byte offset for next blockette */
  uint16_t blkt_type;
  uint16_t next_blkt;
  const char *nextfsdh;

  if (!record || !formatversion)
    return -1;

  /* Buffer must be at least SLRECSIZEMIN */
  if (recbuflen < SLRECSIZEMIN)
    return -1;

  /* Check for valid header, set format version */
  *formatversion = 0;
  if (MS3_ISVALIDHEADER (record))
  {
    *formatversion = 3;

    reclen = MS3FSDH_LENGTH                   /* Length of fixed portion of header */
             + *pMS3FSDH_SIDLENGTH (record)   /* Length of source identifier */
             + *pMS3FSDH_EXTRALENGTH (record) /* Length of extra headers */
             + *pMS3FSDH_DATALENGTH (record); /* Length of data payload */

    foundlen = 1;
  }
  else if (MS2_ISVALIDHEADER (record))
  {
    *formatversion = 2;

    /* Check to see if byte swapping is needed by checking for sane year and day */
    if (!MS_ISVALIDYEARDAY (*pMS2FSDH_YEAR(record), *pMS2FSDH_DAY(record)))
      swapflag = 1;

    blkt_offset = HO2u(*pMS2FSDH_BLOCKETTEOFFSET (record), swapflag);

    /* Loop through blockettes as long as number is non-zero and viable */
    while (blkt_offset != 0 &&
           blkt_offset > 47 &&
           blkt_offset <= recbuflen)
    {
      memcpy (&blkt_type, record + blkt_offset, 2);
      memcpy (&next_blkt, record + blkt_offset + 2, 2);

      if (swapflag)
      {
        sl_gswap2 (&blkt_type);
        sl_gswap2 (&next_blkt);
      }

      /* Found a 1000 blockette, not truncated */
      if (blkt_type == 1000 &&
          (int)(blkt_offset + 8) <= recbuflen)
      {
        foundlen = 1;

        /* Field 3 of B1000 is a uint8_t value describing the record
         * length as 2^(value).  Calculate 2-raised with a shift. */
        reclen = (unsigned int)1 << *pMS2B1000_RECLEN(record+blkt_offset);

        break;
      }

      /* Safety check for invalid offset */
      if (next_blkt != 0 && (next_blkt < 4 || (next_blkt - 4) <= blkt_offset))
      {
        sl_log (2, 0, "Invalid blockette offset (%d) less than or equal to current offset (%d)\n",
                next_blkt, blkt_offset);
        return -1;
      }

      blkt_offset = next_blkt;
    }

    /* If record length was not determined by a 1000 blockette scan the buffer
     * and search for the next record */
    if (reclen == -1)
    {
      nextfsdh = record + 64;

      /* Check for record header or blank/noise record at MINRECLEN byte offsets */
      while (((nextfsdh - record) + 48) < recbuflen)
      {
        if (MS2_ISVALIDHEADER (nextfsdh))
        {
          foundlen = 1;
          reclen = nextfsdh - record;
          break;
        }

        nextfsdh += 64;
      }
    }
  } /* End of miniSEED 2.x detection */

  if (!foundlen)
    return -1;
  else
    return reclen;
} /* End of detect() */
