/***************************************************************************
 * test_slcd.c: ::SLCD lifecycle, configuration setters, and a handful of
 * still-open fable-review findings that are reachable through this API
 * without any network I/O.
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libslink.h"
#include "fixtures.h"
#include "slt.h"

/* Crash probes are dispatched by name through argv (see main()) and run
 * in a freshly exec'd copy of this binary, not a bare forked child, so
 * that a regression here reports a clean TAP failure for one test
 * instead of crashing this whole binary. exec() replaces the child's
 * process image before any of its own instrumented code runs, so
 * nothing the parent's allocator (sanitizer-instrumented or not) was
 * doing is ever inherited into it -- confirmed fork()+exec() from
 * within a running ASan/UBSan build works fine here, unlike a bare
 * fork() of a process that has already done real allocator work. */
static const char *g_argv0;

static void trigger_freeslcd_minimal (void);
static void trigger_host_boundary (void);
static void trigger_clientname_version_then_free (void);
static void trigger_request_info_null_slconn (void);
static void trigger_request_info_null_infostr (void);

static const FxProbe PROBES[] = {
    {"freeslcd_minimal", trigger_freeslcd_minimal},
    {"host_boundary", trigger_host_boundary},
    {"clientname_dangling", trigger_clientname_version_then_free},
    {"request_info_null_slconn", trigger_request_info_null_slconn},
    {"request_info_null_infostr", trigger_request_info_null_infostr},
};

static void
test_initslcd_defaults (void)
{
  SLCD *slconn = sl_initslcd ("testclient", "1.0");

  SLT_NOT_NULL (slconn, "sl_initslcd() returns a non-NULL connection description");
  SLT_EQ_INT (slconn->keepalive, 0, "default keepalive is disabled");
  SLT_EQ_INT (slconn->iotimeout, 60, "default I/O timeout is 60 seconds");
  SLT_EQ_INT (slconn->netto, 600, "default idle timeout is 600 seconds");
  SLT_EQ_INT (slconn->netdly, 30, "default reconnect delay is 30 seconds");
  SLT_EQ_INT (slconn->lastpkttime, 1, "last packet time usage defaults to enabled");
  SLT_EQ_INT (slconn->resume, 1, "resume defaults to enabled");
  SLT_EQ_INT (slconn->multistation, 0, "multistation defaults to disabled");
  SLT_NULL (slconn->streams, "a fresh connection has no streams");
  SLT_NOT_NULL (slconn->stat, "a fresh connection has an allocated state struct");
  SLT_EQ_STR (slconn->clientname, "testclient", "client name is stored");
  SLT_EQ_STR (slconn->clientversion, "1.0", "client version is stored");

  sl_freeslcd (slconn);
}

static void
trigger_freeslcd_minimal (void)
{
  SLCD *slconn = sl_initslcd (NULL, NULL);

  if (!slconn)
    _exit (1); /* sl_initslcd() must tolerate a NULL client name */

  sl_freeslcd (slconn); /* must not crash on an otherwise-empty SLCD */
  _exit (0);
}

static void
test_freeslcd_minimal (void)
{
  SLT_ASSERT (fx_probe_survives (g_argv0, "freeslcd_minimal"),
             "sl_initslcd() tolerates a NULL client name, and sl_freeslcd() "
             "does not crash on a minimally configured connection");
}

