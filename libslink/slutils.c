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
 * Copyright (C) 2025:
 * @author Chad Trabant, EarthScope Data Services
 ***************************************************************************/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globmatch.h"
#include "libslink.h"
#include "mseedformat.h"

/* Function(s) only used in this source file */
static int receive_header (SLCD *slconn, uint8_t *buffer, uint32_t bytesavailable);
static int64_t receive_payload (SLCD *slconn, char *plbuffer, uint32_t plbuffersize,
                                uint8_t *buffer, uint32_t bytesavailable);
static int update_stream (SLCD *slconn, const char *payload);
static int64_t detect (const char *record, uint64_t recbuflen, char *payloadformat);
static const char *internal_auth_value_data (const char *server, void *auth_data);
static void free_internal_auth_data (SLCD *slconn);

/* Initialize the global termination handler */
SLCD *global_termination_SLCD = NULL;

/** ************************************************************************
 * @brief Manage a connection to a SeedLink server and collect packets
 *
 * Designed to run in a loop of a client program, this function manages
 * the connection to the server and returns received packets.  This
 * routine will send keepalives if configured for the connection and
 * can operate in blocking or non-blocking mode.
 *
 * This function will automatically reconnect on connection errors,
 * and other recoverable failures.  Fatal, non-recoverable errors
 * include: invalid arguments, authentication failures, the end of the
 * stream in dial-up mode, internal errors, protocol values that
 * cannot be represented, such as an oversized station ID, and
 * negotiation failures caused by the caller's own configuration
 * (an unparsable or oversized time string or selector) rather than
 * the server, since retrying an unchanged request cannot succeed.
 *
 * The returned \a packetinfo contains the details including: sequence
 * number, payload length, payload type, and how much of the payload
 * has been returned so far.
 *
 * If the connection is set to non-blocking mode using sl_set_blockingmode(),
 * the function will return quickly even if no data are available.
 * If the connection is set to blocking mode, the function will only return
 * when data are available or a non-recoverable error occurs.
 *
 * If \a SLTOOLARGE is returned, the \a plbuffer is not large enough to
 * hold the payload.  The payload length is available at
 * \a packetinfo.payloadlength and the caller may choose to reallocate
 * the buffer to accommodate the payload.  Note that buffer may contain
 * partial payload data and should be preserved if reallocated,
 * specifically the first \a packetinfo.payloadcollected bytes.
 *
 * A clean shutdown of the connection by the caller is achieved by
 * calling sl_terminate(), or by setting up termination handlers with
 * sl_set_termination_handler().  Clean shutdown will cause the function
 * to continue returning packets until the internal buffer is empty and then
 * return SLTERMINATE.
 *
 * @param[in]  slconn   SeedLink connection description
 * @param[out] packetinfo  Pointer to pointer to ::SLpacketinfo describing payload
 * @param[out] plbuffer  Destination buffer for packet payload
 * @param[in]  plbuffersize  Length of destination buffer
 *
 * @returns @ref collect-status
 * @retval SLPACKET Complete packet returned
 * @retval SLTERMINATE Fatal error (invalid parameters) or explicit termination
 * @retval SLNOPACKET  No packet available, call again
 * @retval SLTOOLARGE  Payload is larger than allowed maximum
 * @retval SLAUTHFAIL  Authentication failed
 ***************************************************************************/
