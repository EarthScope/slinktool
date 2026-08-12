/***************************************************************************
 * test_netprims.c: sl_connect()/sl_senddata()/sl_recvdata()/sl_recvresp()/
 * sl_poll()/sl_disconnect() against a bare, hand-rolled loopback listener
 * -- no HELLO, no protocol negotiation.  All of these are public
 * functions, so this drives them directly rather than through
 * sl_collect().
 ***************************************************************************/

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libslink.h"
#include "fixtures.h"
#include "slt.h"

/* Bind and listen on 127.0.0.1 with an OS-assigned port. Returns the
 * listening fd and, via *port, the port number chosen.  If rcvbuf is
 * non-zero, SO_RCVBUF is set on the listening socket (and so inherited
 * by the accepted socket) before listen(). */
static int
start_listener_with_rcvbuf (int *port, int rcvbuf)
{
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof (addr);
  int fd = socket (AF_INET, SOCK_STREAM, 0);

  if (fd < 0)
    return -1;

  if (rcvbuf > 0 &&
      setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof (rcvbuf)) < 0)
  {
    close (fd);
    return -1;
  }

  memset (&addr, 0, sizeof (addr));
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
  addr.sin_port        = 0;

  if (bind (fd, (struct sockaddr *)&addr, sizeof (addr)) < 0)
  {
    close (fd);
    return -1;
  }

  if (listen (fd, 1) < 0)
  {
    close (fd);
    return -1;
  }

  if (getsockname (fd, (struct sockaddr *)&addr, &addrlen) < 0)
  {
    close (fd);
    return -1;
  }

  *port = ntohs (addr.sin_port);

  return fd;
}

static int
start_listener (int *port)
{
  return start_listener_with_rcvbuf (port, 0);
}

static SLCD *
connect_to (int listenfd, int port, int *out_serverfd)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char address[64];
  SOCKET rv;

  snprintf (address, sizeof (address), "127.0.0.1:%d", port);
  sl_set_serveraddress (slconn, address);

  rv = sl_connect (slconn, 0 /* no HELLO */);

  if (rv < 0)
  {
    sl_freeslcd (slconn);
    return NULL;
  }

  *out_serverfd = accept (listenfd, NULL, NULL);

  return slconn;
}

static void
test_connect_and_disconnect (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created");

  slconn = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "sl_connect() succeeds against a live loopback listener");
  SLT_ASSERT (serverfd >= 0, "the listener accepted the connection");
  SLT_ASSERT (slconn->link >= 0, "the SLCD link descriptor is set after connecting");

  SLT_EQ_INT (sl_disconnect (slconn), -1, "sl_disconnect() returns -1 (historical convention)");
  SLT_EQ_INT (slconn->link, -1, "sl_disconnect() resets the link descriptor");

  SLT_EQ_INT (sl_disconnect (slconn), -1, "a second sl_disconnect() call is idempotent, not a crash");

  close (serverfd);
  close (listenfd);
  sl_freeslcd (slconn);
}