static void
test_serveraddress (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_serveraddress (slconn, "example.org"), 0, "bare host: accepted");
  SLT_EQ_STR (slconn->slhost, "example.org", "bare host: host stored");
  SLT_EQ_STR (slconn->slport, SL_DEFAULT_PORT, "bare host: default port applied");

  SLT_EQ_INT (sl_set_serveraddress (slconn, "example.org:19000"), 0, "host:port: accepted");
  SLT_EQ_STR (slconn->slhost, "example.org", "host:port: host stored");
  SLT_EQ_STR (slconn->slport, "19000", "host:port: port stored");

  SLT_EQ_INT (sl_set_serveraddress (slconn, ":19000"), 0, ":port: accepted");
  SLT_EQ_STR (slconn->slhost, SL_DEFAULT_HOST, ":port: default host applied");
  SLT_EQ_STR (slconn->slport, "19000", ":port: port stored");

  SLT_EQ_INT (sl_set_serveraddress (slconn, ":"), 0, "bare colon: accepted");
  SLT_EQ_STR (slconn->slhost, SL_DEFAULT_HOST, "bare colon: default host applied");
  SLT_EQ_STR (slconn->slport, SL_DEFAULT_PORT, "bare colon: default port applied");

  SLT_EQ_INT (sl_set_serveraddress (slconn, "example.org:"), 0, "trailing colon, no port: accepted");
  SLT_EQ_STR (slconn->slhost, "example.org", "trailing colon: host stored");
  SLT_EQ_STR (slconn->slport, SL_DEFAULT_PORT, "trailing colon: default port applied");

  /* An unbracketed IPv6 address is inherently ambiguous with the HOST:PORT
   * syntax (it contains colons itself); brackets are required for a
   * literal IPv6 host, as documented in sl_set_serveraddress()'s header. */
  SLT_EQ_INT (sl_set_serveraddress (slconn, "[::1]:19000"), 0, "bracketed IPv6 with port: accepted");
  SLT_EQ_STR (slconn->slhost, "::1", "bracketed IPv6: brackets stripped from host");
  SLT_EQ_STR (slconn->slport, "19000", "bracketed IPv6: port stored");

  SLT_EQ_INT (sl_set_serveraddress (slconn, "[::1]"), 0, "bracketed IPv6 without port: accepted");
  SLT_EQ_STR (slconn->slhost, "::1", "bracketed IPv6 without port: brackets stripped from host");
  SLT_EQ_STR (slconn->slport, SL_DEFAULT_PORT, "bracketed IPv6 without port: default port applied");

  /* Port 18500 is the recognized TLS port and must enable TLS automatically */
  sl_set_tlsmode (slconn, 0);
  SLT_EQ_INT (sl_set_serveraddress (slconn, "example.org:18500"), 0, "TLS port: accepted");
  SLT_EQ_INT (slconn->tls, 1, "port 18500 enables TLS automatically");

  SLT_EQ_INT (sl_set_serveraddress (slconn, "example.org:18000"), 0, "non-TLS port: accepted");
  SLT_EQ_INT (slconn->tls, 1, "tls is sticky once set; sl_set_tlsmode() is the only way to turn it back off");

  SLT_EQ_INT (sl_set_serveraddress (NULL, "example.org"), -1, "NULL connection is rejected");
  SLT_EQ_INT (sl_set_serveraddress (slconn, NULL), -1, "NULL address is rejected");

  sl_freeslcd (slconn);
}

/* Regression coverage: sl_set_serveraddress() once used
 * `minlen > sizeof(host)` instead of `>=` when copying the host portion
 * into a fixed-size stack buffer, so a host of exactly 300 characters
 * copied all 300 bytes with strncpy() and left no room for a null
 * terminator -- a stack-buffer-overflow under -fsanitize=address.
 * sl_set_serveraddress() no longer copies into a fixed buffer at all. */
static void
trigger_host_boundary (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  char addr[320];
  int ok;

  memset (addr, 'a', 300);
  strcpy (addr + 300, ":18000");

  sl_set_serveraddress (slconn, addr);

  ok = slconn->slhost != NULL &&
       strlen (slconn->slhost) == 300 &&
       strncmp (slconn->slhost, addr, 300) == 0;

  sl_freeslcd (slconn);
  _exit (ok ? 0 : 1);
}

static void
test_serveraddress_host_boundary (void)
{
  SLT_ASSERT (fx_probe_survives (g_argv0, "host_boundary"),
             "a 300-character host is stored intact and null-terminated, "
             "not overrun (a stack-buffer-overflow under ASan)");
}