int
sl_collect (SLCD *slconn, const SLpacketinfo **packetinfo, char *plbuffer, uint32_t plbuffersize)
{
  int64_t bytesread;
  int64_t current_time;
  uint32_t bytesconsumed;
  uint32_t bytesavailable;
  int poll_state;
  int info_payload;
  int info_terminated;
  int was_keepalive;
  int payload_completed;
  int payload_pending;

  if (!slconn || !packetinfo || (plbuffersize > 0 && !plbuffer))
    return SLTERMINATE;

  for (;;) /* Reconnection loop */
  {
    while (slconn->terminate < 2)
    {
      current_time = sl_nstime ();

      if (slconn->link == -1 && slconn->recvdatalen == 0)
      {
        slconn->stat->conn_state = DOWN;
      }

      /* Throttle the loop while delaying */
      if (slconn->stat->conn_state == DOWN && slconn->stat->netdly_time &&
          slconn->stat->netdly_time > current_time)
      {
        sl_usleep (500000);
      }

      /* Connect to server if disconnected */
      if (slconn->stat->conn_state == DOWN && slconn->stat->netdly_time < current_time)
      {
        int connect_status = sl_connect (slconn, 1);

        if (connect_status == SLAUTHFAIL)
        {
          return SLAUTHFAIL;
        }

        if (connect_status > 0)
        {
          slconn->stat->conn_state = UP;
          slconn->stat->netto_time = 0;
          slconn->stat->netdly_time = 0;
          slconn->stat->keepalive_time = 0;
          slconn->stat->query_state = NoQuery;
        }
        else
        {
          /* A connection failure caused by the caller's own configuration
           * (e.g. a protocol forced with sl_set_protocol() that the server
           * does not support) reproduces identically on every retry; treat
           * it as fatal rather than reconnecting forever. */
          if (slconn->config_error)
          {
            sl_log_r (slconn, 2, 0, "[%s] %s(): connection failed due to invalid configuration\n",
                      slconn->sladdr, __func__);
            sl_disconnect (slconn);
            *packetinfo = NULL;
            return SLTERMINATE;
          }

          /* Connection failed, let outer reconnection logic handle delay */
          sl_log_r (slconn, 2, 0, "[%s] connection failed\n", slconn->sladdr);
          break;
        }
      }

      /* Negotiate/configure the connection */
      if (slconn->stat->conn_state == UP)
      {
        if (slconn->streams)
        {
          if (sl_configlink (slconn) == -1)
          {
            /* A negotiation failure caused by the caller's own configuration
             * (an unparsable or oversized time string or selector) reproduces
             * identically on every retry; treat it as fatal rather than
             * reconnecting forever. Server-driven rejections leave
             * config_error unset and remain retryable. */
            if (slconn->config_error)
            {
              sl_log_r (slconn, 2, 0,
                        "[%s] %s(): negotiation failed due to invalid configuration\n",
                        slconn->sladdr, __func__);
              sl_disconnect (slconn);
              *packetinfo = NULL;
              return SLTERMINATE;
            }

            sl_log_r (slconn, 2, 0, "[%s] %s(): negotiation with server failed\n", slconn->sladdr,
                      __func__);
            break;
          }
        }

        slconn->stat->conn_state = STREAMING;
      }

      /* Send INFO request if one not in progress */
      if (slconn->stat->conn_state == STREAMING && slconn->stat->query_state == NoQuery &&
          slconn->info)
      {
        if (sl_send_info (slconn, slconn->info, 1) != -1)
        {
          slconn->stat->query_state = InfoQuery;
        }
        else
        {
          sl_log_r (slconn, 2, 0, "[%s] %s(): error sending INFO request\n", slconn->sladdr,
                    __func__);
          slconn->stat->query_state = NoQuery;
        }

        free (slconn->info);
        slconn->info = NULL;
      }

      /* Read incoming data stream */
      if (slconn->stat->conn_state == STREAMING)
      {
        /* Receive data into internal buffer (skip if connection already closed
         * or the buffer is already full; a zero-length read is indistinguishable
         * from the peer closing the connection) */
        if (slconn->terminate == 0 && slconn->link != -1 &&
            slconn->recvdatalen < sizeof (slconn->recvbuffer))
        {
          bytesread =
              sl_recvdata (slconn, slconn->recvbuffer + slconn->recvdatalen,
                           sizeof (slconn->recvbuffer) - slconn->recvdatalen, slconn->sladdr);

          if (bytesread < 0)
          {
            /* Connection closed - close socket but continue processing buffer */
            sl_disconnect (slconn);
          }
          else if (bytesread > 0)
          {
            slconn->recvdatalen += bytesread;
          }
          else if (slconn->recvdatalen == 0) /* bytesread == 0 */
          {
            /* Wait up to 1/2 second when blocking, otherwise 1 millisecond */
            poll_state = sl_poll (slconn, 1, 0, (slconn->noblock) ? 1 : 500);

            if (poll_state < 0 && slconn->terminate == 0)
            {
              sl_log_r (slconn, 2, 0, "[%s] %s(): polling error: %s\n", slconn->sladdr, __func__,
                        sl_strerror ());
              break;
            }
          }
        }

        /* Process data in internal buffer */
        bytesconsumed = 0;
        payload_completed = 0;
        payload_pending = 0;

        /* Check for special cases of the server reporting end of streaming or errors
         * while awaiting a header (i.e. in between packets) */
        if (slconn->stat->stream_state == HEADER)
        {
          if (slconn->recvdatalen - bytesconsumed >= 3 &&
              memcmp (slconn->recvbuffer + bytesconsumed, "END", 3) == 0)
          {
            sl_log_r (slconn, 1, 1,
                      "[%s] End of selected time window or stream (FETCH/dial-up mode)\n",
                      slconn->sladdr);

            /* A completed request has nothing left to ask for again, so this
             * ends the connection outright rather than reconnecting, regardless
             * of dial-up mode. */
            sl_disconnect (slconn);
            *packetinfo = NULL;
            return SLTERMINATE;
          }

          if (slconn->recvdatalen - bytesconsumed >= 5 &&
              memcmp (slconn->recvbuffer + bytesconsumed, "ERROR", 5) == 0)
          {
            sl_log_r (slconn, 2, 0, "[%s] Server reported an error with the last command\n",
                      slconn->sladdr);

            bytesconsumed += 5;
            break;
          }
        }

        /* Read next header */
        if (slconn->stat->stream_state == HEADER)
        {
          bytesavailable = slconn->recvdatalen - bytesconsumed;

          if ((slconn->protocol & SLPROTO3X && bytesavailable >= SLHEADSIZE_V3) ||
              (slconn->protocol & SLPROTO40 && bytesavailable >= SLHEADSIZE_V4))
          {
            bytesread = receive_header (slconn, slconn->recvbuffer + bytesconsumed, bytesavailable);

            if (bytesread < 0)
            {
              sl_log_r (slconn, 2, 0, "[%s] %s(): error receiving header: %s\n", slconn->sladdr,
                        __func__, sl_strerror ());
              break;
            }
            else if (bytesread > 0)
            {
              /* Set state for station ID or payload collection */
              if (slconn->stat->packetinfo.stationidlength > 0)
              {
                slconn->stat->packetinfo.stationid[0] = '\0';
                slconn->stat->stream_state = STATIONID;
              }
              else
              {
                slconn->stat->packetinfo.payloadcollected = 0;
                slconn->stat->stream_state = PAYLOAD;
              }

              bytesconsumed += bytesread;
            }
          }
        } /* Done reading header */

        /* Read station ID */
        if (slconn->stat->stream_state == STATIONID &&
            slconn->stat->packetinfo.stationidlength > 0 &&
            (slconn->recvdatalen - bytesconsumed) >= slconn->stat->packetinfo.stationidlength)
        {
          if (slconn->stat->packetinfo.stationidlength >
              (sizeof (slconn->stat->packetinfo.stationid) - 1))
          {
            sl_log_r (slconn, 2, 0,
                      "[%s] %s(): received station ID is too large (%u) for buffer (%zu)\n",
                      slconn->sladdr, __func__, slconn->stat->packetinfo.stationidlength,
                      sizeof (slconn->stat->packetinfo.stationid) - 1);

            sl_disconnect (slconn);
            *packetinfo = NULL;
            return SLTERMINATE;
          }
          else
          {
            memcpy (slconn->stat->packetinfo.stationid, slconn->recvbuffer + bytesconsumed,
                    slconn->stat->packetinfo.stationidlength);

            slconn->stat->packetinfo.stationid[slconn->stat->packetinfo.stationidlength] = '\0';

            /* Set state for payload collection */
            slconn->stat->packetinfo.payloadcollected = 0;
            slconn->stat->stream_state = PAYLOAD;

            bytesconsumed += slconn->stat->packetinfo.stationidlength;
          }
        } /* Done reading station ID */

        /* Read payload */
        if (slconn->stat->stream_state == PAYLOAD)
        {
          bytesavailable = slconn->recvdatalen - bytesconsumed;

          /* If payload length is known, return SLTOOLARGE if buffer is not sufficient */
          if (slconn->stat->packetinfo.payloadlength > 0 &&
              slconn->stat->packetinfo.payloadlength > plbuffersize)
          {
            /* Shift any remaining data in the buffer to the start */
            if (bytesconsumed > 0 && bytesconsumed < slconn->recvdatalen)
            {
              memmove (slconn->recvbuffer, slconn->recvbuffer + bytesconsumed,
                       slconn->recvdatalen - bytesconsumed);
            }

            slconn->recvdatalen -= bytesconsumed;
            bytesconsumed = 0;

            *packetinfo = &slconn->stat->packetinfo;
            return SLTOOLARGE;
          }

          bytesread = receive_payload (slconn, plbuffer, plbuffersize,
                                       slconn->recvbuffer + bytesconsumed, bytesavailable);

          if (bytesread < 0)
          {
            sl_log_r (slconn, 2, 0, "[%s] %s(): error receiving payload: %s\n", slconn->sladdr,
                      __func__, sl_strerror ());
            break;
          }
          if (bytesread > 0)
          {
            slconn->stat->netto_time = 0;
            slconn->stat->keepalive_time = 0;

            bytesconsumed += bytesread;
          }
          /* A v3 payload of unknown length that could not yet be detected;
           * more data is needed, not a stuck stream */
          else
          {
            payload_pending = 1;
          }

          /* Payload is complete; the length is declared in the v4 header and detected for v3 */
          if ((slconn->protocol & SLPROTO40 || slconn->stat->packetinfo.payloadlength > 0) &&
              slconn->stat->packetinfo.payloadcollected == slconn->stat->packetinfo.payloadlength)
          {
            /* Shift any remaining data in the buffer to the start */
            if (bytesconsumed > 0 && bytesconsumed < slconn->recvdatalen)
            {
              memmove (slconn->recvbuffer, slconn->recvbuffer + bytesconsumed,
                       slconn->recvdatalen - bytesconsumed);
            }

            slconn->recvdatalen -= bytesconsumed;
            bytesconsumed = 0;
            payload_completed = 1;

            /* Set state for header collection if payload is complete */
            slconn->stat->stream_state = HEADER;

            /* INFO response payload, terminated if the last (only, for v4) packet */
            info_terminated = (slconn->stat->packetinfo.payloadformat == SLPAYLOAD_MSEED2INFOTERM ||
                               (slconn->stat->packetinfo.payloadformat == SLPAYLOAD_JSON &&
                                slconn->stat->packetinfo.payloadsubformat == SLPAYLOAD_JSON_INFO));
            info_payload =
                (info_terminated || slconn->stat->packetinfo.payloadformat == SLPAYLOAD_MSEED2INFO);

            was_keepalive = (info_payload && slconn->stat->query_state == KeepAliveQuery);

            /* A terminated INFO response closes out the pending query */
            if (info_payload && info_terminated)
            {
              if (was_keepalive)
                sl_log_r (slconn, 1, 2, "[%s] Keepalive message received\n", slconn->sladdr);

              slconn->stat->query_state = NoQuery;
            }

            /* Keepalive INFO responses are not returned to the caller */
            if (was_keepalive)
            {
              /* Multi-packet v3 keepalive responses are swallowed until the terminator */
            }
            /* All other payloads are returned to the caller, unless stream tracking
             * cannot be updated, e.g. an unparsable payload or an unexpected station;
             * update_stream() logs the specific reason. Such a packet is dropped and
             * streaming continues rather than terminating the connection. */
            else if (update_stream (slconn, plbuffer) == 0)
            {
              *packetinfo = &slconn->stat->packetinfo;
              return SLPACKET;
            }
          }
        } /* Done reading payload */

        /* If a viable amount of data exists but has not been consumed something is wrong with the
         * stream. A completed payload already shifted its bytes out and zeroed
         * bytesconsumed above, whether the packet was returned, swallowed as a
         * keepalive, or dropped for failing stream tracking; none of those is stuck.
         * A v3 payload awaiting more data to determine its length is also not stuck. */
        if (slconn->recvdatalen > SL_MIN_PAYLOAD && bytesconsumed == 0 && !payload_completed &&
            !payload_pending)
        {
          sl_log_r (slconn, 2, 0,
                    "[%s] %s(): cannot process received data (recvdatalen: %u, stream_state: %d)\n",
                    slconn->sladdr, __func__, slconn->recvdatalen, slconn->stat->stream_state);
          break;
        }

        /* Shift any remaining data in the buffer to the start */
        if (bytesconsumed > 0 && bytesconsumed < slconn->recvdatalen)
        {
          memmove (slconn->recvbuffer, slconn->recvbuffer + bytesconsumed,
                   slconn->recvdatalen - bytesconsumed);
        }

        slconn->recvdatalen -= bytesconsumed;

        /* Connection closed and buffer exhausted or can't progress - break to
         * reconnect. A payload completed this pass also zeroes bytesconsumed
         * (above) after shifting it out, so that alone is not "can't
         * progress" - any further complete packets still buffered must be
         * drained before reconnecting. */
        if (slconn->link == -1 &&
            (slconn->recvdatalen == 0 || (bytesconsumed == 0 && !payload_completed)))
        {
          sl_log_r (slconn, 2, 0, "[%s] %s(): connection closed\n", slconn->sladdr, __func__);
          break;
        }

        /* Set termination flag to level 2 if buffer has little/no viable data */
        if (slconn->terminate == 1 && slconn->recvdatalen <= SL_MIN_PAYLOAD)
        {
          slconn->terminate = 2;
        }
      } /* Done reading data in STREAMING state */

      /* Update timing variables */
      current_time = sl_nstime ();

      /* Check for network idle timeout */
      if (slconn->stat->conn_state == STREAMING && slconn->netto && slconn->stat->netto_time &&
          slconn->stat->netto_time < current_time)
      {
        sl_log_r (slconn, 1, 0, "[%s] network timeout, no data for %d seconds\n", slconn->sladdr,
                  slconn->netto);
        break;
      }

      /* Check if keepalive packet needs to be sent */
      if (slconn->stat->conn_state == STREAMING && slconn->stat->query_state == NoQuery &&
          slconn->keepalive && slconn->stat->keepalive_time &&
          slconn->stat->keepalive_time < current_time)
      {
        sl_log_r (slconn, 1, 2, "[%s] Sending keepalive message\n", slconn->sladdr);

        if (sl_send_info (slconn, "ID", 3) == -1)
        {
          sl_log_r (slconn, 2, 0, "[%s] %s(): error sending keepalive message: %s\n",
                    slconn->sladdr, __func__, sl_strerror ());
          break;
        }

        slconn->stat->query_state = KeepAliveQuery;
        slconn->stat->keepalive_time = 0;
      }

      /* Set network idle timeout if not already set */
      if (slconn->netto && slconn->stat->netto_time == 0)
      {
        slconn->stat->netto_time = current_time + SL_EPOCH2SLTIME (slconn->netto);
      }

      /* Set keepalive/heartbeat interval if not already set */
      if (slconn->keepalive && slconn->stat->keepalive_time == 0)
      {
        slconn->stat->keepalive_time = current_time + SL_EPOCH2SLTIME (slconn->keepalive);
      }

      /* Return if not waiting for data and no data in internal buffer */
      if (slconn->noblock && slconn->recvdatalen == 0)
      {
        *packetinfo = NULL;
        return SLNOPACKET;
      }

      /* Termination when not connected is immediate */
      if (slconn->terminate && slconn->stat->conn_state == DOWN)
      {
        break;
      }
    } /* End of streaming loop */

    /* Check for conditions that should not trigger reconnection:
     * - Explicit termination requested
     * - End of time window in dial-up mode (only if we were streaming) */
    if (slconn->terminate || (slconn->dialup && slconn->stat->conn_state == STREAMING))
    {
      break;
    }

    /* Prepare for reconnection */
    sl_log_r (slconn, 1, 1, "[%s] reconnecting in %d seconds\n", slconn->sladdr, slconn->netdly);
    sl_disconnect (slconn);
    slconn->stat->conn_state = DOWN;
    slconn->stat->stream_state = HEADER;
    slconn->recvdatalen = 0;
    slconn->stat->netto_time = 0;
    slconn->stat->netdly_time = sl_nstime () + SL_EPOCH2SLTIME (slconn->netdly);

  } /* End of reconnection loop */

  /* Terminating */
  sl_disconnect (slconn);

  *packetinfo = NULL;
  return SLTERMINATE;
} /* End of sl_collect() */