static void
test_senddata (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;
  char buf[16] = {0};
  ssize_t n;

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for sl_senddata() test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for sl_senddata() test");

  SLT_EQ_INT (sl_senddata (slconn, (void *)"PING\r\n", 6, "id", NULL, 0), 0,
             "sl_senddata() without a response request returns 0");

  n = recv (serverfd, buf, sizeof (buf), 0);
  SLT_EQ_INT ((int)n, 6, "the server side received the bytes sl_senddata() sent");
  SLT_EQ_INT (memcmp (buf, "PING\r\n", 6), 0, "the received bytes match exactly");

  close (serverfd);
  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

/* Force sl_senddata() to face a short send()/mbedtls_ssl_write() by
 * shrinking both sides' kernel socket buffers, then pushing a buffer
 * far larger than either buffer holds.  A single, unlooped write call
 * only transfers what fits and would leave the rest of the buffer
 * unsent; sl_senddata() must loop until every byte is written. */
static void
test_senddata_partial_write (void)
{
  static const size_t len = 1024 * 1024;
  int port, listenfd, serverfd = -1;
  int smallbuf = 4096;
  int pipefd[2];
  pid_t child;
  SLCD *slconn;
  char *sendbuf;

  if (pipe (pipefd) != 0)
  {
    SLT_FAIL ("pipe created for the child's byte count", "pipe() failed");
    return;
  }
  SLT_PASS ("pipe created for the child's byte count");

  listenfd = start_listener_with_rcvbuf (&port, smallbuf);
  if (listenfd < 0)
  {
    SLT_FAIL ("listener created with a small SO_RCVBUF", "start_listener_with_rcvbuf() failed");
    close (pipefd[0]);
    close (pipefd[1]);
    return;
  }
  SLT_PASS ("listener created with a small SO_RCVBUF");

  slconn = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for the partial-write regression test");

  SLT_ASSERT (setsockopt (slconn->link, SOL_SOCKET, SO_SNDBUF, &smallbuf,
                          sizeof (smallbuf)) == 0,
             "client socket SO_SNDBUF shrunk");

  sendbuf = malloc (len + 1);
  SLT_NOT_NULL (sendbuf, "send buffer allocated");
  memset (sendbuf, 'x', len);
  sendbuf[len] = '\0';

  child = fork ();
  if (child < 0)
  {
    SLT_FAIL ("fork() for the draining reader succeeded", "fork() failed");
    close (pipefd[0]);
    close (pipefd[1]);
    close (serverfd);
    close (listenfd);
    sl_disconnect (slconn);
    sl_freeslcd (slconn);
    return;
  }
  SLT_PASS ("fork() for the draining reader succeeded");

  if (child == 0)
  {
    /* Child: drain the server side to EOF, report the total count. */
    size_t total = 0;
    char rdbuf[65536];
    ssize_t n;

    close (pipefd[0]);
    close (listenfd);
    sl_disconnect (slconn);

    /* Let the client's sends fill both socket buffers before draining. */
    usleep (200000);

    while ((n = recv (serverfd, rdbuf, sizeof (rdbuf), 0)) > 0)
    {
      total += (size_t)n;
    }

    close (serverfd);
    ssize_t written = write (pipefd[1], &total, sizeof (total));
    (void)written;
    close (pipefd[1]);
    _exit (0);
  }

  /* Parent: send the full buffer, then close so the child's recv() sees EOF. */
  close (pipefd[1]);
  close (serverfd);

  SLT_EQ_INT (sl_senddata (slconn, sendbuf, len, "id", NULL, 0), 0,
             "sl_senddata() reports success sending a buffer larger than both socket buffers");

  sl_disconnect (slconn);
  close (listenfd);

  {
    size_t childtotal = 0;
    ssize_t n = read (pipefd[0], &childtotal, sizeof (childtotal));

    close (pipefd[0]);
    waitpid (child, NULL, 0);

    SLT_EQ_INT ((int)n, (int)sizeof (childtotal), "read the child's reported byte count");
    SLT_ASSERT (childtotal == len,
               "the peer received every byte sl_senddata() claimed to send");
  }

  free (sendbuf);
  sl_freeslcd (slconn);
}

static void
test_recvdata (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;
  char buf[16] = {0};
  int64_t n;

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for sl_recvdata() test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for sl_recvdata() test");

  /* Nothing has been sent yet: a non-blocking read reports 0, not an error. */
  n = sl_recvdata (slconn, buf, sizeof (buf), "id");
  SLT_EQ_INT ((int)n, 0, "sl_recvdata() returns 0 when no data is available (non-blocking)");

  send (serverfd, "PONG", 4, 0);
  SLT_ASSERT (sl_poll (slconn, 1, 0, 2000) > 0, "sl_poll() reports readability once data arrives");

  n = sl_recvdata (slconn, buf, sizeof (buf), "id");
  SLT_EQ_INT ((int)n, 4, "sl_recvdata() returns the number of bytes available");
  SLT_EQ_INT (memcmp (buf, "PONG", 4), 0, "sl_recvdata() delivers the correct bytes");

  close (serverfd);
  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

static void
test_recvdata_on_closed_connection (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;
  char buf[16];

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for the closed-connection test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for the closed-connection test");

  close (serverfd); /* server hangs up */

  /* Give the FIN a moment to arrive before reading. */
  sl_usleep (50000);

  SLT_EQ_INT ((int)sl_recvdata (slconn, buf, sizeof (buf), "id"), -1,
             "sl_recvdata() reports -1 once the peer has closed the connection");

  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

static void
test_recvresp (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;
  char resp[64];

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for sl_recvresp() test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for sl_recvresp() test");

  send (serverfd, "OK\r\n", 4, 0);

  SLT_EQ_INT (sl_recvresp (slconn, resp, sizeof (resp), "CMD\r\n", "id"), 4,
             "sl_recvresp() returns the number of bytes up to and including the CRLF terminator");
  SLT_EQ_INT (memcmp (resp, "OK\r\n", 4), 0, "sl_recvresp() captures the exact response bytes");

  close (serverfd);
  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

static void
test_recvresp_split_across_reads (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;
  char resp[64];

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for the split-response test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for the split-response test");

  /* Send the response in two pieces; sl_recvresp() reads one byte at a
   * time internally and must still assemble it correctly. */
  send (serverfd, "O", 1, 0);
  sl_usleep (20000);
  send (serverfd, "K\r\n", 3, 0);

  SLT_EQ_INT (sl_recvresp (slconn, resp, sizeof (resp), "CMD\r\n", "id"), 4,
             "sl_recvresp() assembles a response delivered across multiple TCP segments");
  SLT_EQ_INT (memcmp (resp, "OK\r\n", 4), 0, "the assembled response is correct");

  close (serverfd);
  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

static void
test_poll_timeout (void)
{
  int port, listenfd, serverfd = -1;
  SLCD *slconn;

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for sl_poll() timeout test");
  slconn   = connect_to (listenfd, port, &serverfd);
  SLT_NOT_NULL (slconn, "connected for sl_poll() timeout test");

  SLT_EQ_INT (sl_poll (slconn, 1, 0, 200), 0, "sl_poll() returns 0 when the timeout expires with no activity");

  close (serverfd);
  close (listenfd);
  sl_disconnect (slconn);
  sl_freeslcd (slconn);
}

static void
test_connect_refused (void)
{
  int port, probefd;
  SLCD *slconn;

  /* Reserve then immediately release a port so nothing is listening on it. */
  probefd = start_listener (&port);
  if (probefd < 0)
  {
    SLT_FAIL ("test listener created to reserve a port", "start_listener() failed");
    return;
  }
  SLT_PASS ("test listener created to reserve a port");
  close (probefd);

  slconn = sl_initslcd ("t", NULL);
  {
    char address[64];
    snprintf (address, sizeof (address), "127.0.0.1:%d", port);
    sl_set_serveraddress (slconn, address);
  }

  SLT_ASSERT (sl_connect (slconn, 0) < 0, "sl_connect() fails against a port nothing is listening on");

  sl_freeslcd (slconn);
}

/* Every failed sl_ping() call must release the socket it opened; before
 * the fix this leaked slconn->link (and any TLS context) on both
 * response-read failure paths inside sl_ping(). A forked peer accepts
 * the connection and closes it immediately, without ever responding to
 * HELLO, so both of sl_ping()'s sl_recvresp() calls fail promptly. */
static void
test_ping_disconnects_after_failed_hello_response (void)
{
  int port, listenfd;
  pid_t child;
  SLCD *slconn;
  char address[64];
  int rv;

  listenfd = start_listener (&port);
  SLT_ASSERT (listenfd >= 0, "test listener created for the ping-leak test");

  child = fork ();
  if (child < 0)
  {
    SLT_FAIL ("fork() for the accept-then-close peer succeeded", "fork() failed");
    close (listenfd);
    return;
  }
  SLT_PASS ("fork() for the accept-then-close peer succeeded");

  if (child == 0)
  {
    int fd = accept (listenfd, NULL, NULL);

    if (fd >= 0)
      close (fd);

    close (listenfd);
    _exit (0);
  }

  slconn = sl_initslcd ("t", NULL);
  snprintf (address, sizeof (address), "127.0.0.1:%d", port);
  sl_set_serveraddress (slconn, address);

  rv = sl_ping (slconn, NULL, NULL);

  SLT_EQ_INT (rv, -1, "sl_ping() reports failure when the peer closes before responding to HELLO");
  SLT_EQ_INT (slconn->link, -1, "the socket sl_ping() opened was released, not leaked");

  waitpid (child, NULL, 0);
  close (listenfd);
  sl_freeslcd (slconn);
}

/* NULL-guard crash probes, dispatched by name through argv and run in a
 * fresh fork+exec'd copy of this binary (see test_slcd.c for why exec(),
 * not a bare fork(), is used here), so a regression reports a clean TAP
 * failure for one test instead of crashing this whole binary. */
static const char *g_argv0;

static void trigger_disconnect_null (void);
static void trigger_configlink_null (void);
static void trigger_senddata_null (void);
static void trigger_recvdata_null (void);
static void trigger_recvresp_null (void);

static const FxProbe PROBES[] = {
    {"disconnect_null", trigger_disconnect_null},
    {"configlink_null", trigger_configlink_null},
    {"senddata_null", trigger_senddata_null},
    {"recvdata_null", trigger_recvdata_null},
    {"recvresp_null", trigger_recvresp_null},
};

static void
trigger_disconnect_null (void)
{
  sl_disconnect (NULL);
}

static void
trigger_configlink_null (void)
{
  sl_configlink (NULL);
}

static void
trigger_senddata_null (void)
{
  sl_senddata (NULL, (void *)"x", 1, "id", NULL, 0);
}

static void
trigger_recvdata_null (void)
{
  char buf[4];
  sl_recvdata (NULL, buf, sizeof (buf), "id");
}

static void
trigger_recvresp_null (void)
{
  char buf[4];
  sl_recvresp (NULL, buf, sizeof (buf), "CMD\r\n", "id");
}

static void
test_null_guards_do_not_crash (void)
{
  SLT_ASSERT (fx_probe_survives (g_argv0, "disconnect_null"), "sl_disconnect(NULL) does not crash");
  SLT_ASSERT (fx_probe_survives (g_argv0, "configlink_null"), "sl_configlink(NULL) does not crash");
  SLT_ASSERT (fx_probe_survives (g_argv0, "senddata_null"), "sl_senddata(NULL, ...) does not crash");
  SLT_ASSERT (fx_probe_survives (g_argv0, "recvdata_null"), "sl_recvdata(NULL, ...) does not crash");
  SLT_ASSERT (fx_probe_survives (g_argv0, "recvresp_null"), "sl_recvresp(NULL, ...) does not crash");
}

int
main (int argc, char **argv)
{
  g_argv0 = argv[0];
  fx_dispatch_probe (argc, argv, PROBES, sizeof (PROBES) / sizeof (PROBES[0])); /* exits directly if this is a probe re-exec */

  SLT_RUN (test_connect_and_disconnect);
  SLT_RUN (test_senddata);
  SLT_RUN (test_senddata_partial_write);
  SLT_RUN (test_recvdata);
  SLT_RUN (test_recvdata_on_closed_connection);
  SLT_RUN (test_recvresp);
  SLT_RUN (test_recvresp_split_across_reads);
  SLT_RUN (test_poll_timeout);
  SLT_RUN (test_connect_refused);
  SLT_RUN (test_ping_disconnects_after_failed_hello_response);
  SLT_RUN (test_null_guards_do_not_crash);

  return SLT_REPORT ();
}
