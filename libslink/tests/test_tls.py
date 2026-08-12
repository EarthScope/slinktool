#!/usr/bin/env python3
"""TLS coverage for libslink's mbedTLS glue in network.c.

Requires the third-party `trustme` package to mint a throwaway CA and
leaf certificate; the whole module is skipped with a clear reason when
it isn't installed, so `make test` still runs everything else on a
stdlib-only machine.

    python3 -m pip install trustme
"""

import os
import signal
import ssl
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))

try:
    import trustme

    HAVE_TRUSTME = True
except ImportError:
    HAVE_TRUSTME = False

from slmock import mseed
from slmock.server import ConnectionClosed, MockServer, serve_hello, serve_precommands, serve_v4
from test_protocol import CRASH_SIGNALS, HARNESS, parse_output


class TLSMockServer(MockServer):
    """A MockServer whose accepted sockets are wrapped in a server-side
    TLS context signed by the given trustme leaf certificate."""

    def __init__(self, handler, leaf_cert, host="127.0.0.1"):
        self._ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        leaf_cert.configure_cert(self._ssl_ctx)
        super().__init__(handler, host=host)

    def _wrap(self, conn):
        try:
            return self._ssl_ctx.wrap_socket(conn, server_side=True)
        except ssl.SSLError:
            # A client that rejects our certificate answers the
            # handshake with a fatal alert rather than a bare socket
            # close -- refusal, not a server error.
            raise ConnectionClosed()