/***************************************************************************
 * receive_header:
 *
 * Receive packet header.
 *
 * Returns:
 * bytes : Size of header read
 * -1 :  on error
 ***************************************************************************/
static int
receive_header (SLCD *slconn, uint8_t *buffer, uint32_t bytesavailable)
{
  uint32_t bytesread = 0;
  char sequence[7] = {0};
  char *tail = NULL;

  if (!slconn)
    return -1;

  /* Zero the destination packet info structure */
  memset (&slconn->stat->packetinfo, 0, sizeof (SLpacketinfo));

  if (slconn->protocol & SLPROTO3X && bytesavailable >= SLHEADSIZE_V3)
  {
    /* Parse v3 INFO header */
    if (memcmp (buffer, INFOSIGNATURE, 6) == 0)
    {
      slconn->stat->packetinfo.seqnum = SL_UNSETSEQUENCE;
      slconn->stat->packetinfo.payloadlength = 0;
      slconn->stat->packetinfo.payloadformat =
          (buffer[SLHEADSIZE_V3 - 1] == '*') ? SLPAYLOAD_MSEED2INFO : SLPAYLOAD_MSEED2INFOTERM;
    }
    /* Parse v3 data header */
    else if (memcmp (buffer, SIGNATURE_V3, 2) == 0)
    {
      int idx;

      memcpy (sequence, buffer + 2, 6);

      /* The field is a fixed-width 6-digit hex value; reject anything else
       * outright rather than let strtoul() accept a leading sign and wrap
       * to a value colliding with the SL_UNSETSEQUENCE/SL_ALLDATASEQUENCE
       * sentinels. */
      for (idx = 0; idx < 6; idx++)
      {
        if (!isxdigit ((unsigned char)sequence[idx]))
        {
          sl_log_r (slconn, 2, 0, "[%s] %s() cannot parse sequence number from v3 header: %8.8s\n",
                    slconn->sladdr, __func__, buffer + 2);
          return -1;
        }
      }

      slconn->stat->packetinfo.seqnum = strtoul (sequence, &tail, 16);

      if (*tail)
      {
        sl_log_r (slconn, 2, 0, "[%s] %s() cannot parse sequence number from v3 header: %8.8s\n",
                  slconn->sladdr, __func__, buffer + 2);
        return -1;
      }

      slconn->stat->packetinfo.payloadlength = 0;
      slconn->stat->packetinfo.payloadformat = SLPAYLOAD_UNKNOWN;
    }
    else
    {
      sl_log_r (slconn, 2, 0, "[%s] %s(): unexpected V3 header signature found: %2.2s)\n",
                slconn->sladdr, __func__, buffer);
      return -1;
    }

    bytesread = SLHEADSIZE_V3;
  }
  else if (slconn->protocol & SLPROTO40 && bytesavailable >= SLHEADSIZE_V4)
  {
    /* Parse v4 header */
    if (memcmp (buffer, SIGNATURE_V4, 2) == 0)
    {
      slconn->stat->packetinfo.payloadformat = buffer[2];
      slconn->stat->packetinfo.payloadsubformat = buffer[3];
      memcpy (&slconn->stat->packetinfo.payloadlength, buffer + 4, 4);
      memcpy (&slconn->stat->packetinfo.seqnum, buffer + 8, 8);
      memcpy (&slconn->stat->packetinfo.stationidlength, buffer + 16, 1);

      if (!sl_littleendianhost ())
      {
        sl_gswap8 (&slconn->stat->packetinfo.seqnum);
        sl_gswap4 (&slconn->stat->packetinfo.payloadlength);
      }

      /* Reject a wire sequence number that collides with the reserved
       * SL_UNSETSEQUENCE/SL_ALLDATASEQUENCE sentinels; storing either would
       * silently change what the next reconnect requests. */
      if (slconn->stat->packetinfo.seqnum == SL_UNSETSEQUENCE ||
          slconn->stat->packetinfo.seqnum == SL_ALLDATASEQUENCE)
      {
        sl_log_r (slconn, 2, 0,
                  "[%s] %s(): sequence number in v4 header collides with a reserved value "
                  "(%" PRIu64 ")\n",
                  slconn->sladdr, __func__, slconn->stat->packetinfo.seqnum);
        return -1;
      }
    }
    else
    {
      sl_log_r (slconn, 2, 0, "[%s] %s(): unexpected V4 header signature found: %2.2s)\n",
                slconn->sladdr, __func__, buffer);
      return -1;
    }

    bytesread = SLHEADSIZE_V4;
  }
  else
  {
    sl_log_r (slconn, 2, 0, "[%s] %s(): unexpected header signature found (instead: %2.2s)\n",
              slconn->sladdr, __func__, buffer);
    return -1;
  }

  return bytesread;
} /* End of receive_header() */

/***************************************************************************
 * receive_payload:
 *
 * Copy payload data to supplied buffer.
 *
 * The supplied buffer must be large enough for payload detection,
 * defined as SL_MIN_PAYLOAD bytes.
 *
 * Returns
 * bytes : Number of bytes consumed on success
 * -1 :  on error
 ***************************************************************************/
int64_t
receive_payload (SLCD *slconn, char *plbuffer, uint32_t plbuffersize, uint8_t *buffer,
                 uint32_t bytesavailable)
{
  SLpacketinfo *packetinfo = NULL;
  uint32_t bytestoconsume = 0;
  int64_t detectedlength;
  char payloadformat = SLPAYLOAD_UNKNOWN;

  if (!slconn || !plbuffer)
    return -1;

  packetinfo = &slconn->stat->packetinfo;

  /* Payload length is unknown for v3 until detected.  Detect directly
   * against the internal receive buffer, before anything is copied to the
   * caller's buffer: a record with no blockette 1000 is only detectable by
   * locating the start of the following record's header, which requires
   * bytes beyond the end of this one still be available to detect against. */
  if (slconn->protocol & SLPROTO3X && packetinfo->payloadlength == 0)
  {
    /* Wait for more data if the minimum for detection is not available */
    if (bytesavailable < SL_MIN_PAYLOAD)
    {
      return 0;
    }

    detectedlength = detect ((const char *)buffer, bytesavailable, &payloadformat);

    /* Return error if no recognized payload detected */
    if (detectedlength < 0)
    {
      sl_log_r (slconn, 2, 0,
                "[%s] %s(): non-miniSEED packet received for v3 protocol! Terminating.\n",
                slconn->sladdr, __func__);
      return -1;
    }
    /* Length not yet determined; wait for more data unless the internal
     * receive buffer is already full, in which case it never will be */
    else if (detectedlength == 0)
    {
      if (slconn->recvdatalen >= sizeof (slconn->recvbuffer))
      {
        sl_log_r (slconn, 2, 0,
                  "[%s] %s(): cannot determine miniSEED v3 payload length within %zu bytes\n",
                  slconn->sladdr, __func__, sizeof (slconn->recvbuffer));
        return -1;
      }

      return 0;
    }

    if (packetinfo->payloadformat == SLPAYLOAD_UNKNOWN)
    {
      packetinfo->payloadformat = payloadformat;
    }

    /* Fits uint32_t: detect() already rejected any length that would not. */
    packetinfo->payloadlength = (uint32_t)detectedlength;
  }

  /* If remaining payload is smaller than available, consume remaining */
  if ((packetinfo->payloadlength - packetinfo->payloadcollected) < bytesavailable)
  {
    bytestoconsume = packetinfo->payloadlength - packetinfo->payloadcollected;
  }
  /* Otherwise, all available data is payload */
  else
  {
    bytestoconsume = bytesavailable;
  }

  /* Cap at remaining caller-buffer space; the caller reports SLTOOLARGE when
   * payloadlength exceeds plbuffersize. Guard collected >= size so the
   * uint32_t subtraction cannot underflow. */
  if (packetinfo->payloadcollected >= plbuffersize)
  {
    bytestoconsume = 0;
  }
  else if (bytestoconsume > plbuffersize - packetinfo->payloadcollected)
  {
    bytestoconsume = plbuffersize - packetinfo->payloadcollected;
  }

  /* Copy payload data from internal buffer to payload buffer */
  memcpy (plbuffer + packetinfo->payloadcollected, buffer, bytestoconsume);
  packetinfo->payloadcollected += bytestoconsume;

  return bytestoconsume;
} /* End of receive_payload() */

