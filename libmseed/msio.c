/***************************************************************************
 * I/O handling routines, for files and URLs.
 *
 * This file is part of the miniSEED Library.
 *
 * Copyright (c) 2024 Chad Trabant, EarthScope Data Services
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
 ***************************************************************************/

/* Define _LARGEFILE_SOURCE to get ftello/fseeko on some systems (Linux) */
#define _LARGEFILE_SOURCE 1

#include <errno.h>
#include <stddef.h>

#include "msio.h"

/* Include libcurl library header if URL supported is requested */
#if defined(LIBMSEED_URL)

#include <curl/curl.h>

/* Default timeouts, in seconds, for URL connections */
#define LIBMSEED_URL_CONNECTTIMEOUT_DEFAULT 60
#define LIBMSEED_URL_STALLTIMEOUT_DEFAULT 300

/* Control for enabling debugging information */
int libmseed_url_debug = -1;

/* Control for SSL peer and host verification */
long libmseed_ssl_noverify = -1;

/* Timeouts, in seconds, for URL connections; negative means unset */
long libmseed_url_connecttimeout = -1;
long libmseed_url_stalltimeout = -1;

/* A global libcurl easy handle for configuration options */
CURL *gCURLeasy = NULL;

/* A global libcurl list of headers */
struct curl_slist *gCURLheaders = NULL;

/* Receving callback parameters */
struct recv_callback_parameters
{
  char *buffer;
  size_t size;
  int is_paused;
};

/* Header callback parameters */
struct header_callback_parameters
{
  int64_t *startoffset;
  int64_t *endoffset;
  int range_honored;
};

/*********************************************************************
 * Callback fired when recv'ing data using libcurl.
 *
 * The destination buffer pointer and size in the callback parameters
 * are adjusted as data are added.
 *
 * Returns number of bytes added to the destination buffer.
 *********************************************************************/
static size_t
recv_callback (char *buffer, size_t size, size_t num, void *userdata)
{
  struct recv_callback_parameters *rcp = (struct recv_callback_parameters *)userdata;

  if (!buffer || !userdata)
    return 0;

  size *= num;

  /* Pause connection if passed data does not fit into destination buffer */
  if (size > rcp->size)
  {
    rcp->is_paused = 1;
    return CURL_WRITEFUNC_PAUSE;
  }
  /* Otherwise, copy data to destination buffer */
  else
  {
    memcpy (rcp->buffer, buffer, size);
    rcp->buffer += size;
    rcp->size -= size;
  }

  return size;
}

/*********************************************************************
 * Callback fired when receiving headers using libcurl.
 *
 * Returns number of bytes processed for success.
 *********************************************************************/