static void
test_scalar_setters (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_keepalive (slconn, 30), 0, "sl_set_keepalive() accepted");
  SLT_EQ_INT (slconn->keepalive, 30, "keepalive value stored");

  SLT_EQ_INT (sl_set_iotimeout (slconn, 45), 0, "sl_set_iotimeout() accepted");
  SLT_EQ_INT (slconn->iotimeout, 45, "I/O timeout value stored");

  SLT_EQ_INT (sl_set_idletimeout (slconn, 120), 0, "sl_set_idletimeout() accepted");
  SLT_EQ_INT (slconn->netto, 120, "idle timeout value stored in netto");

  SLT_EQ_INT (sl_set_reconnectdelay (slconn, 5), 0, "sl_set_reconnectdelay() accepted");
  SLT_EQ_INT (slconn->netdly, 5, "reconnect delay value stored in netdly");

  SLT_EQ_INT (sl_set_blockingmode (slconn, 1), 0, "sl_set_blockingmode() accepted");
  SLT_EQ_INT (slconn->noblock, 1, "non-blocking flag set");
  sl_set_blockingmode (slconn, 0);
  SLT_EQ_INT (slconn->noblock, 0, "non-blocking flag cleared");
  sl_set_blockingmode (slconn, 42); /* any non-zero value normalizes to 1 */
  SLT_EQ_INT (slconn->noblock, 1, "non-blocking flag normalizes non-zero input to 1");

  SLT_EQ_INT (sl_set_dialupmode (slconn, 1), 0, "sl_set_dialupmode() accepted");
  SLT_EQ_INT (slconn->dialup, 1, "dial-up flag set");

  SLT_EQ_INT (sl_set_batchmode (slconn, 1), 0, "sl_set_batchmode() accepted");
  SLT_EQ_INT (slconn->batchmode, 1, "batch mode flag set");

  SLT_EQ_INT (sl_set_tlsmode (slconn, 1), 0, "sl_set_tlsmode() accepted");
  SLT_EQ_INT (slconn->tls, 1, "TLS flag set");
  sl_set_tlsmode (slconn, 0);
  SLT_EQ_INT (slconn->tls, 0, "TLS flag cleared");

  SLT_EQ_INT (sl_set_protocol (slconn, SLPROTO3X), 0, "sl_set_protocol(SLPROTO3X) accepted");
  SLT_EQ_INT (slconn->protocol, SLPROTO3X, "protocol stored as SLPROTO3X");
  SLT_EQ_INT (sl_set_protocol (slconn, SLPROTO40), 0, "sl_set_protocol(SLPROTO40) accepted");
  SLT_EQ_INT (slconn->protocol, SLPROTO40, "protocol stored as SLPROTO40");

  /* Every setter must reject a NULL connection rather than crash */
  SLT_EQ_INT (sl_set_keepalive (NULL, 1), -1, "sl_set_keepalive(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_iotimeout (NULL, 1), -1, "sl_set_iotimeout(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_idletimeout (NULL, 1), -1, "sl_set_idletimeout(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_reconnectdelay (NULL, 1), -1, "sl_set_reconnectdelay(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_blockingmode (NULL, 1), -1, "sl_set_blockingmode(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_dialupmode (NULL, 1), -1, "sl_set_dialupmode(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_batchmode (NULL, 1), -1, "sl_set_batchmode(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_tlsmode (NULL, 1), -1, "sl_set_tlsmode(NULL, ...) rejected");
  SLT_EQ_INT (sl_set_protocol (NULL, SLPROTO3X), -1, "sl_set_protocol(NULL, ...) rejected");

  sl_freeslcd (slconn);
}

static void
test_clientname (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_clientname (slconn, "myclient", "2.1"), 0, "name+version accepted");
  SLT_EQ_STR (slconn->clientname, "myclient", "client name updated");
  SLT_EQ_STR (slconn->clientversion, "2.1", "client version updated");

  SLT_EQ_INT (sl_set_clientname (slconn, "onlyname", NULL), 0, "name without version accepted");
  SLT_EQ_STR (slconn->clientname, "onlyname", "client name updated again");
  SLT_NULL (slconn->clientversion, "client version cleared when set without a version");

  SLT_EQ_INT (sl_set_clientname (NULL, "x", NULL), -1, "NULL connection rejected");
  SLT_EQ_INT (sl_set_clientname (slconn, NULL, NULL), -1, "NULL name rejected");

  sl_freeslcd (slconn);
}

/* Regression: sl_set_clientname() must not leave clientversion dangling
 * when re-called with a NULL version after a prior call set one; a
 * subsequent sl_freeslcd() would otherwise free that pointer twice. */

static void
trigger_clientname_version_then_free (void)
{
  SLCD *slconn = sl_initslcd ("t", "1.0"); /* sets a clientversion string */

  sl_set_clientname (slconn, "onlyname", NULL); /* must clear clientversion, not dangle it */
  sl_freeslcd (slconn);
}

static void
test_clientname_version_dangling_pointer (void)
{
  SLT_ASSERT (fx_probe_survives (g_argv0, "clientname_dangling"),
             "re-calling sl_set_clientname() without a version, then sl_freeslcd(), "
             "does not double-free clientversion");
}

static void
test_timewindow (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_set_timewindow (slconn, "2024-01-01T00:00:00", "2024-01-02T00:00:00"), 0,
             "start+end time window accepted");
  SLT_EQ_STR (slconn->start_time, "2024-01-01T00:00:00", "start time stored verbatim");
  SLT_EQ_STR (slconn->end_time, "2024-01-02T00:00:00", "end time stored verbatim");

  SLT_EQ_INT (sl_set_timewindow (slconn, "2024-06-01T00:00:00", NULL), 0, "start time only accepted");

  SLT_EQ_INT (sl_set_timewindow (NULL, "2024-01-01", NULL), -1, "NULL connection rejected");
  SLT_EQ_INT (sl_set_timewindow (slconn, NULL, NULL), -1, "both times NULL rejected");

  sl_freeslcd (slconn);
}

static void
test_terminate (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (slconn->terminate, 0, "terminate flag starts clear");
  sl_terminate (slconn);
  SLT_EQ_INT (slconn->terminate, 1, "sl_terminate() sets the terminate flag");

  sl_freeslcd (slconn);
}

static void
test_hascapability (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  SLT_EQ_INT (sl_hascapability (slconn, "CAP"), 0, "no capabilities configured yet: not supported");

  slconn->capabilities = strdup ("SLPROTO:3.1 CAP MULTISTATION");

  SLT_ASSERT (sl_hascapability (slconn, "CAP") > 0, "an exact capability flag is found");
  SLT_ASSERT (sl_hascapability (slconn, "MULTISTATION") > 0, "the last capability flag is found");
  SLT_ASSERT (sl_hascapability (slconn, "SLPROTO:3.1") > 0, "a capability flag containing a colon is found");
  SLT_EQ_INT (sl_hascapability (slconn, "MULTI"), 0, "a partial-match substring is not considered supported");
  SLT_EQ_INT (sl_hascapability (slconn, "NOTPRESENT"), 0, "an absent capability flag is not supported");

  SLT_EQ_INT (sl_hascapability (NULL, "CAP"), 0, "NULL connection returns not-supported rather than crashing");
  SLT_EQ_INT (sl_hascapability (slconn, NULL), 0, "NULL capability string returns not-supported rather than crashing");

  sl_freeslcd (slconn);
}

static char printslcd_capture[2048];

static void
printslcd_capture_line (const char *msg)
{
  strncat (printslcd_capture, msg, sizeof (printslcd_capture) - strlen (printslcd_capture) - 1);
}

static void
test_printslcd (void)
{
  SLCD *slconn = sl_initslcd ("t", "1.0");

  /* sl_printslcd() has no return value; route its log_r() output through
   * a per-connection callback and check it actually named the fields it
   * was given, instead of only confirming the call didn't crash. */
  printslcd_capture[0] = '\0';
  sl_loginit_r (slconn, 0, printslcd_capture_line, NULL, NULL, NULL);

  sl_set_serveraddress (slconn, "example.org:18000");
  sl_add_stream (slconn, "XX_TEST", "BHZ", SL_UNSETSEQUENCE, NULL);
  sl_printslcd (slconn);

  SLT_ASSERT (strstr (printslcd_capture, "example.org") != NULL,
             "sl_printslcd() output names the configured server address");
  SLT_ASSERT (strstr (printslcd_capture, "XX_TEST") != NULL,
             "sl_printslcd() output lists the added stream's station ID");
  SLT_ASSERT (strstr (printslcd_capture, "1.0") != NULL,
             "sl_printslcd() output names the client version");

  sl_freeslcd (slconn);
}

/* sl_request_info() guards against a NULL slconn and a NULL infostr,
 * returning -1 rather than dereferencing either.
 *
 * Each probe below builds whatever SLCD it needs itself: since survives()
 * runs probes in a fork+exec'd copy of this binary (see above), a
 * probe can no longer rely on state a parent-process test left in a
 * global -- the exec'd child starts with fresh, zero-initialized globals.
 * Each probe exits non-zero if sl_request_info() returns anything but -1,
 * so survives() also catches a guard that swallows the error silently. */

static void
trigger_request_info_null_slconn (void)
{
  _exit (sl_request_info (NULL, "STREAMS") == -1 ? 0 : 1);
}

static void
trigger_request_info_null_infostr (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);
  _exit (sl_request_info (slconn, NULL) == -1 ? 0 : 1);
}