/***************************************************************************
 * update_stream:
 *
 * Update the appropriate stream list entries.  Length of the payload
 * must be at least enough to determine stream details.
 *
 * The slconn->stat->packetinfo.stationid value is also populated from
 * the payload if not already set.
 *
 * Returns 0 if successfully updated and -1 if not found or error.
 ***************************************************************************/
static int
update_stream (SLCD *slconn, const char *payload)
{
  SLpacketinfo *packetinfo = NULL;
  SLstream *curstream;
  int updates = 0;

  char timestamp[32] = {0};
  char sourceid[64] = {0};
  char *cp;
  size_t count;

  if (!slconn || !payload)
    return -1;

  packetinfo = &slconn->stat->packetinfo;

  /* No updates for info and error packets */
  if (packetinfo->payloadformat == SLPAYLOAD_MSEED2INFO ||
      packetinfo->payloadformat == SLPAYLOAD_MSEED2INFOTERM ||
      (packetinfo->payloadformat == SLPAYLOAD_JSON &&
       (packetinfo->payloadsubformat == SLPAYLOAD_JSON_INFO ||
        packetinfo->payloadsubformat == SLPAYLOAD_JSON_ERROR)))
  {
    return 0;
  }

  /* Extract start time stamp and source ID (if needed) from payload if miniSEED */
  if (packetinfo->payloadformat == SLPAYLOAD_MSEED2 ||
      packetinfo->payloadformat == SLPAYLOAD_MSEED3)
  {
    if (sl_payload_info (slconn->log, packetinfo, payload, packetinfo->payloadlength,
                         (packetinfo->stationidlength == 0) ? sourceid : NULL, sizeof (sourceid),
                         timestamp, sizeof (timestamp), NULL, NULL) == -1)
    {
      sl_log_r (slconn, 2, 0, "[%s] %s(): cannot extract payload info for miniSEED\n",
                slconn->sladdr, __func__);
      return -1;
    }

    /* Set station ID if it was not included in SeedLink header (e.g. v3 protocol) */
    if (packetinfo->stationidlength == 0)
    {
      /* Extract NET_STA from FDSN Source Identifier returned by sl_payload_info() */
      if (strlen (sourceid) >= 8 && strncmp (sourceid, "FDSN:", 5) == 0)
      {
        /* Copy from ':' to 2nd '_' from "FDSN:NET_STA_LOC_B_S_SS" */
        if ((cp = strchr (sourceid + 5, '_')))
        {
          if ((cp = strchr (cp + 1, '_')))
          {
            count = (cp - sourceid) - 5;

            if (count >= sizeof (packetinfo->stationid))
            {
              sl_log_r (slconn, 2, 0,
                        "[%s] %s(): extracted NET_STA ID from miniSEED is too large (%zu)\n",
                        slconn->sladdr, __func__, count);
              return -1;
            }

            memcpy (packetinfo->stationid, sourceid + 5, count);
            packetinfo->stationid[count] = '\0';
            packetinfo->stationidlength = count;
          }
        }
      }
    }
  }

  curstream = slconn->streams;

  /* For all-station mode */
  if (curstream != NULL && strcmp (curstream->stationid, "*") == 0)
  {
    curstream->seqnum = packetinfo->seqnum;

    if (timestamp[0])
      strcpy (curstream->timestamp, timestamp);

    return 0;
  }

  /* For multi-station mode, search the stream list and update all matching entries */
  while (curstream != NULL)
  {
    /* Use glob matching to match wildcarded station ID codes */
    if (sl_globmatch (packetinfo->stationid, curstream->stationid))
    {
      curstream->seqnum = packetinfo->seqnum;

      if (timestamp[0])
        strcpy (curstream->timestamp, timestamp);

      updates++;
    }

    curstream = curstream->next;
  }

  /* If no updates then no match was found */
  if (updates == 0)
    sl_log_r (slconn, 2, 0, "[%s] unexpected data received: %s\n", slconn->sladdr,
              packetinfo->stationid);

  return (updates == 0) ? -1 : 0;
} /* End of update_stream() */

/** ************************************************************************
 * @brief Initialize a new ::SLCD
 *
 * Allocate a new ::SLCD and set default values.
 *
 * The \a clientname must be specified and should be a string
 * describing the name of the client program. The \a clientversion is
 * optional and should be the version of the client program.  These
 * values are passed directly to sl_set_clientname().
 *
 * @param[in] clientname     Name of the client program
 * @param[in] clientversion  Version of the client program
 *
 * @returns An initialized ::SLCD on success, NULL on error.
 ***************************************************************************/
SLCD *
sl_initslcd (const char *clientname, const char *clientversion)
{
  SLCD *slconn;

  slconn = (SLCD *)malloc (sizeof (SLCD));

  if (slconn == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return NULL;
  }

  memset (slconn, 0, sizeof (SLCD));

  /* Set defaults */
  slconn->sladdr = NULL;
  slconn->slhost = NULL;
  slconn->slport = NULL;
  slconn->clientname = NULL;
  slconn->clientversion = NULL;
  slconn->start_time = NULL;
  slconn->end_time = NULL;
  slconn->keepalive = 0;
  slconn->iotimeout = 60;
  slconn->netto = 600;
  slconn->netdly = 30;
  slconn->auth_value = NULL;
  slconn->auth_finish = NULL;
  slconn->auth_data = NULL;
  slconn->streams = NULL;
  slconn->info = NULL;
  slconn->noblock = 0;
  slconn->dialup = 0;
  slconn->batchmode = 0;
  slconn->lastpkttime = 1;
  slconn->terminate = 0;
  slconn->resume = 1;
  slconn->multistation = 0;

  slconn->link = -1;
  slconn->protocol = UNSET_PROTO;
  slconn->protocol_forced = 0;
  slconn->config_error = 0;
  slconn->server_protocols = 0;
  slconn->capabilities = NULL;
  slconn->caparray = NULL;
  slconn->tls = 0;
  slconn->tlsctx = NULL;

  /* Allocate the associated persistent state struct */
  if ((slconn->stat = (SLstat *)malloc (sizeof (SLstat))) == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    free (slconn);
    return NULL;
  }

  memset (slconn->stat, 0, sizeof (SLstat));

  slconn->stat->packetinfo.seqnum = SL_UNSETSEQUENCE;
  slconn->stat->packetinfo.payloadlength = 0;
  slconn->stat->packetinfo.payloadcollected = 0;
  slconn->stat->packetinfo.payloadformat = SLPAYLOAD_UNKNOWN;

  slconn->stat->netto_time = 0;
  slconn->stat->netdly_time = 0;
  slconn->stat->keepalive_time = 0;

  slconn->stat->conn_state = DOWN;
  slconn->stat->stream_state = HEADER;
  slconn->stat->query_state = NoQuery;

  slconn->log = NULL;

  slconn->recvdatalen = 0;

  /* Store copies of client name and version */
  if (clientname && sl_set_clientname (slconn, clientname, clientversion))
  {
    sl_freeslcd (slconn);
    return NULL;
  }

  return slconn;
} /* End of sl_newslcd() */

/** ************************************************************************
 * @brief Free all memory associated with a ::SLCD
 *
 * Free all memory associated with a SLCD struct including the
 * associated stream list and persistent connection state.
 *
 * @param[in] slconn     SeedLink connection description to free
 ***************************************************************************/
void
sl_freeslcd (SLCD *slconn)
{
  SLstream *curstream;
  SLstream *nextstream;

  if (!slconn)
    return;

  curstream = slconn->streams;

  /* Traverse the stream list and free memory */
  while (curstream != NULL)
  {
    nextstream = curstream->next;

    if (curstream->selectors != NULL)
      free (curstream->selectors);
    free (curstream);

    curstream = nextstream;
  }

  free (slconn->sladdr);
  free (slconn->slhost);
  free (slconn->slport);
  free (slconn->start_time);
  free (slconn->end_time);
  free (slconn->capabilities);
  free (slconn->caparray);
  free (slconn->clientname);
  free (slconn->clientversion);
  free (slconn->stat);
  free (slconn->log);
  free_internal_auth_data (slconn);
  free (slconn);
} /* End of sl_freeslcd() */