@unittest.skipUnless(HAVE_TRUSTME, "trustme not installed (pip install trustme); TLS tests skipped")
class TestTLS(unittest.TestCase):
    def setUp(self):
        self.ca = trustme.CA()
        self.leaf = self.ca.issue_cert("localhost", "127.0.0.1")
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)
        self.ca_file = os.path.join(self.tmpdir.name, "ca.pem")
        self.ca.cert_pem.write_to_path(self.ca_file)

    def run_tls_scenario(self, handler, args, leaf=None, timeout=15, expect_timeout=False):
        server = TLSMockServer(handler, leaf or self.leaf).start()
        self.addCleanup(server.stop)

        full_args = [HARNESS, "--address", server.address(), "--tls"] + args

        try:
            proc = subprocess.run(full_args, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired as e:
            if expect_timeout:
                # subprocess.run() has already killed the child on this
                # path, so its socket is closed and the handler thread's
                # next read/write should unblock -- join it before
                # trusting server.errors (see test_protocol.py's
                # run_scenario() for the same reasoning).
                server.stop()
                if server.errors:
                    self.fail("mock server handler raised: %r" % (server.errors,))
                stdout = e.stdout or ""
                if isinstance(stdout, bytes):
                    stdout = stdout.decode("utf-8", "replace")
                stderr = e.stderr or ""
                if isinstance(stderr, bytes):
                    stderr = stderr.decode("utf-8", "replace")
                events = parse_output(stdout)
                # subprocess.run() doesn't expose the killed process's real
                # exit status through TimeoutExpired; None (rather than a
                # missing key) lets a future assertion on events["returncode"]
                # fail cleanly instead of raising KeyError.
                events["returncode"] = None
                events["stderr"] = stderr
                return events, server
            self.fail("slharness did not exit within %s seconds; stdout so far:\n%s" % (e.timeout, e.stdout))

        if expect_timeout:
            self.fail("expected slharness to hang retrying, but it exited with %r" % (proc.returncode,))

        server.stop()

        if server.errors:
            self.fail("mock server handler raised: %r" % (server.errors,))

        events = parse_output(proc.stdout)
        events["returncode"] = proc.returncode
        events["stderr"] = proc.stderr
        return events, server

    def test_v4_negotiation_over_tls(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events, _ = self.run_tls_scenario(
            handler,
            [
                "--v4",
                "--ca-file",
                self.ca_file,
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertTrue(
            any("TLS connection established" in line for line in events["log"]), events["log"]
        )

    def test_ca_cert_path_directory_form(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        # LIBSLINK_CA_CERT_PATH points mbedtls_x509_crt_parse_path() at a
        # directory of PEM files rather than a single file.
        capath = os.path.join(self.tmpdir.name, "ca_dir")
        os.mkdir(capath)
        self.ca.cert_pem.write_to_path(os.path.join(capath, "ca.pem"))

        events, _ = self.run_tls_scenario(
            handler,
            [
                "--v4",
                "--ca-path",
                capath,
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)

    def test_untrusted_certificate_is_rejected(self):
        other_ca = trustme.CA()
        untrusted_leaf = other_ca.issue_cert("localhost", "127.0.0.1")

        def handler(conn, reader, server, idx):
            # Never reached: the handshake itself must fail.
            pass

        events, _ = self.run_tls_scenario(
            handler,
            [
                "--v4",
                "--ca-file",
                self.ca_file,  # trusted CA does NOT match the server's cert
                "--station",
                "XX_TEST:BHZ",
                "--reconnectdelay",
                "3",
                "--timeout-seconds",
                "4",
            ],
            leaf=untrusted_leaf,
            timeout=12,
            expect_timeout=True,
        )

        self.assertTrue(
            any("certificate verification failed" in line.lower() for line in events["log"]),
            events["log"],
        )
        self.assertEqual(events["packets"], [])

    def test_unsupported_tls_cert_env_var_name_is_not_honored(self):
        """LIBSLINK_CA_CERT_FILE/_PATH are the only supported CA env var
        names (see load_ca_certs()); LIBSLINK_TLS_CERT_FILE is not read, so
        it must not be treated as a substitute."""

        def handler(conn, reader, server, idx):
            # Never reached: no CA is loaded, so the handshake must fail.
            pass

        env_backup = dict(os.environ)
        os.environ.pop("LIBSLINK_CA_CERT_FILE", None)
        os.environ.pop("LIBSLINK_CA_CERT_PATH", None)
        os.environ["LIBSLINK_TLS_CERT_FILE"] = self.ca_file
        self.addCleanup(lambda: os.environ.clear() or os.environ.update(env_backup))

        events, _ = self.run_tls_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ",
                "--reconnectdelay",
                "3",
                "--timeout-seconds",
                "4",
            ],
            timeout=12,
            expect_timeout=True,
        )

        self.assertTrue(
            any("certificate verification failed" in line.lower() for line in events["log"]),
            events["log"],
        )
        self.assertEqual(events["packets"], [])

    def test_handshake_silent_peer_does_not_hang_or_crash(self):
        """Regression coverage for core-review-pass3 finding 10: the TLS
        handshake loop must be bounded by a deadline and honor
        termination, rather than spin forever (previously at 100% CPU,
        polling for writability on a socket that's essentially always
        writable) against a peer that completes the TCP connection and
        then never sends a byte. Uses a plain (non-TLS-wrapping)
        MockServer directly -- the point is that the *server* never
        engages in a TLS handshake at all, so trustme's certificates are
        irrelevant here. Every reconnect attempt hits the same silent
        peer, so (like a rejected-forever negotiation) sl_collect()
        never returns on its own by design -- observe slharness for a
        few seconds, past several handshake-timeout-and-retry cycles,
        then terminate it and check it wasn't a real crash rather than
        our own signal."""

        def handler(conn, reader, server, idx):
            time.sleep(10)

        server = MockServer(handler).start()
        self.addCleanup(server.stop)

        full_args = [
            HARNESS,
            "--address",
            server.address(),
            "--tls",
            "--iotimeout",
            "2",
            "--reconnectdelay",
            "1",
            "--v4",
            "--station",
            "XX_TEST:BHZ",
        ]
        proc = subprocess.Popen(full_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        try:
            stdout, stderr = proc.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                stdout, stderr = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                stdout, stderr = proc.communicate()

        server.stop()
        if server.errors:
            self.fail("mock server handler raised: %r" % (server.errors,))

        if proc.returncode is not None and proc.returncode < 0 and -proc.returncode in CRASH_SIGNALS:
            self.fail(
                "process was killed by %s; stderr:\n%s"
                % (signal.Signals(-proc.returncode).name, stderr)
            )

        self.assertIn(
            "TLS handshake timed out",
            stdout,
            "handshake deadline never fired; stdout:\n%s" % stdout,
        )


if __name__ == "__main__":
    unittest.main()