static void
test_request_info_null_guards (void)
{
  SLT_ASSERT (fx_probe_survives (g_argv0, "request_info_null_slconn"),
             "sl_request_info(NULL, ...) returns an error instead of crashing");
  SLT_ASSERT (fx_probe_survives (g_argv0, "request_info_null_infostr"),
             "sl_request_info(slconn, NULL) returns an error instead of crashing");
}

/* Exercises sl_set_auth_envvars(), including that the constructed auth_data
 * string is released by a subsequent authentication call and by
 * sl_freeslcd(); leak-freedom itself is only observable under a
 * leak-detecting sanitizer (see tests/README.md). */
static void
test_auth_envvars (void)
{
  SLCD *slconn = sl_initslcd ("t", NULL);

  setenv ("SLTEST_USER", "alice", 1);
  setenv ("SLTEST_PASS", "s3cr3t", 1);

  SLT_EQ_INT (sl_set_auth_envvars (slconn, "SLTEST_USER", "SLTEST_PASS"), 0,
             "sl_set_auth_envvars() succeeds when both variables are set");
  SLT_NOT_NULL (slconn->auth_value, "auth_value callback installed");
  SLT_NULL (slconn->auth_finish, "auth_finish callback is left unset");
  SLT_EQ_STR ((const char *)slconn->auth_data, "USERPASS alice s3cr3t", "auth_data holds the constructed USERPASS value");

  unsetenv ("SLTEST_PASS");
  SLT_EQ_INT (sl_set_auth_envvars (slconn, "SLTEST_USER", "SLTEST_PASS"), -1,
             "sl_set_auth_envvars() fails when a variable is missing");

  setenv ("SLTEST_PASS", "n3wpass", 1);
  SLT_EQ_INT (sl_set_auth_envvars (slconn, "SLTEST_USER", "SLTEST_PASS"), 0,
             "sl_set_auth_envvars() succeeds again after resetting the missing variable");
  SLT_EQ_STR ((const char *)slconn->auth_data, "USERPASS alice n3wpass",
             "auth_data holds the newly constructed value, replacing (and freeing) the prior one");

  SLT_EQ_INT (sl_set_auth_params (slconn, NULL, NULL, NULL), 0,
             "sl_set_auth_params() clears the authentication parameters");
  SLT_NULL (slconn->auth_data, "auth_data is released, not left dangling, when parameters are cleared");

  unsetenv ("SLTEST_USER");
  unsetenv ("SLTEST_PASS");
  sl_freeslcd (slconn);
}

int
main (int argc, char **argv)
{
  g_argv0 = argv[0];
  fx_dispatch_probe (argc, argv, PROBES, sizeof (PROBES) / sizeof (PROBES[0])); /* exits directly if this is a probe re-exec */

  SLT_RUN (test_initslcd_defaults);
  SLT_RUN (test_freeslcd_minimal);
  SLT_RUN (test_serveraddress);
  SLT_RUN (test_serveraddress_host_boundary);
  SLT_RUN (test_scalar_setters);
  SLT_RUN (test_clientname);
  SLT_RUN (test_clientname_version_dangling_pointer);
  SLT_RUN (test_timewindow);
  SLT_RUN (test_terminate);
  SLT_RUN (test_hascapability);
  SLT_RUN (test_printslcd);
  SLT_RUN (test_request_info_null_guards);
  SLT_RUN (test_auth_envvars);

  return SLT_REPORT ();
}