/** ************************************************************************
 * @brief Set client name and version reported to server (v4 only)
 *
 * Set the program name and, optionally, version that will be send to
 * the server in protocol v4 version.  These values will be combined
 * into a value with the pattern:
 *   NAME[/VERSION]
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] name       Name of the client program
 * @param[in] version    Version of the client program
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_clientname (SLCD *slconn, const char *name, const char *version)
{
  char *newname = NULL;
  char *newversion = NULL;

  if (!slconn || !name)
    return -1;

  newname = strdup (name);

  if (newname == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  if (version)
  {
    newversion = strdup (version);

    if (newversion == NULL)
    {
      sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
      free (newname);
      return -1;
    }
  }

  free (slconn->clientname);
  free (slconn->clientversion);

  slconn->clientname = newname;
  slconn->clientversion = newversion;

  return 0;
} /* End of sl_set_clientname() */

/** ************************************************************************
 * @brief Set SeedLink server address (and port)
 *
 * Set the address (and port) of the SeedLink server to connect to.  The
 * \p server_address string should be in the following format:
 *
 *  \c HOST:PORT
 *
 * where \c HOST and \c PORT are both optional.  If \c HOST is not
 * specified the value of \a SL_DEFAULT_HOST (usually "localhost") will
 * be used.  If \c PORT is not specified the value of \a SL_DEFAULT_PORT
 * (usually "18000") will be used.
 *
 * The \c HOST value can be an IPv4 or IPv6 address, or a hostname.  The
 * \c PORT value must be a valid port number.
 *
 * The following variations are supported:
 * ```
 * 192.168.0.1
 * :18000
 * 192.168.0.1:18000
 * seedlink.datacenter.org:18500
 * 2607:f8b0:400a:805::200e
 * [2607:f8b0:400a:805::200e]:18000
 * ```
 *
 * This routine will also set the \a tls flag if the port is the default
 * for a secure connection, aka \a SL_SECURE_PORT.
 *
 * @param slconn          SeedLink connection description
 * @param server_address  Server address in \c HOST:PORT format
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_serveraddress (SLCD *slconn, const char *server_address)
{
  const char *hostptr;
  const char *portptr;
  size_t hostlen;
  const char *separator;
  const char *search;
  const char *open;
  const char *close;
  char *new_sladdr = NULL;
  char *new_slhost = NULL;
  char *new_slport = NULL;

  if (!slconn || !server_address)
    return -1;

  /* Check for host enclosed in square brackets, e.g. for raw IPv6 addresses */
  if ((open = strchr (server_address, '[')) != NULL &&
      (close = strchr (server_address, ']')) != NULL && open < close)
  {
    search = close + 1;
  }
  else
  {
    search = server_address;
  }

  /* A bare (unbracketed) IPv6 address contains more than one ':'; every
   * other supported form (hostname, IPv4, either with an optional port)
   * contains at most one, so more than one ':' with no brackets cannot be
   * host:port. Treat the whole string as the host with the default port
   * rather than splitting on the last ':', which would otherwise cut a raw
   * IPv6 address in two. */
  if (search == server_address && strchr (server_address, ':') != strrchr (server_address, ':'))
  {
    hostptr = server_address;
    hostlen = strlen (server_address);
    portptr = SL_DEFAULT_PORT;
  }
  else
  {
    /* Search address for host-port separator, i.e. last ':' */
    separator = strrchr (search, ':');

    /* If address begins with the separator */
    if (server_address == separator)
    {
      hostptr = SL_DEFAULT_HOST;
      hostlen = strlen (SL_DEFAULT_HOST);

      if (server_address[1] == '\0') /* Only a separator */
      {
        portptr = SL_DEFAULT_PORT;
      }
      else /* Only a port */
      {
        portptr = server_address + 1;
      }
    }
    /* Otherwise if no separator, use default port */
    else if (separator == NULL)
    {
      hostptr = server_address;
      hostlen = strlen (server_address);
      portptr = SL_DEFAULT_PORT;
    }
    /* Otherwise separate host and port */
    else
    {
      hostptr = server_address;
      hostlen = (size_t)(separator - server_address);

      /* Handle case of separator present but nothing following */
      if (strlen (separator + 1) > 0)
        portptr = separator + 1;
      else
        portptr = SL_DEFAULT_PORT;
    }
  }

  /* Remove brackets from host if present, i.e. for raw IPv6 addresses */
  if (hostlen >= 2 && hostptr[0] == '[' && hostptr[hostlen - 1] == ']')
  {
    hostptr += 1;
    hostlen -= 2;
  }

  /* Copy host and port to newly allocated buffers before touching the
   * SLCD, since hostptr/portptr may point into slconn->sladdr itself */
  if ((new_slhost = (char *)malloc (hostlen + 1)) != NULL)
  {
    memcpy (new_slhost, hostptr, hostlen);
    new_slhost[hostlen] = '\0';
  }

  new_slport = strdup (portptr);

  if (server_address != slconn->sladdr)
    new_sladdr = strdup (server_address);

  if (new_slhost == NULL || new_slport == NULL ||
      (server_address != slconn->sladdr && new_sladdr == NULL))
  {
    free (new_sladdr);
    free (new_slhost);
    free (new_slport);
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  /* Store the user-supplied address if not set directly */
  if (server_address != slconn->sladdr)
  {
    free (slconn->sladdr);
    slconn->sladdr = new_sladdr;
  }

  free (slconn->slhost);
  free (slconn->slport);

  slconn->slhost = new_slhost;
  slconn->slport = new_slport;

  /* Set TLS flag if port is the TLS default */
  if (strcmp (slconn->slport, SL_SECURE_PORT) == 0)
  {
    sl_set_tlsmode (slconn, 1);
  }

  return 0;
} /* End of sl_set_serveraddress() */

/** ************************************************************************
 * @brief Set SeedLink connection time window (begin and end times)
 *
 * Set the connection time window limits.  This will trigger the
 * connection to be negotiated with the \c TIME command for v3 connections
 * and a time range to be included with the \c DATA command of v4 connections.
 *
 * No validation of the time strings is done, so the user must ensure that
 * the target SeedLink server supports time string format supplied.  A
 * relatively safe format is <code>yyyy-mm-ddTHH:MM:SS</code>.
 *
 * @param slconn      SeedLink connection description
 * @param start_time  Starting time string in <code>yyyy-mm-ddTHH:MM:SS</code> format
 * @param end_time    Ending time string in <code>yyyy-mm-ddTHH:MM:SS</code> format
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_timewindow (SLCD *slconn, const char *start_time, const char *end_time)
{
  if (!slconn || (!start_time && !end_time))
    return -1;

  free (slconn->start_time);
  free (slconn->end_time);
  slconn->start_time = NULL;
  slconn->end_time = NULL;

  if (start_time && (slconn->start_time = strdup (start_time)) == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  if (end_time && (slconn->end_time = strdup (end_time)) == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  return 0;
} /* End of sl_set_timewindow() */

/* Internal auth_value handler to return auth_data */
static const char *
internal_auth_value_data (const char *server, void *auth_data)
{
  (void)server; /* Unused parameter */
  return (const char *)auth_data;
}

/* Free the auth_data allocated by sl_set_auth_envvars(), if present */
static void
free_internal_auth_data (SLCD *slconn)
{
  if (slconn->auth_value == internal_auth_value_data && slconn->auth_data)
  {
    memset (slconn->auth_data, 0, strlen ((char *)slconn->auth_data));
    free (slconn->auth_data);
    slconn->auth_data = NULL;
  }
}

/** ************************************************************************
 * @brief Set SeedLink connection authentication parameters (v4 only)
 *
 * Set the callback functions and callback data used for authentication
 * of SeedLink connections.  This is only relevant for the v4 protocol.
 *
 * The \a auth_value callback is executed to retrieve the authentication
 * value to be sent to the server.  This value is transmitted with the
 * \c AUTH command for v4 connections.  The following values are specified
 * by the v4 protocol:
 * ```
 * USERPASS <username> <password>
 * ```
 * and
 * ```
 * JWT <token>
 * ```
 *
 * If \a auth_value returns NULL or an empty string, no credentials are
 * available and the connection attempt is aborted as an authentication
 * failure; \a auth_finish, if set, is still called in that case.
 *
 * The \a auth_finish callback, if not NULL, is executed when authentication
 * is complete. This can be used to free memory or perform other cleanup tasks.
 * Note that it runs on every connection attempt, not only at teardown, so it
 * must not free anything needed by a later reconnect.
 *
 * The \a auth_data parameter is a pointer to caller-supplied data that
 * is passed to the callback functions; the library never frees it.
 *
 * There is no requirement that servers must support authentication, so
 * the user must ensure that the target server supports authentication.
 *
 * @param slconn        SeedLink connection description
 * @param auth_value    Callback executed to retrieve the authentication value
 * @param auth_finish   Callback executed when authentication is complete
 * @param auth_data     Caller-supplied data passed to the callback functions
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_auth_params (SLCD *slconn, const char *(*auth_value) (const char *server, void *auth_data),
                    void (*auth_finish) (const char *server, void *auth_data), void *auth_data)
{
  if (!slconn)
    return -1;

  if (auth_data != slconn->auth_data)
    free_internal_auth_data (slconn);

  slconn->auth_value = auth_value;
  slconn->auth_finish = auth_finish;
  slconn->auth_data = auth_data;

  return 0;
} /* End of sl_set_auth_params() */

/** ************************************************************************
 * @brief Configure authentication with environment variables
 *
 * Use the specified environment variables to set the authentication
 * parameters for the SeedLink connection.
 *
 * The constructed authentication value is owned by the library and is
 * released by sl_freeslcd() or by a subsequent call that sets the
 * authentication parameters.
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] uservar    Environment variable for username
 * @param[in] passvar    Environment variable for password
 *
 * @retval  0 : success
 * @retval -1 : error
 *
 * @sa sl_set_auth_params()
 ***************************************************************************/