static size_t
header_callback (char *buffer, size_t size, size_t num, void *userdata)
{
  struct header_callback_parameters *hcp = (struct header_callback_parameters *)userdata;

  char startstr[21] = {0}; /* Maximum of 20 digit value */
  char endstr[21] = {0};   /* Maximum of 20 digit value */
  unsigned long long startval = 0;
  unsigned long long endval = 0;
  uint8_t startdigits = 0;
  uint8_t enddigits = 0;
  char *dash = NULL;
  char *ptr;

  if (!buffer || !userdata)
    return size * num;

  size *= num;

  /* Parse and store: "Content-Range: bytes START-END/TOTAL"
   * e.g. Content-Range: bytes 512-1023/4096 */
  if (size > 22 && lmp_strncasecmp (buffer, "Content-Range: bytes", 20) == 0)
  {
    /* Process each character, starting just after "bytes" unit */
    for (ptr = buffer + 20; *ptr != '\0' && (ptr - buffer) < (ptrdiff_t)size; ptr++)
    {
      /* Skip spaces before start of range */
      if (*ptr == ' ' && startdigits == 0)
        continue;
      /* Digits before dash, part of start */
      else if (isdigit ((unsigned char)*ptr) && dash == NULL)
        startstr[startdigits++] = *ptr;
      /* Digits after dash, part of end */
      else if (isdigit ((unsigned char)*ptr) && dash != NULL)
        endstr[enddigits++] = *ptr;
      /* If first dash found, store pointer */
      else if (*ptr == '-' && dash == NULL)
        dash = ptr;
      /* Nothing else is part of the range */
      else
        break;

      /* If digit sequences have exceeded limits, not a valid range */
      if (startdigits >= sizeof (startstr) || enddigits >= sizeof (endstr))
      {
        startdigits = 0;
        enddigits = 0;
        break;
      }
    }

    /* Convert start and end values to numbers if non-zero length,
     * rejecting values that overflow a signed 64-bit offset */
    if (startdigits)
    {
      startval = strtoull (startstr, NULL, 10);

      if (startval > INT64_MAX)
      {
        startdigits = 0;
        enddigits = 0;
      }
    }

    if (enddigits)
    {
      endval = strtoull (endstr, NULL, 10);

      if (endval > INT64_MAX)
      {
        startdigits = 0;
        enddigits = 0;
      }
    }

    /* The range is only honored if an offset was actually applied */
    if (hcp->startoffset && startdigits)
    {
      *hcp->startoffset = (int64_t)startval;
      hcp->range_honored = 1;
    }

    if (hcp->endoffset && enddigits)
    {
      *hcp->endoffset = (int64_t)endval;
      hcp->range_honored = 1;
    }
  }

  return size;
}

#endif /* defined(LIBMSEED_URL) */

/***************************************************************************
 * msio_fopen:
 *
 * Determine if requested path is a regular file or a URL and open or
 * initialize as appropriate.
 *
 * The 'mode' argument is only for file-system paths and ignored for
 * URLs.  If 'mode' is set to NULL, default is 'rb' mode.
 *
 * If 'startoffset' or 'endoffset' are non-zero they will be used to
 * position the stream for reading, either setting the read position
 * of a file or requesting a range via HTTP.  These will be set to the
 * actual range if reported via HTTP, which may be different than
 * requested.
 *
 * Return 0 on success and -1 on error.
 *
 * @ref MessageOnError - this function logs a message on error
 ***************************************************************************/
