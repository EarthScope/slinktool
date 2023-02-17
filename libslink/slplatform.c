/***************************************************************************
 * slplatform.c:
 *
 * Platform portability routines.
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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "libslink.h"

/***************************************************************************
 * slp_sockstartup:
 *
 * Startup the network socket layer.  At the moment this is only meaningful
 * for the WIN platform.
 *
 * Returns -1 on errors and 0 on success.
 ***************************************************************************/
int
slp_sockstartup (void)
{
#if defined(SLP_WIN)
  WORD wVersionRequested;
  WSADATA wsaData;

  /* Check for Windows sockets version 2.2 */
  wVersionRequested = MAKEWORD (2, 2);

  if (WSAStartup (wVersionRequested, &wsaData))
    return -1;

#endif

  return 0;
} /* End of slp_sockstartup() */

/***************************************************************************
 * slp_sockconnect:
 *
 * Connect a network socket.
 *
 * Returns -1 on errors and 0 on success.
 ***************************************************************************/
int
slp_sockconnect (SOCKET sock, struct sockaddr *inetaddr, int addrlen)
{
#if defined(SLP_WIN)
  if ((connect (sock, inetaddr, addrlen)) == SOCKET_ERROR)
  {
    if (WSAGetLastError () != WSAEWOULDBLOCK)
      return -1;
  }
#else
  if ((connect (sock, inetaddr, addrlen)) == -1)
  {
    if (errno != EINPROGRESS)
      return -1;
  }
#endif

  return 0;
} /* End of slp_sockconnect() */

/***************************************************************************
 * slp_sockclose:
 *
 * Close a network socket.
 *
 * Returns -1 on errors and 0 on success.
 ***************************************************************************/
int
slp_sockclose (SOCKET sock)
{
#if defined(SLP_WIN)
  return closesocket (sock);
#else
  return close (sock);
#endif
} /* End of slp_sockclose() */

/***************************************************************************
 * slp_socknoblock:
 *
 * Set a network socket to non-blocking.
 *
 * Returns -1 on errors and 0 on success.
 ***************************************************************************/
int
slp_socknoblock (SOCKET sock)
{
#if defined(SLP_WIN)
  u_long flag = 1;

  if (ioctlsocket (sock, FIONBIO, &flag) == -1)
    return -1;

#else
  int flags = fcntl (sock, F_GETFL, 0);

  flags |= O_NONBLOCK;
  if (fcntl (sock, F_SETFL, flags) == -1)
    return -1;

#endif

  return 0;
} /* End of slp_socknoblock() */

/***************************************************************************
 * slp_noblockcheck:
 *
 * Return -1 on error and 0 on success (meaning no data for a non-blocking
 * socket).
 ***************************************************************************/
int
slp_noblockcheck (void)
{
#if defined(SLP_WIN)
  if (WSAGetLastError () != WSAEWOULDBLOCK)
    return -1;

#else
  if (errno != EWOULDBLOCK)
    return -1;

#endif

  /* no data available for NONBLOCKing IO */
  return 0;
} /* End of slp_noblockcheck() */

/***********************************************************************/ /**
 * @brief Set socket I/O timeout
 *
 * Set socket I/O timeout if such an option exists.  On WIN and
 * other platforms where SO_RCVTIMEO and SO_SNDTIMEO are defined this
 * sets the SO_RCVTIMEO and SO_SNDTIMEO socket options using
 * setsockopt() to the @a timeout value (specified in seconds).
 *
 * Solaris does not implelement socket-level timeout options.
 *
 * @param socket Network socket descriptor
 * @param timeout Alarm timeout in seconds
 *
 * @return -1 on error, 0 when not possible and 1 on success.
 ***************************************************************************/