int
sl_set_auth_envvars (SLCD *slconn, const char *uservar, const char *passvar)
{
  if (!slconn)
  {
    return -1;
  }

  const char *username = getenv (uservar);
  const char *password = getenv (passvar);

  if (username == NULL || password == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error retrieving authentication environment variables\n",
              __func__);

    if (username == NULL)
    {
      sl_log_r (NULL, 2, 0, "  Environment variable %s not set\n", uservar);
    }
    if (password == NULL)
    {
      sl_log_r (NULL, 2, 0, "  Environment variable %s not set\n", passvar);
    }

    return -1;
  }

  /* Create AUTH value of "USERPASS <username> <password>" */
  size_t avlength = strlen (username) + strlen (password) + 11;

  char *auth_value = (char *)malloc (avlength);
  if (auth_value == NULL)
  {
    sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  snprintf (auth_value, avlength, "USERPASS %s %s", username, password);

  /* Set the authentication parameters */
  sl_set_auth_params (slconn, internal_auth_value_data, NULL, auth_value);

  return 0;
}

/** ************************************************************************
 * @brief Set SeedLink connection keep alive interval in seconds
 *
 * Keep alive packets are sent to the server at the specified interval
 * when no data is being received to maintain the connection.
 *
 * By default, keep alive packets are disabled.
 *
 * @param slconn       SeedLink connection description
 * @param keepalive    Keep alive interval in seconds, 0 to disable
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_keepalive (SLCD *slconn, int keepalive)
{
  if (!slconn)
    return -1;

  slconn->keepalive = keepalive;

  return 0;
} /* End of sl_set_keepalive() */

/** ************************************************************************
 * @brief Set SeedLink connection I/O timeout in seconds
 *
 * Set the I/O timeout for the SeedLink connection.  This is the maximum
 * time allowed for a read or write operation to complete before the
 * connection is considered to be in a failed state and disconnected.
 *
 * By default, the I/O timeout is set to 60 seconds.
 *
 * @param slconn       SeedLink connection description
 * @param iotimeout    I/O timeout in seconds, 0 to disable
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_iotimeout (SLCD *slconn, int iotimeout)
{
  if (!slconn)
    return -1;

  slconn->iotimeout = iotimeout;

  return 0;
} /* End of sl_set_iotimeout() */

/** ************************************************************************
 * @brief Set SeedLink connection idle timeout in seconds
 *
 * Set the idle connection timeout.  This is the maximum time allowed
 * for a connection to be idle, after which it will be disconnected.
 *
 * By default, the network timeout is set to 600 seconds.
 *
 * @param slconn       SeedLink connection description
 * @param idletimeout  Network timeout in seconds, 0 to disable
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_idletimeout (SLCD *slconn, int idletimeout)
{
  if (!slconn)
    return -1;

  slconn->netto = idletimeout;

  return 0;
} /* End of sl_set_idletimeout() */

/** ************************************************************************
 * @brief Set SeedLink re-connection delay in seconds
 *
 * Set the re-connection delay.  This is the number of seconds to wait
 * before attempting to re-connect to the SeedLink server after a
 * connection has been lost.
 *
 * By default, the network delay is set to 30 seconds.
 *
 * @param slconn          SeedLink connection description
 * @param reconnectdelay  Network delay in seconds, 0 to disable
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_reconnectdelay (SLCD *slconn, int reconnectdelay)
{
  if (!slconn)
    return -1;

  slconn->netdly = reconnectdelay;

  return 0;
} /* End of sl_set_reconnectdelay() */

/** ************************************************************************
 * @brief Set or unset the SeedLink connection blocking mode
 *
 * Set the SeedLink connction to block or non-blocking mode.  In blocking
 * mode sl_collect() will block until data is received or the connection
 * is closed.  In non-blocking mode sl_collect() will return quickly.
 *
 * By default, the connection is set to blocking mode.
 *
 * @param slconn        SeedLink connection description
 * @param nonblock      Boolean flag, if non-zero set to non-blocking mode

 * @retval  0 : success
 * @retval -1 : error
 *
 * @sa sl_collect()
 ***************************************************************************/
int
sl_set_blockingmode (SLCD *slconn, int nonblock)
{
  if (!slconn)
    return -1;

  slconn->noblock = (nonblock) ? 1 : 0;

  return 0;
} /* End of sl_set_blockingmode() */

/** ************************************************************************
 * @brief Set or unset the SeedLink connection dial-up mode
 *
 * Set the SeedLink connction to dial-up mode.  In dial-up mode the
 * connection will be closed after the last data packet available
 * from the server is transmitted.
 *
 * By default, the connection is set to remain open.
 *
 * @param slconn        SeedLink connection description
 * @param dialup        Boolean flag, if non-zero set to dial-up mode
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_dialupmode (SLCD *slconn, int dialup)
{
  if (!slconn)
    return -1;

  slconn->dialup = (dialup) ? 1 : 0;

  return 0;
} /* End of sl_set_dialupmode() */

/** ************************************************************************
 * @brief Set or unset the SeedLink connection batch mode (v3 only)
 *
 * Set the SeedLink connction to batch mode.  In batch mode the client
 * can send multiple commands to the server before waiting for a response.
 * The server will send the responses in the order the commands were received.
 *
 * By default, the connection is set to non-batch mode.
 *
 * @param slconn        SeedLink connection description
 * @param batchmode     Boolean flag, if non-zero set to batch mode
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_batchmode (SLCD *slconn, int batchmode)
{
  if (!slconn)
    return -1;

  slconn->batchmode = (batchmode) ? 1 : 0;

  return 0;
} /* End of sl_set_batchmode() */

/** ************************************************************************
 * @brief Enable or disable TLS for the SeedLink connection
 *
 * By default, TLS is enabled for port number 18500, and for all other
 * ports it is disabled.  This function can be used to expliclty enable
 * or disable TLS for the connection.
 *
 * This function must be run after sl_set_serveraddress() to disable TLS
 * on port 18500.
 *
 * @param slconn     SeedLink connection description
 * @param tlsmode    Boolean flag, if non-zero enable TLS
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_tlsmode (SLCD *slconn, int tlsmode)
{
  if (!slconn)
    return -1;

  slconn->tls = (tlsmode) ? 1 : 0;

  return 0;
} /* End of sl_set_tlsmode() */

/** ************************************************************************
 * @brief Set the protocol version for the SeedLink connection
 *
 * By default, the protocol version defaults to v3 and if the server
 * advertises v4 it will be promoted to v4.  This function allows the
 * calling program to set the protocol version explicitly.
 *
 * @param slconn     SeedLink connection description
 * @param protocol   Protocol version to use, ::SLPROTO3X or ::SLPROTO40
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_protocol (SLCD *slconn, LIBPROTOCOL protocol)
{
  if (!slconn)
    return -1;

  slconn->protocol = protocol;
  slconn->protocol_forced = (protocol != UNSET_PROTO);

  return 0;
} /* End of sl_set_protocol() */

/** ************************************************************************
 * sl_addstream:
 *
 * Add a new stream entry to the stream list for the given ::SLCD
 * struct.  No checking is done for duplicate streams.
 *
 * The use of this function will enable multi-station mode.
 *
 * The \a seqnum parameter should be the last received sequence number
 * for the stream to resume the connection from previous data transfer.
 * The \a seqnum can also be ::SL_UNSETSEQUENCE to start from the next
 * available data.  The \a seqnum be ::SL_ALLDATASEQUENCE to request
 * all available data from the server (v4 only).
 *
 * The stream list is sorted alphanumerically by station ID,
 * and partitioned by the presence of wildcard characters in the
 * station ID, starting with more specific entries first.
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] stationid  Station ID
 * @param[in] selectors  Selectors for the station ID, NULL if none
 * @param[in] seqnum     Last received sequence number or special value
 * @param[in] timestamp  Start time for the stream, NULL if not used
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_add_stream (SLCD *slconn, const char *stationid, const char *selectors, uint64_t seqnum,
               const char *timestamp)
{
  SLstream *curstream;
  SLstream *newstream;
  SLstream *followstream = NULL;
  int newparitition = 0;
  int partition = 0;
  char isotime[32] = {0};

  if (!slconn || !stationid)
    return -1;

  /* Sanity, check for a all-station mode entry */
  if (slconn->streams)
  {
    if (strcmp (slconn->streams->stationid, "*") == 0)
    {
      sl_log_r (slconn, 2, 0, "[%s] %s(): all-station mode already configured!\n", slconn->sladdr,
                __func__);
      return -1;
    }
  }

  /* Convert old comma-delimited date-time to ISO-compatible format if needed
   * Example: '2021,11,19,17,23,18' => '2021-11-18T17:23:18.0Z' */
  if (timestamp)
  {
    if (strlen (timestamp) > sizeof (isotime) - 2)
    {
      sl_log_r (slconn, 2, 0, "%s(): timestamp for %s entry is too long: '%s'\n", __func__,
                stationid, timestamp);
      return -1;
    }

    strncpy (isotime, timestamp, sizeof (isotime) - 1);

    if (sl_isodatetime (isotime, isotime) == NULL)
    {
      sl_log_r (slconn, 2, 0, "%s(): could not convert timestamp for %s entry: '%s'\n", __func__,
                stationid, isotime);
      return -1;
    }
  }

  newstream = (SLstream *)malloc (sizeof (SLstream));

  if (newstream == NULL)
  {
    sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
    return -1;
  }

  memset (newstream, 0, sizeof (SLstream));

  strncpy (newstream->stationid, stationid, sizeof (newstream->stationid) - 1);
  newstream->stationid[sizeof (newstream->stationid) - 1] = '\0';

  newstream->selectors = NULL;

  if (selectors && (newstream->selectors = strdup (selectors)) == NULL)
  {
    sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
    free (newstream);
    return -1;
  }

  newstream->seqnum = seqnum;

  strcpy (newstream->timestamp, isotime);

  /* Search the stream list to find the proper insertion point.
   * The resulting list is sorted alphanumerically and partitioned by:
   * 1) no-wildcards in NET_STA, followed by
   * 2) ? wildcards in NET_STA, followed by
   * 3) * wildcards in NET_STA. */
  newparitition = (strchr (stationid, '*')) ? 3 : (strchr (stationid, '?')) ? 2 : 1;
  curstream = slconn->streams;
  while (curstream)
  {
    /* Determine wildcard partition */
    partition = (strchr (curstream->stationid, '*'))   ? 3
                : (strchr (curstream->stationid, '?')) ? 2
                                                       : 1;

    /* Compare partitions */
    if (newparitition < partition)
    {
      break;
    }
    else if (newparitition > partition)
    {
      followstream = curstream;
      curstream = curstream->next;
      continue;
    }

    /* Compare alphanumerically */
    if (strcmp (curstream->stationid, stationid) > 0)
    {
      break;
    }

    followstream = curstream;
    curstream = curstream->next;
  }

  /* Add new entry to the list */
  if (followstream)
  {
    newstream->next = followstream->next;
    followstream->next = newstream;
  }
  else
  {
    newstream->next = slconn->streams;
    slconn->streams = newstream;
  }

  slconn->multistation = 1;

  return 0;
} /* End of sl_add_stream() */