int
msio_fopen (LMIO *io, const char *path, const char *mode, int64_t *startoffset, int64_t *endoffset)
{
  int knownfile = 0;

  if (!io || !path)
    return -1;

  if (!mode)
    mode = "rb";

  /* Treat "file://" specifications as local files by removing the scheme */
  if (lmp_strncasecmp (path, "file://", 7) == 0)
  {
    path += 7;
    knownfile = 1;
  }

  /* Test for URL scheme via "://" */
  if (!knownfile && strstr (path, "://"))
  {
#if !defined(LIBMSEED_URL)
    (void)endoffset; /* Unused */
    ms_log (2, "URL support not included in library for %s\n", path);
    return -1;
#else
    long response_code;
    struct header_callback_parameters hcp;
    int range_requested = 0;

    io->type = LMIO_URL;
    io->handle2 = NULL;

    /* Check for URL debugging environment variable */
    if (libmseed_url_debug < 0)
    {
      if (getenv ("LIBMSEED_URL_DEBUG"))
        libmseed_url_debug = 1;
      else
        libmseed_url_debug = 0;
    }

    /* Check for SSL peer/host verify environment variable */
    if (libmseed_ssl_noverify < 0)
    {
      if (getenv ("LIBMSEED_SSL_NOVERIFY"))
        libmseed_ssl_noverify = 1;
      else
        libmseed_ssl_noverify = 0;
    }

    /* Check for stall (low-speed) timeout environment variable */
    if (libmseed_url_stalltimeout < 0)
    {
      char *timeoutstr = getenv ("LIBMSEED_URL_TIMEOUT");
      long timeoutval;

      if (timeoutstr && (timeoutval = strtol (timeoutstr, NULL, 10)) > 0)
        libmseed_url_stalltimeout = timeoutval;
      else
        libmseed_url_stalltimeout = LIBMSEED_URL_STALLTIMEOUT_DEFAULT;
    }

    if (libmseed_url_connecttimeout < 0)
      libmseed_url_connecttimeout = LIBMSEED_URL_CONNECTTIMEOUT_DEFAULT;

    /* Configure the libcurl easy handle, duplicate global options if present */
    io->handle = (gCURLeasy) ? curl_easy_duphandle (gCURLeasy) : curl_easy_init ();

    if (io->handle == NULL)
    {
      ms_log (2, "Cannot initialize CURL handle\n");
      return -1;
    }

    /* URL debug */
    if (libmseed_url_debug && curl_easy_setopt (io->handle, CURLOPT_VERBOSE, 1L) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_VERBOSE\n");
      goto onerror;
    }

    /* SSL peer and host verification */
    if (libmseed_ssl_noverify &&
        (curl_easy_setopt (io->handle, CURLOPT_SSL_VERIFYPEER, 0L) != CURLE_OK ||
         curl_easy_setopt (io->handle, CURLOPT_SSL_VERIFYHOST, 0L) != CURLE_OK))
    {
      ms_log (2, "Cannot set CURLOPT_SSL_VERIFYPEER and/or CURLOPT_SSL_VERIFYHOST\n");
      goto onerror;
    }

    /* Set URL */
    if (curl_easy_setopt (io->handle, CURLOPT_URL, path) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_URL\n");
      goto onerror;
    }

    /* Set default User-Agent header, can be overridden via custom header */
    if (curl_easy_setopt (io->handle, CURLOPT_USERAGENT,
                          "libmseed/" LIBMSEED_VERSION " libcurl/" LIBCURL_VERSION) != CURLE_OK)
    {
      ms_log (2, "Cannot set default CURLOPT_USERAGENT\n");
      goto onerror;
    }

    /* Disable signals */
    if (curl_easy_setopt (io->handle, CURLOPT_NOSIGNAL, 1L) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_NOSIGNAL\n");
      goto onerror;
    }

    /* Connection timeout, 0 disables */
    if (libmseed_url_connecttimeout > 0 &&
        curl_easy_setopt (io->handle, CURLOPT_CONNECTTIMEOUT, libmseed_url_connecttimeout) !=
            CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_CONNECTTIMEOUT\n");
      goto onerror;
    }

    /* Abort the transfer if it stalls below 1 byte/second, 0 disables */
    if (libmseed_url_stalltimeout > 0 &&
        (curl_easy_setopt (io->handle, CURLOPT_LOW_SPEED_LIMIT, 1L) != CURLE_OK ||
         curl_easy_setopt (io->handle, CURLOPT_LOW_SPEED_TIME, libmseed_url_stalltimeout) !=
             CURLE_OK))
    {
      ms_log (2, "Cannot set CURLOPT_LOW_SPEED_LIMIT and/or CURLOPT_LOW_SPEED_TIME\n");
      goto onerror;
    }

    /* Return failure codes on errors */
    if (curl_easy_setopt (io->handle, CURLOPT_FAILONERROR, 1L) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_FAILONERROR\n");
      goto onerror;
    }

    /* Follow HTTP redirects */
    if (curl_easy_setopt (io->handle, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_FOLLOWLOCATION\n");
      goto onerror;
    }

    /* Configure write callback for recv'ed data */
    if (curl_easy_setopt (io->handle, CURLOPT_WRITEFUNCTION, recv_callback) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_WRITEFUNCTION\n");
      goto onerror;
    }

    /* Configure the libcurl multi handle, for use with the asynchronous interface */
    if ((io->handle2 = curl_multi_init ()) == NULL)
    {
      ms_log (2, "Cannot initialize CURL multi handle\n");
      goto onerror;
    }

    if (curl_multi_add_handle (io->handle2, io->handle) != CURLM_OK)
    {
      ms_log (2, "Cannot add CURL handle to multi handle\n");
      goto onerror;
    }

    /* Set byte ranging */
    if ((startoffset && *startoffset > 0) || (endoffset && *endoffset > 0))
    {
      char startstr[21] = {0};
      char endstr[21] = {0};
      char rangestr[42];

      range_requested = 1;

      /* Build Range header value.
       * If start is undefined set it to zero if end is defined. */
      if (startoffset && *startoffset > 0)
        snprintf (startstr, sizeof (startstr), "%" PRId64, *startoffset);
      else if (endoffset && *endoffset > 0)
        snprintf (startstr, sizeof (startstr), "0");
      if (endoffset && *endoffset > 0)
        snprintf (endstr, sizeof (endstr), "%" PRId64, *endoffset);

      snprintf (rangestr, sizeof (rangestr), "%s-%s", startstr, endstr);

      /* Set Range header */
      if (curl_easy_setopt (io->handle, CURLOPT_RANGE, rangestr) != CURLE_OK)
      {
        ms_log (2, "Cannot set CURLOPT_RANGE to '%s'\n", rangestr);
        goto onerror;
      }
    }

    /* Set up header callback */
    if (startoffset || endoffset)
    {
      hcp.startoffset = startoffset;
      hcp.endoffset = endoffset;
      hcp.range_honored = 0;

      /* Configure header callback */
      if (curl_easy_setopt (io->handle, CURLOPT_HEADERFUNCTION, header_callback) != CURLE_OK)
      {
        ms_log (2, "Cannot set CURLOPT_HEADERFUNCTION\n");
        goto onerror;
      }

      if (curl_easy_setopt (io->handle, CURLOPT_HEADERDATA, (void *)&hcp) != CURLE_OK)
      {
        ms_log (2, "Cannot set CURLOPT_HEADERDATA\n");
        goto onerror;
      }
    }

    /* Set custom headers */
    if (gCURLheaders && curl_easy_setopt (io->handle, CURLOPT_HTTPHEADER, gCURLheaders) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_HTTPHEADER\n");
      goto onerror;
    }

    /* Set connection as still running */
    io->still_running = 1;

    /* Start connection, get status & headers, without consuming any data */
    msio_fread (io, NULL, 0);

    /* Detach the header callback and its data pointer (a local on this stack
     * frame) now that the response headers have been consumed, so a later
     * header callback (e.g. on trailing headers) cannot dereference the
     * pointer or abort the transfer by returning a short count. */
    if (startoffset || endoffset)
    {
      curl_easy_setopt (io->handle, CURLOPT_HEADERFUNCTION, NULL);
      curl_easy_setopt (io->handle, CURLOPT_HEADERDATA, NULL);
    }

    curl_easy_getinfo (io->handle, CURLINFO_RESPONSE_CODE, &response_code);

    if (response_code == 404)
    {
      ms_log (2, "Cannot open %s: Not Found (404)\n", path);
      goto onerror;
    }
    else if (response_code >= 400 && response_code < 600)
    {
      ms_log (2, "Cannot open %s: response code %ld\n", path, response_code);
      goto onerror;
    }

    /* Detect transfer-level failures with no HTTP status, e.g. connection errors */
    if (io->urlfail)
    {
      ms_log (2, "Cannot open %s: transfer failed\n", path);
      goto onerror;
    }

    /* Fail if a byte range was requested but the server did not honor it
     * (no Content-Range in the response); the full body starts at offset 0
     * and would otherwise silently include data outside the requested range. */
    if (range_requested && !hcp.range_honored)
    {
      ms_log (2, "Cannot open %s: server did not honor requested byte range\n", path);
      goto onerror;
    }
#endif /* defined(LIBMSEED_URL) */
  }
  else
  {
    io->type = LMIO_FILE;

    if ((io->handle = fopen (path, mode)) == NULL)
    {
      ms_log (2, "Cannot open: %s (%s)\n", path, strerror (errno));
      goto onerror;
    }

    /* Seek to position if start offset is provided */
    if (startoffset && *startoffset > 0)
    {
      if (lmp_fseek64 (io->handle, *startoffset, SEEK_SET))
      {
        ms_log (2, "Cannot seek in %s to offset %" PRId64 "\n", path, *startoffset);
        goto onerror;
      }
    }
  }

  return 0;

onerror:
  /* Release any open handle so it does not leak on error */
  msio_fclose (io);

  return -1;
} /* End of msio_fopen() */

/*********************************************************************
 * msio_fclose:
 *
 * Close an IO handle.
 *
 * Returns 0 on success and negative value on error.
 *
 * @ref MessageOnError - this function logs a message on error
 *********************************************************************/
int
msio_fclose (LMIO *io)
{
  int rv;

  if (!io)
  {
    ms_log (2, "%s(): Required input not defined: 'io'\n", __func__);
    return -1;
  }

  if (io->handle == NULL || io->type == LMIO_NULL)
    return 0;

  if (io->type == LMIO_FILE || io->type == LMIO_FD)
  {
    rv = fclose (io->handle);

    if (rv)
    {
      ms_log (2, "Error closing file (%s)\n", strerror (errno));
      return -1;
    }
  }
  else if (io->type == LMIO_URL)
  {
#if !defined(LIBMSEED_URL)
    ms_log (2, "URL support not included in library\n");
    return -1;
#else
    curl_multi_remove_handle (io->handle2, io->handle);
    curl_easy_cleanup (io->handle);
    curl_multi_cleanup (io->handle2);
#endif
  }

  io->type = LMIO_NULL;
  io->handle = NULL;
  io->handle2 = NULL;
  io->urlfail = 0;

  return 0;
} /* End of msio_fclose() */

/*********************************************************************
 * msio_fread:
 *
 * Read data from the identified IO handle into the specified buffer.
 * Up to the requested 'size' bytes are read.
 *
 * For URL support, with defined(LIBMSEED_URL), the destination
 * receive buffer MUST be at least as big as the curl receive buffer
 * (CURLOPT_BUFFERSIZE, which defaults to CURL_MAX_WRITE_SIZE of 16kB)
 * or the maximum size of a retrieved object if less than
 * CURL_MAX_WRITE_SIZE.  The caller must ensure this.
 *
 * Returns the number of bytes read on success and a negative value on
 * error.
 *********************************************************************/
int64_t
msio_fread (LMIO *io, void *buffer, size_t size)
{
  size_t read = 0;

  if (!io)
    return -1;

  if (!buffer && size > 0)
  {
    ms_log (2, "%s(): No buffer specified for non-zero size\n", __func__);
    return -1;
  }

  if (size > INT64_MAX)
  {
    ms_log (2, "%s(): Unsupported size, greater than INT64_MAX: %zu\n", __func__, size);
    return -1;
  }

  /* Read from regular file stream */
  if (io->type == LMIO_FILE || io->type == LMIO_FD)
  {
    read = fread (buffer, 1, size, io->handle);
  }
  /* Read from URL stream */
  else if (io->type == LMIO_URL)
  {
#if !defined(LIBMSEED_URL)
    ms_log (2, "URL support not included in library\n");
    return -1;
#else
    struct recv_callback_parameters rcp;
    struct timeval timeout;
    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    long curl_timeo = -1;
    int maxfd = -1;
    int rc;

    /* Report an error if a previous transfer failure was detected */
    if (io->urlfail)
      return -1;

    if (!io->still_running)
      return 0;

    /* Set up destination buffer in write callback parameters */
    rcp.buffer = buffer;
    rcp.size = size;
    if (curl_easy_setopt (io->handle, CURLOPT_WRITEDATA, (void *)&rcp) != CURLE_OK)
    {
      ms_log (2, "Cannot set CURLOPT_WRITEDATA\n");
      return -1;
    }

    /* Unpause connection */
    rcp.is_paused = 0;
    curl_easy_pause (io->handle, CURLPAUSE_CONT);

    /* Receive data while connection running, destination space available
     * and connection is not paused. */
    do
    {
      /* Default timeout for read failure */
      timeout.tv_sec = 60;
      timeout.tv_usec = 0;

      curl_multi_timeout (io->handle2, &curl_timeo);

      /* Tailor timeout based on maximum suggested by libcurl */
      if (curl_timeo >= 0)
      {
        timeout.tv_sec = curl_timeo / 1000;
        if (timeout.tv_sec > 1)
          timeout.tv_sec = 1;
        else
          timeout.tv_usec = (curl_timeo % 1000) * 1000;
      }

      FD_ZERO (&fdread);
      FD_ZERO (&fdwrite);
      FD_ZERO (&fdexcep);

      /* Extract descriptors from the multi-handle */
      if (curl_multi_fdset (io->handle2, &fdread, &fdwrite, &fdexcep, &maxfd) != CURLM_OK)
      {
        ms_log (2, "Error with curl_multi_fdset()\n");
        return -1;
      }

      /* libcurl/system needs time to work, sleep 100 milliseconds */
      if (maxfd == -1)
      {
        lmp_nanosleep (100000000);
        rc = 0;
      }
      else
      {
        rc = select (maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

        /* An interrupted select() is not an error, let libcurl proceed */
        if (rc < 0 && errno == EINTR)
        {
          rc = 0;
        }
        else if (rc < 0)
        {
          ms_log (2, "Error with select(): %s\n", strerror (errno));
          io->urlfail = 1;
          return -1;
        }
      }

      /* Receive data */
      if (rc >= 0)
      {
        curl_multi_perform (io->handle2, &io->still_running);
      }
    } while (io->still_running > 0 && !rcp.is_paused && (rcp.size > 0 || rcp.buffer == NULL));

    read = size - rcp.size;

    /* When the transfer is no longer running, check its completion status.
     * A non-OK result means the transfer failed (e.g. connection reset)
     * rather than reaching a clean end of stream. */
    if (!io->still_running)
    {
      CURLMsg *msg;
      int msgs_left;

      while ((msg = curl_multi_info_read (io->handle2, &msgs_left)))
      {
        if (msg->msg == CURLMSG_DONE && msg->data.result != CURLE_OK)
        {
          ms_log (2, "Error transferring data: %s\n", curl_easy_strerror (msg->data.result));
          io->urlfail = 1;
        }
      }

      /* Report an error if no data were received before the failure */
      if (io->urlfail && read == 0)
        return -1;
    }

#endif /* defined(LIBMSEED_URL) */
  }

  return (int64_t)read;
} /* End of msio_fread() */

/*********************************************************************
 * msio_feof:
 *
 * Test if end-of-stream.
 *
 * Returns 1 when stream is at end, 0 if not, and -1 on error.
 *********************************************************************/
int
msio_feof (LMIO *io)
{
  if (!io)
    return 0;

  if (io->handle == NULL || io->type == LMIO_NULL)
    return 0;

  if (io->type == LMIO_FILE || io->type == LMIO_FD)
  {
    if (feof ((FILE *)io->handle))
      return 1;
  }
  else if (io->type == LMIO_URL)
  {
#if !defined(LIBMSEED_URL)
    ms_log (2, "URL support not included in library\n");
    return -1;
#else
    /* A failed transfer is not a clean end of stream */
    if (io->urlfail)
      return 0;

    /* The still_running flag is only changed by curl_multi_perform()
     * and indicates current "transfers in progress".  Presumably no data
     * are in internal libcurl buffers either. */
    if (!io->still_running)
      return 1;
#endif
  }

  return 0;
} /* End of msio_feof() */

/*********************************************************************
 * msio_url_useragent:
 *
 * Set global User-Agent header for URL-based IO.
 *
 * The header is built as "PROGRAM/VERSION libmseed/version libcurl/version"
 * where VERSION is optional.
 *
 * Returns 0 on success non-zero otherwise.
 *
 * @ref MessageOnError - this function logs a message on error
 *********************************************************************/
int
msio_url_useragent (const char *program, const char *version)
{
  if (!program)
  {
    ms_log (2, "%s(): Required input not defined: 'program'\n", __func__);
    return -1;
  }

#if !defined(LIBMSEED_URL)
  (void)version; /* Unused */
  ms_log (2, "URL support not included in library\n");
  return -1;
#else
  char header[1024];

  /* Build User-Agent header and add internal versions */
  snprintf (header, sizeof (header),
            "User-Agent: %s%s%s libmseed/" LIBMSEED_VERSION " libcurl/" LIBCURL_VERSION, program,
            (version) ? "/" : "", (version) ? version : "");

  return msio_url_addheader (header);
#endif

  return 0;
} /* End of msio_url_useragent() */

/*********************************************************************
 * msio_url_timeout:
 *
 * Set global connection and stall timeouts, in seconds, for
 * URL-based IO.  A value of 0 disables the respective timeout and a
 * negative value leaves it unchanged.
 *
 * Returns 0 on success non-zero otherwise.
 *
 * @ref MessageOnError - this function logs a message on error
 *********************************************************************/
int
msio_url_timeout (long connecttimeout, long stalltimeout)
{
#if !defined(LIBMSEED_URL)
  (void)connecttimeout; /* Unused */
  (void)stalltimeout;   /* Unused */
  ms_log (2, "URL support not included in library\n");
  return -1;
#else
  if (connecttimeout >= 0)
    libmseed_url_connecttimeout = connecttimeout;

  if (stalltimeout >= 0)
    libmseed_url_stalltimeout = stalltimeout;
#endif

  return 0;
} /* End of msio_url_timeout() */

/*********************************************************************
 * msio_url_userpassword:
 *
 * Set global user-password credentials for URL-based IO.
 *
 * Returns 0 on success non-zero otherwise.
 *
 * @ref MessageOnError - this function logs a message on error
 *********************************************************************/
int
msio_url_userpassword (const char *userpassword)
{
  if (!userpassword)
  {
    ms_log (2, "%s(): Required input not defined: 'userpassword'\n", __func__);
    return -1;
  }

#if !defined(LIBMSEED_URL)
  ms_log (2, "URL support not included in library\n");
  return -1;
#else
  if (gCURLeasy == NULL && (gCURLeasy = curl_easy_init ()) == NULL)
    return -1;

  /* Allow any authentication, libcurl will pick the most secure */
  if (curl_easy_setopt (gCURLeasy, CURLOPT_HTTPAUTH, CURLAUTH_ANY) != CURLE_OK)
  {
    ms_log (2, "Cannot set CURLOPT_HTTPAUTH\n");
    return -1;
  }

  if (curl_easy_setopt (gCURLeasy, CURLOPT_USERPWD, userpassword) != CURLE_OK)
  {
    ms_log (2, "Cannot set CURLOPT_USERPWD\n");
    return -1;
  }
#endif

  return 0;
} /* End of msio_url_userpassword() */

/*********************************************************************
 * msio_url_addheader:
 *
 * Add header to global list for URL-based IO.
 *
 * Returns 0 on success non-zero otherwise.
 *
 * @ref MessageOnError - this function logs a message on error
 *********************************************************************/
int
msio_url_addheader (const char *header)
{
  if (!header)
  {
    ms_log (2, "%s(): Required input not defined: 'header'\n", __func__);
    return -1;
  }

#if !defined(LIBMSEED_URL)
  ms_log (2, "URL support not included in library\n");
  return -1;
#else
  struct curl_slist *slist = NULL;

  slist = curl_slist_append (gCURLheaders, header);

  if (slist == NULL)
  {
    ms_log (2, "Error adding header to list: %s\n", header);
    return -1;
  }

  gCURLheaders = slist;
#endif

  return 0;
} /* End of msio_url_addheader() */

/*********************************************************************
 * msio_url_freeheaders:
 *
 * Free the global list of headers for URL-based IO.
 *********************************************************************/
void
msio_url_freeheaders (void)
{
#if !defined(LIBMSEED_URL)
  ms_log (2, "URL support not included in library\n");
  return;
#else
  if (gCURLheaders != NULL)
  {
    curl_slist_free_all (gCURLheaders);
    gCURLheaders = NULL;
  }
#endif
} /* End of msio_url_freeheaders() */