int
slp_setsocktimeo (SOCKET socket, int timeout)
{
#if defined(SLP_WIN)
  int tval = timeout * 1000;

  if (setsockopt (socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tval, sizeof (tval)))
  {
    return -1;
  }
  tval = timeout * 1000;
  if (setsockopt (socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&tval, sizeof (tval)))
  {
    return -1;
  }

#else
/* Set socket I/O timeouts if socket options are defined */
#if defined(SO_RCVTIMEO) && defined(SO_SNDTIMEO)
  struct timeval tval;

  tval.tv_sec  = timeout;
  tval.tv_usec = 0;

  if (setsockopt (socket, SOL_SOCKET, SO_RCVTIMEO, &tval, sizeof (tval)))
  {
    return -1;
  }
  if (setsockopt (socket, SOL_SOCKET, SO_SNDTIMEO, &tval, sizeof (tval)))
  {
    return -1;
  }
#else
  return 0;
#endif

#endif

  return 1;
} /* End of slp_setsocktimeo() */

/***************************************************************************
 * slp_openfile:
 *
 * Open a specified file and return the file descriptor.  The perm
 * character is interpreted the following way:
 *
 * perm:
 *  'r', open file with read-only permissions
 *  'w', open file with read-write permissions, creating if necessary.
 *
 * Returns the return value of open(), generally this is a positive
 * file descriptor on success and -1 on error.
 ***************************************************************************/
int
slp_openfile (const char *filename, char perm)
{
#if defined(SLP_WIN)
  int flags = (perm == 'w') ? (_O_RDWR | _O_CREAT | _O_BINARY) : (_O_RDONLY | _O_BINARY);
  int mode  = (_S_IREAD | _S_IWRITE);
#else
  int flags   = (perm == 'w') ? (O_RDWR | O_CREAT) : O_RDONLY;
  mode_t mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif

  return open (filename, flags, mode);
} /* End of slp_openfile() */

/***************************************************************************
 * slp_strerror:
 *
 * Return a description of the last system error, in the case of Win32
 * this will be the last Windows Sockets error.
 ***************************************************************************/
const char *
slp_strerror (void)
{
#if defined(SLP_WIN)
  static char errorstr[100];

  snprintf (errorstr, sizeof (errorstr), "%d", WSAGetLastError ());
  return (const char *)errorstr;

#else
  return (const char *)strerror (errno);

#endif
} /* End of slp_strerror() */

/***************************************************************************
 * slp_dtime:
 *
 * Get the current time from the system as Unix/POSIX epoch time with double
 * precision.  On the WIN platform this function has millisecond
 * resulution, on *nix platforms this function has microsecond resolution.
 *
 * Return a double precision Unix/POSIX epoch time.
 ***************************************************************************/
double
slp_dtime (void)
{
#if defined(SLP_WIN)

  static const __int64 SECS_BETWEEN_EPOCHS = 11644473600;
  static const __int64 SECS_TO_100NS       = 10000000; /* 10^7 */

  __int64 UnixTime;
  SYSTEMTIME SystemTime;
  FILETIME FileTime;
  double depoch;

  GetSystemTime (&SystemTime);
  SystemTimeToFileTime (&SystemTime, &FileTime);

  /* Get the full win32 epoch value, in 100ns */
  UnixTime = ((__int64)FileTime.dwHighDateTime << 32) +
             FileTime.dwLowDateTime;

  /* Convert to the Unix epoch */
  UnixTime -= (SECS_BETWEEN_EPOCHS * SECS_TO_100NS);

  UnixTime /= SECS_TO_100NS; /* now convert to seconds */

  if ((double)UnixTime != UnixTime)
  {
    sl_log_r (NULL, 2, 0, "%s(): resulting value is too big for a double value\n", __func__);
  }

  depoch = (double)UnixTime + ((double)SystemTime.wMilliseconds / 1000.0);

  return depoch;

#else

  struct timeval tv;

  gettimeofday (&tv, (struct timezone *)0);
  return ((double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0));

#endif
} /* End of slp_dtime() */

/***************************************************************************
 * slp_usleep:
 *
 * Sleep for a given number of microseconds.  Under Win32 use SleepEx()
 * and for all others use the POSIX.4 nanosleep().
 ***************************************************************************/
void
slp_usleep (unsigned long int useconds)
{
#if defined(SLP_WIN)

  SleepEx ((useconds / 1000), 1);

#else

  struct timespec treq, trem;

  treq.tv_sec  = (time_t) (useconds / 1e6);
  treq.tv_nsec = (long)((useconds * 1e3) - (treq.tv_sec * 1e9));

  nanosleep (&treq, &trem);

#endif
} /* End of slp_usleep() */