/** ************************************************************************
 * @brief Set the parameters for an all-station mode connection
 *
 * Set the parameters for all-station mode using a wildcard (*) for the
 * station ID.  If the stream entry already exists, overwrite the previous
 * settings.
 *
 * For SeedLink v3 this is "uni-station" mode.
 *
 * Also set the multistation flag to false (0).
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] selectors  Selectors for the station ID, NULL if none
 * @param[in] seqnum     Last received sequence number or ::SL_UNSETSEQUENCE
 * @param[in] timestamp  Start time for the stream, NULL if not used
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_set_allstation_params (SLCD *slconn, const char *selectors, uint64_t seqnum,
                          const char *timestamp)
{
  SLstream *newstream;
  char isotime[32] = {0};

  if (!slconn)
    return -1;

  /* Convert old comma-delimited date-time to ISO-compatible format if needed
   * Example: '2021,11,19,17,23,18' => '2021-11-18T17:23:18.0Z' */
  if (timestamp)
  {
    if (strlen (timestamp) > sizeof (isotime) - 2)
    {
      sl_log_r (slconn, 2, 0, "%s(): timestamp for all-station mode is too long: '%s'\n", __func__,
                timestamp);
      return -1;
    }

    strncpy (isotime, timestamp, sizeof (isotime) - 1);

    if (sl_isodatetime (isotime, isotime) == NULL)
    {
      sl_log_r (slconn, 2, 0, "%s(): could not convert timestamp for all-station mode: '%s'\n",
                __func__, isotime);
      return -1;
    }
  }

  newstream = slconn->streams;

  if (newstream == NULL)
  {
    newstream = (SLstream *)malloc (sizeof (SLstream));

    if (newstream == NULL)
    {
      sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
      return -1;
    }

    memset (newstream, 0, sizeof (SLstream));
  }
  else if (strcmp (newstream->stationid, "*") != 0)
  {
    sl_log_r (slconn, 2, 0, "[%s] %s(): multi-station mode already configured!\n", slconn->sladdr,
              __func__);
    return -1;
  }

  /* Set the station ID to an all-matching, single wildcard */
  strncpy (newstream->stationid, "*", sizeof (newstream->stationid));

  free (newstream->selectors);
  if (selectors)
    newstream->selectors = strdup (selectors);
  else
    newstream->selectors = NULL;

  newstream->seqnum = seqnum;

  strcpy (newstream->timestamp, isotime);

  newstream->next = NULL;

  slconn->streams = newstream;

  slconn->multistation = 0;

  return 0;
} /* End of sl_set_allstation_params() */

/** ************************************************************************
 * @brief Submit an INFO request to the server at the next opportunity
 *
 * Add an INFO request to the SeedLink Connection Description.
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] infostr    INFO level to request
 *
 * @retval  0 : success
 * @retval -1 : error
 ***************************************************************************/
int
sl_request_info (SLCD *slconn, const char *infostr)
{
  if (!slconn || !infostr)
    return -1;

  if (slconn->info != NULL)
  {
    sl_log_r (slconn, 2, 0, "[%s] Cannot request INFO '%.20s', another is pending\n",
              slconn->sladdr, infostr);
    return -1;
  }
  else
  {
    slconn->info = strdup (infostr);

    if (slconn->info == NULL)
    {
      sl_log_r (NULL, 2, 0, "%s(): error allocating memory\n", __func__);
      return -1;
    }

    return 0;
  }
} /* End of sl_request_info() */

/** ************************************************************************
 * @brief Check if server capabilities include specified value
 *
 * The server capabilities returned during connection negotiation are
 * searched for matches to the specified \a capability.
 *
 * NOTE: Only the capabilities listed in the response to the \a HELLO
 * command are available for checking.  Full server capabilities are
 * available with a \a INFO request.
 *
 * @param[in] slconn     SeedLink connection description
 * @param[in] capability Capabilty string to search for (case sensitive)
 *
 * @retval 0 Capability is not supported or unknown
 * @retval >0 Capability is supported
 ***************************************************************************/
int
sl_hascapability (SLCD *slconn, char *capability)
{
  int length;
  int start;
  int idx;

  if (!slconn || !capability)
    return 0;

  if (!slconn->capabilities)
    return 0;

  length = strlen (slconn->capabilities);
  /* Create capabilities array if needed */
  if (slconn->caparray == NULL)
  {
    /* Copy and replace spaces with terminating NULLs */
    slconn->caparray = strdup (slconn->capabilities);

    if (slconn->caparray == NULL)
    {
      sl_log_r (slconn, 2, 0, "%s(): error allocating memory\n", __func__);
      return 0;
    }

    for (idx = 0; idx < length; idx++)
    {
      if (slconn->caparray[idx] == ' ')
        slconn->caparray[idx] = '\0';
    }
  }

  /* Search capabilities array for a matching entry */
  for (idx = 0, start = -1; idx < length; idx++)
  {
    /* Determine if at the start of a capability flag:
       either initial state or following a terminating NULL */
    if (slconn->caparray[idx] == '\0')
      start = -1;
    else if (start == -1)
      start = 1;
    else
      start = 0;

    if (start == 1 && strcmp (slconn->caparray + idx, capability) == 0)
      return 1;
  }

  return 0;
} /* End of sl_hascapablity() */

/** ************************************************************************
 * @brief Trigger a termination of the SeedLink connection
 *
 * Set the terminate flag in the SLCD, which will cause the
 * connection to be terminated at the next opportunity.
 *
 * @param[in] slconn     SeedLink connection description
 ***************************************************************************/
void
sl_terminate (SLCD *slconn)
{
  if (!slconn)
    return;

  sl_log_r (slconn, 1, 1, "[%s] Terminating connection\n", slconn->sladdr);

  slconn->terminate = 1;
} /* End of sl_terminate() */

/* Internal termination routine for use as a signal handler.
 * Only sets the terminate flag directly; sl_terminate() is avoided here
 * because it logs, and the logging path is not async-signal-safe. */
static void
internal_term_handler (int sig)
{
  (void)sig;

  if (global_termination_SLCD)
    global_termination_SLCD->terminate = 1;
}

/** ************************************************************************
 * @brief Set signal handlers that trigger connection shutdown.
 *
 * @warning This function is not thread safe due to use of static variables.
 *
 * This routine will set the signal handlers for `SIGINT` and `SIGTERM`
 * that trigger connection shutdown for the specified SLCD.  On Windows
 * the `SIGABRT` signal is also set.  On all other platforms the
 * `SIGQUIT` signal is also set.
 *
 * @return 0 on success and -1 on error.
 ***************************************************************************/
int
sl_set_termination_handler (SLCD *slconn)
{
  if (slconn == NULL)
    return -1;

  global_termination_SLCD = slconn;

#if defined(SLP_WIN)
  signal (SIGINT, internal_term_handler);
  signal (SIGTERM, internal_term_handler);
  signal (SIGABRT, internal_term_handler);
#else
  struct sigaction sa;

  sigemptyset (&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  sa.sa_handler = internal_term_handler;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
#endif

  return 0;
} /* End of sl_set_termination_handler() */

/** ************************************************************************
 * @brief Print user parameters of the SeedLink connection description
 *
 * Useful for diagnostic purposes, this routine will print the
 * details of the SeedLink connection description to the logging
 * facility.
 *
 * @param[in] slconn     SeedLink connection description
 ***************************************************************************/
void
sl_printslcd (SLCD *slconn)
{
  SLstream *curstream;
  char sequence[32];

  if (!slconn)
    return;

  sl_log_r (slconn, 0, 0, "SeedLink connection description:\n");
  sl_log_r (slconn, 0, 0, "             Address: %s\n", slconn->sladdr ? slconn->sladdr : "NULL");
  sl_log_r (slconn, 0, 0, "                Host: %s\n", slconn->slhost ? slconn->slhost : "NULL");
  sl_log_r (slconn, 0, 0, "                Port: %s\n", slconn->slport ? slconn->slport : "NULL");
  sl_log_r (slconn, 0, 0, "         Client name: %s\n",
            slconn->clientname ? slconn->clientname : "NULL");
  sl_log_r (slconn, 0, 0, "      Client version: %s\n",
            slconn->clientversion ? slconn->clientversion : "NULL");
  sl_log_r (slconn, 0, 0, "          Start time: %s\n",
            slconn->start_time ? slconn->start_time : "NULL");
  sl_log_r (slconn, 0, 0, "            End time: %s\n",
            slconn->end_time ? slconn->end_time : "NULL");
  sl_log_r (slconn, 0, 0, "          Keep alive: %d seconds\n", slconn->keepalive);
  sl_log_r (slconn, 0, 0, "         I/O timeout: %d seconds\n", slconn->iotimeout);
  sl_log_r (slconn, 0, 0, "        Idle timeout: %d seconds\n", slconn->netto);
  sl_log_r (slconn, 0, 0, "     Reconnect delay: %d seconds\n", slconn->netdly);
  sl_log_r (slconn, 0, 0, "        auth_value(): %s\n", (slconn->auth_value) ? "SET" : "NOT SET");
  sl_log_r (slconn, 0, 0, "       auth_finish(): %s\n", (slconn->auth_finish) ? "SET" : "NOT SET");
  sl_log_r (slconn, 0, 0, "           auth_data: %s\n", (slconn->auth_data) ? "SET" : "NOT SET");
  sl_log_r (slconn, 0, 0, "   Non-blocking mode: %d\n", slconn->noblock);
  sl_log_r (slconn, 0, 0, "        Dial-up mode: %d\n", slconn->dialup);
  sl_log_r (slconn, 0, 0, "          Batch mode: %d\n", slconn->batchmode);
  sl_log_r (slconn, 0, 0, "Use last packet time: %d\n", slconn->lastpkttime);
  sl_log_r (slconn, 0, 0, "           Terminate: %d\n", slconn->terminate);
  sl_log_r (slconn, 0, 0, "Resume with sequence: %d\n", slconn->resume);
  sl_log_r (slconn, 0, 0, "  Multi-station mode: %d\n", slconn->multistation);
  sl_log_r (slconn, 0, 0, "        INFO request: %s\n", slconn->info ? slconn->info : "NULL");
  sl_log_r (slconn, 0, 0, "         Stream list:\n");
  curstream = slconn->streams;
  while (curstream)
  {
    if (curstream->seqnum == SL_UNSETSEQUENCE)
      strcpy (sequence, "UNSET");
    else if (curstream->seqnum == SL_ALLDATASEQUENCE)
      strcpy (sequence, "ALLDATA");
    else
      snprintf (sequence, sizeof (sequence), "%" PRIu64, curstream->seqnum);

    sl_log_r (slconn, 0, 0, "             Station ID: %s\n", curstream->stationid);
    sl_log_r (slconn, 0, 0, "                  Selectors: %s\n",
              curstream->selectors ? curstream->selectors : "NULL");
    sl_log_r (slconn, 0, 0, "                   Sequence: %s\n", sequence);
    sl_log_r (slconn, 0, 0, "                 Time stamp: %s\n", curstream->timestamp);
    curstream = curstream->next;
  }
} /* End of sl_printslcd() */

/** ************************************************************************
 * @brief Detect miniSEED record in buffer
 *
 * Determine if the buffer contains a miniSEED data record by
 * verifying known signatures (fields with known limited values).
 *
 * At least SL_MIN_PAYLOAD bytes of data are required for detection.
 *
 * If miniSEED 2.x is detected, search the record up to recbuflen
 * bytes for a 1000 blockette. If no blockette 1000 is found, search
 * at 64-byte offsets for the fixed section of the next header,
 * thereby implying the record length.
 *
 * @param[in] buffer Buffer to test for known data types
 * @param[in] buflen Length of buffer
 * @param[out] payloadformat Payload type detected
 *
 * @retval -1 Data record not detected or error
 * @retval 0 Data record detected but could not determine length
 * @retval >0 Size of the record in bytes
 ***************************************************************************/
static int64_t
detect (const char *buffer, uint64_t buflen, char *payloadformat)
{
  uint8_t swapflag = 0; /* Byte swapping flag */
  int64_t reclen = -1;  /* Size of record in bytes */

  uint16_t blkt_offset; /* Byte offset for next blockette */
  uint16_t blkt_type;
  uint16_t next_blkt;
  const char *nextfsdh;

  if (!buffer || !payloadformat)
    return -1;

  if (buflen < SL_MIN_PAYLOAD)
    return -1;

  /* Check for valid header, set format version */
  *payloadformat = SLPAYLOAD_UNKNOWN;
  if (MS3_ISVALIDHEADER (buffer))
  {
    *payloadformat = SLPAYLOAD_MSEED3;

    if (!sl_littleendianhost ())
      swapflag = 1;

    uint16_t extralength = HO2u (*pMS3FSDH_EXTRALENGTH (buffer), swapflag);
    uint32_t datalength = HO4u (*pMS3FSDH_DATALENGTH (buffer), swapflag);

    reclen = MS3FSDH_LENGTH                 /* Length of fixed portion of header */
             + *pMS3FSDH_SIDLENGTH (buffer) /* Length of source identifier */
             + extralength                  /* Length of extra headers */
             + datalength;                  /* Length of data payload */

    /* Reject a record length that cannot survive the narrowing assignment
     * to payloadlength's uint32_t below, e.g. one that would wrap to 0. */
    if (reclen <= 0 || reclen > UINT32_MAX)
    {
      sl_log (2, 0, "Invalid miniSEED3 record length (%" PRId64 ")\n", reclen);
      return -1;
    }
  }
  else if (MS2_ISVALIDHEADER (buffer))
  {
    *payloadformat = SLPAYLOAD_MSEED2;
    reclen = 0;

    /* Check to see if byte swapping is needed by checking for sane year and day */
    if (!MS_ISVALIDYEARDAY (*pMS2FSDH_YEAR (buffer), *pMS2FSDH_DAY (buffer)))
      swapflag = 1;

    blkt_offset = HO2u (*pMS2FSDH_BLOCKETTEOFFSET (buffer), swapflag);

    /* Loop through blockettes as long as number is non-zero and viable */
    while (blkt_offset != 0 && blkt_offset > 47 && (blkt_offset + 4) <= buflen)
    {
      memcpy (&blkt_type, buffer + blkt_offset, 2);
      memcpy (&next_blkt, buffer + blkt_offset + 2, 2);

      if (swapflag)
      {
        sl_gswap2 (&blkt_type);
        sl_gswap2 (&next_blkt);
      }

      /* Found a 1000 blockette, not truncated */
      if (blkt_type == 1000 && (blkt_offset + 8) <= buflen)
      {
        /* Field 3 of B1000 is a uint8_t value describing the record
         * length as 2^(value).  Valid exponents span 64 bytes (6) to
         * 1 MiB (20); reject anything outside that range rather than
         * shift by an out-of-range amount. */
        uint8_t reclen_exp = *pMS2B1000_RECLEN (buffer + blkt_offset);

        if (reclen_exp < 6 || reclen_exp > 20)
        {
          sl_log (2, 0, "Invalid miniSEED2 B1000 record length exponent (%u)\n", reclen_exp);
          return -1;
        }

        reclen = (int64_t)1 << reclen_exp;

        break;
      }

      /* Safety check for invalid offset */
      if (next_blkt != 0 && (next_blkt < 4 || (next_blkt - 4) <= blkt_offset))
      {
        sl_log (
            2, 0,
            "Invalid miniSEED2 blockette offset (%d) less than or equal to current offset (%d)\n",
            next_blkt, blkt_offset);
        return -1;
      }

      blkt_offset = next_blkt;
    }

    /* If record length was not determined by a 1000 blockette scan the buffer
     * and search for the next record header, optionally preceded by an
     * intervening V3 SeedLink packet header (the record itself carries no
     * framing of its own, but a V3 data stream interleaves one 8-byte
     * header per record). */
    if (reclen == 0)
    {
      nextfsdh = buffer + 64;

      /* Check for record header or blank/noise record at 64-byte offsets */
      while ((size_t)((nextfsdh - buffer) + 48) < buflen)
      {
        if (MS2_ISVALIDHEADER (nextfsdh))
        {
          reclen = nextfsdh - buffer;

          break;
        }

        if (memcmp (nextfsdh, SIGNATURE_V3, 2) == 0 &&
            (size_t)((nextfsdh - buffer) + SLHEADSIZE_V3 + 48) < buflen &&
            MS2_ISVALIDHEADER (nextfsdh + SLHEADSIZE_V3))
        {
          reclen = nextfsdh - buffer;

          break;
        }

        nextfsdh += 64;
      }
    }
  } /* End of miniSEED 2.x detection */

  return reclen;
} /* End of detect() */
