#!/usr/bin/env python3
"""Protocol-level tests: drive the real slharness client against the
slmock mock server over loopback TCP, covering both SeedLink v3 and v4.
"""

import base64
import os
import queue
import re
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from slmock import mseed
from slmock.server import (
    ConnectionClosed,
    MockServer,
    serve_hello,
    serve_precommands,
    serve_v3_multi,
    serve_v3_uni,
    serve_v4,
)

HARNESS = os.path.join(os.path.dirname(__file__), "slharness")
if sys.platform == "win32":
    HARNESS += ".exe"

# Built from names rather than a literal tuple since signal.SIGBUS doesn't
# exist on Windows.
CRASH_SIGNALS = tuple(
    getattr(signal, name)
    for name in ("SIGSEGV", "SIGABRT", "SIGILL", "SIGFPE", "SIGBUS")
    if hasattr(signal, name)
)

PACKET_RE = re.compile(
    # station=(\S*), not \S+: v4 INFO/JSON packets (and all v3 packets,
    # whose 8-byte header carries no station id at all) print an empty
    # station field.
    r'PACKET seq=(\d+) format=(.) subformat=(.) station=(\S*) length=(\d+) summary="(.*)"$'
)


PAYLOAD_RE = re.compile(r"PAYLOAD seq=(\d+) b64=(\S*)$")


def parse_output(stdout):
    events = {"log": [], "packets": [], "caps": {}, "ping": None, "result": None}
    for line in stdout.splitlines():
        if line.startswith("LOG "):
            events["log"].append(line[len("LOG ") :])
        elif line.startswith("PACKET "):
            m = PACKET_RE.match(line)
            if not m:
                raise AssertionError("PACKET line did not match PACKET_RE: %r" % line)
            events["packets"].append(
                {
                    "seq": int(m.group(1)),
                    "format": m.group(2),
                    "subformat": m.group(3),
                    "station": m.group(4),
                    "length": int(m.group(5)),
                    "summary": m.group(6),
                }
            )
        elif line.startswith("PAYLOAD "):
            m = PAYLOAD_RE.match(line)
            if not m:
                raise AssertionError("PAYLOAD line did not match PAYLOAD_RE: %r" % line)
            if events["packets"]:
                # Raw payload bytes for whichever packet was just parsed
                # above (slharness emits PAYLOAD immediately after its
                # PACKET line, same seq) -- lets a test inspect
                # non-miniSEED content (JSON/XML/opaque) without fighting
                # the PACKET line's quoted "summary=" field.
                events["packets"][-1]["payload"] = base64.b64decode(m.group(2))
        elif line.startswith("CAP "):
            k, _, v = line[len("CAP ") :].partition("=")
            events["caps"][k] = v == "1"
        elif line.startswith("PING "):
            events["ping"] = line
        elif line.startswith("RESULT "):
            events["result"] = line[len("RESULT ") :]
    return events


class ProtocolTestCase(unittest.TestCase):
    """Base class: starts a MockServer with a per-test handler, runs
    slharness against it with a hard subprocess timeout, and always
    stops the server afterward."""

    def run_scenario(self, handler, args, timeout=20, subprocess_timeout=None):
        server = MockServer(handler).start()
        self.addCleanup(server.stop)

        full_args = [HARNESS, "--address", server.address()] + args

        try:
            proc = subprocess.run(
                full_args,
                capture_output=True,
                text=True,
                timeout=subprocess_timeout or (timeout + 10),
            )
        except subprocess.TimeoutExpired as e:
            self.fail(
                "slharness did not exit within %s seconds; stdout so far:\n%s"
                % (e.timeout, e.stdout)
            )

        # The client has already exited, so its socket is closed and the
        # handler thread's next read/write should unblock almost
        # immediately -- but it may not have gotten there yet. stop()
        # joins that thread before we look at server.errors, so an
        # assertion failure (or any other exception) raised late in the
        # handler is never missed by checking errors before the thread
        # that fills it in has actually finished.
        server.stop()

        if server.errors:
            self.fail("mock server handler raised: %r" % (server.errors,))

        events = parse_output(proc.stdout)
        events["returncode"] = proc.returncode
        events["stderr"] = proc.stderr

        # slharness's normal exit path always returns 0 -- SLcollect()'s
        # actual result is only ever reported via the printed "RESULT"
        # line, never through the exit status -- so anything else here
        # means it crashed (a negative value is the killing signal) or
        # died some other abnormal way, after already having printed and
        # flushed whatever packets/log lines this scenario asserts on.
        self.assertEqual(
            proc.returncode,
            0,
            "slharness exited abnormally (code %r); stderr:\n%s"
            % (proc.returncode, proc.stderr),
        )

        return events

    def run_scenario_bounded(self, handler, args, observe_seconds=3, grace_seconds=2, until=None):
        """Like run_scenario(), but for a scenario where sl_collect()
        legitimately never returns on its own -- e.g. a server that
        keeps rejecting negotiation, which libslink retries forever by
        design. slharness has no way to bound such a run itself, so
        observe its output, then terminate it (SIGTERM, falling back to
        SIGKILL) and return whatever it printed. A negative `returncode`
        here reflects our own signal, not necessarily a crash -- check
        `returncode` against the specific signal sent, or use
        assertNotCrashed().

        Without `until`, observes for a fixed `observe_seconds` before
        terminating -- appropriate when the assertions that follow only
        check the *absence* of something (no packets, no crash).

        With `until` (a callable taking one stdout line, including its
        trailing newline, and returning bool), terminates as soon as a
        line satisfies it, bounded by `observe_seconds` if it never does.
        Use this when asserting a specific line *must* appear -- a fixed
        sleep-then-check either wastes time waiting past when the line
        actually showed up, or (on a slow/loaded machine) can cut off
        before it arrives at all, since one reconnect cycle's duration
        isn't otherwise bounded here."""
        server = MockServer(handler).start()
        self.addCleanup(server.stop)

        full_args = [HARNESS, "--address", server.address()] + args
        proc = subprocess.Popen(full_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        if until is None:
            try:
                stdout, stderr = proc.communicate(timeout=observe_seconds)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    stdout, stderr = proc.communicate(timeout=grace_seconds)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    stdout, stderr = proc.communicate()
        else:
            # Two reader threads relay lines from the child's stdout/stderr
            # pipes into a queue as they arrive (readline() blocks in its
            # own thread rather than the main one, so this works the same
            # on Windows as on POSIX -- unlike select() on a pipe object,
            # which Windows doesn't support). The main thread just watches
            # the queue for either a match or the deadline.
            stdout_lines, stderr_lines = [], []
            line_q = queue.Queue()

            def pump(stream, tag):
                for line in iter(stream.readline, ""):
                    line_q.put((tag, line))
                line_q.put((tag, None))  # EOF marker

            threads = [
                threading.Thread(target=pump, args=(proc.stdout, "out"), daemon=True),
                threading.Thread(target=pump, args=(proc.stderr, "err"), daemon=True),
            ]
            for t in threads:
                t.start()

            deadline = time.monotonic() + observe_seconds
            eof_count = 0
            while eof_count < len(threads):
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                try:
                    tag, line = line_q.get(timeout=remaining)
                except queue.Empty:
                    break
                if line is None:
                    eof_count += 1
                    continue
                (stdout_lines if tag == "out" else stderr_lines).append(line)
                if tag == "out" and until(line):
                    break

            proc.terminate()
            try:
                proc.wait(timeout=grace_seconds)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

            # The process is gone, so each pump thread's readline() loop
            # hits EOF on its own; join them, then collect whatever they
            # queued in the meantime (including after the match/deadline
            # above, which only stopped the main thread from reading further).
            for t in threads:
                t.join(timeout=grace_seconds)
            while not line_q.empty():
                tag, line = line_q.get_nowait()
                if line is not None:
                    (stdout_lines if tag == "out" else stderr_lines).append(line)

            proc.stdout.close()
            proc.stderr.close()

            stdout = "".join(stdout_lines)
            stderr = "".join(stderr_lines)

        # See the matching comment in run_scenario(): join the handler
        # thread before trusting server.errors to be complete.
        server.stop()

        if server.errors:
            self.fail("mock server handler raised: %r" % (server.errors,))

        events = parse_output(stdout)
        events["returncode"] = proc.returncode
        events["stderr"] = stderr
        return events

    def assertNotCrashed(self, returncode, msg=""):
        """Fail if `returncode` indicates the process was killed by a
        signal associated with a memory-safety crash (as opposed to
        SIGTERM/SIGKILL sent deliberately by run_scenario_bounded(), or
        a normal non-negative exit)."""
        if returncode < 0 and -returncode in CRASH_SIGNALS:
            self.fail(
                "process was killed by %s: %s" % (signal.Signals(-returncode).name, msg)
            )


class TestV3UniStation(ProtocolTestCase):
    def test_basic_stream(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)

            for seq in range(1, 4):
                conn.sendall(
                    mseed.frame_v3_data(
                        seq, mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
                    )
                )

        events = self.run_scenario(
            handler,
            # sl_add_stream()/--station always enables multistation mode; a
            # true uni-station connection (no STATION command at all) comes
            # from sl_set_allstation_params()/--allstation instead.
            ["--v3", "--allstation", "BHZ", "--max-packets", "3", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 3, events)
        self.assertEqual([p["seq"] for p in events["packets"]], [1, 2, 3])
        self.assertEqual(events["packets"][0]["format"], "2")
        self.assertEqual(events["result"], "MAXPACKETS")


class TestV3MultiStation(ProtocolTestCase):
    def test_basic_stream(self):
        seen_stations = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            stations = serve_v3_multi(reader, conn, cmd)
            seen_stations.extend(stations)

            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TST2", channel="BHZ"))
            )
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TST1", channel="BHN"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--station",
                "XX_TST2:BHZ",
                "--station",
                "XX_TST1:BHN",
                "--max-packets",
                "2",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 2, events)
        stations = {p["station"] for p in events["packets"]}
        self.assertEqual(stations, {"XX_TST2", "XX_TST1"})
        # sl_add_stream() keeps the stream list sorted alphanumerically by
        # station id, so XX_TST1 is negotiated before XX_TST2 regardless of
        # the order the two --station options were given on the command line.
        self.assertEqual(seen_stations, ["STATION TST1 XX", "STATION TST2 XX"])

    def test_unexpected_station_is_dropped_not_fatal(self):
        """A packet for a station outside the configured list is a
        server-driven condition (truncated record, unexpected data), not an
        internal error; it must be logged and dropped, with the connection
        left open for the next, valid packet."""
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_multi(reader, conn, cmd)

            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="ZZ", station="NOPE", channel="BHZ"))
            )
            conn.sendall(
                mseed.frame_v3_data(2, mseed.build_ms2(network="XX", station="TST1", channel="BHN"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--station",
                "XX_TST1:BHN",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(attempts), 1, "expected no reconnect after the unexpected station")
        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["packets"][0]["station"], "XX_TST1")


class TestV4(ProtocolTestCase):
    def test_basic_stream(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, end_cmd = serve_v4(reader, conn, cmd)
            self.assertIn("STATION XX_TEST", commands)
            self.assertEqual(end_cmd, "END")

            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0, numsamples=50)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))
            conn.sendall(mseed.frame_v4_data(2, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "2", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 2, events)
        self.assertEqual(events["packets"][0]["format"], "3")
        self.assertEqual(events["packets"][0]["station"], "XX_TEST")
        self.assertEqual([p["seq"] for p in events["packets"]], [1, 2])


class TestProtocolSelection(ProtocolTestCase):
    def test_force_v3_against_v4_capable_server(self):
        negotiation_kind = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)  # advertises both 3.1 and 4.0
            cmd = serve_precommands(reader, conn)
            if cmd.startswith("SELECT") or cmd in ("DATA", "FETCH") or cmd.startswith("TIME"):
                negotiation_kind.append("v3")
                serve_v3_uni(reader, conn, cmd)
            else:
                negotiation_kind.append("v4")
                serve_v4(reader, conn, cmd)

            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(negotiation_kind, ["v3"])
        self.assertEqual(len(events["packets"]), 1, events)

    def test_auto_promotes_to_v4_when_offered(self):
        seen_commands = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, _ = serve_v4(reader, conn, cmd)
            seen_commands.extend(commands)

            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            # no --v3/--v4: default behavior should auto-promote to v4
            ["--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertIn("STATION XX_TEST", seen_commands)
        self.assertEqual(events["packets"][0]["format"], "3")

    def test_reconnect_drops_stale_protocol_and_capabilities(self):
        """Capabilities and protocol support are properties of the server
        being connected to, not the client. A reconnect to a server that no
        longer advertises v4 or any capabilities must not carry over the
        previous connection's promotion to v4 or its capability string --
        otherwise the client keeps sending SLPROTO 4.0 to a server that
        never offered it."""
        histories = {}

        def handler(conn, reader, server, idx):
            if idx == 1:
                serve_hello(reader, conn)  # default: offers v4 with capabilities
                cmd = serve_precommands(reader, conn)
                serve_v4(reader, conn, cmd)

                record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
                conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))
                # Handler returns without an END; the connection closes and
                # the client reconnects.
            else:
                serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")  # no capabilities at all
                cmd = serve_precommands(reader, conn)
                serve_v3_multi(reader, conn, cmd)

                conn.sendall(
                    mseed.frame_v3_data(
                        1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
                    )
                )

            histories[idx] = [c for c, _ in reader.history]

        events = self.run_scenario(
            handler,
            [
                "--station",
                "XX_TEST:BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "2",
                "--timeout-seconds",
                "12",
            ],
        )

        self.assertEqual(len(events["packets"]), 2, events)
        self.assertIn("SLPROTO 4.0", histories[1], histories)
        self.assertNotIn("SLPROTO 4.0", histories[2], histories)
        self.assertNotIn("CAPABILITIES", histories[2], histories)
        self.assertEqual(histories[2][1], "STATION TEST XX", histories)


class TestCapabilities(ProtocolTestCase):
    def test_hascapability_reflects_hello_flags(self):
        def handler(conn, reader, server, idx):
            serve_hello(
                conn=conn,
                reader=reader,
                server_id="SeedLink v3.1 (test) :: SLPROTO:3.1 CAP MULTISTATION",
            )
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
                "--cap",
                "MULTISTATION",
                "--cap",
                "NOTPRESENT",
            ],
        )

        self.assertEqual(events["caps"], {"MULTISTATION": True, "NOTPRESENT": False})


class TestPing(ProtocolTestCase):
    def test_ping_reports_server_and_site(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (mock)", site="Mock Site")

        events = self.run_scenario(handler, ["--ping"], timeout=8)

        self.assertIn('serverid="SeedLink v3.1 (mock)"', events["ping"])
        self.assertIn('site="Mock Site"', events["ping"])
        self.assertIn("status=0", events["ping"])

    def test_implementation_note_ping_does_not_validate_the_server_identity(self):
        # Both v3 and v4 specs define the HELLO response as an identifying
        # server ID line (see test_spec_v3.TestCommandSyntax and
        # test_spec_v4.TestCommandSyntax for the negotiation path, which
        # does check it via sayhello_int()). sl_ping() is a separate,
        # lighter-weight code path that bypasses that check entirely: its
        # doc comment promises "-1: invalid response to HELLO", but the
        # implementation never checks the "SEEDLINK" prefix -- any two
        # CRLF-terminated lines are accepted. Not a safety issue, just a
        # doc/behavior mismatch worth pinning down here as an
        # implementation note rather than a protocol expectation.
        def handler(conn, reader, server, idx):
            cmd = reader.read_command()
            self.assertEqual(cmd, "HELLO")
            conn.sendall(b"NotASeedLinkServer\r\nSomewhere\r\n")

        events = self.run_scenario(handler, ["--ping"], timeout=8)

        self.assertIn("status=0", events["ping"])


class TestErrorAndEnd(ProtocolTestCase):
    def test_server_error_causes_reconnect_cycle(self):
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)

            if idx == 1:
                conn.sendall(b"ERROR")
            else:
                conn.sendall(
                    mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
                )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "10",
            ],
        )

        self.assertGreaterEqual(len(attempts), 2, "expected the client to reconnect after ERROR")
        self.assertEqual(len(events["packets"]), 1, events)
        # Distinguishes actually exercising the ERROR-handling branch from a
        # plain disconnect, which alone would also make the client reconnect
        # and satisfy the two assertions above.
        self.assertTrue(
            any("Server reported an error" in line for line in events["log"]), events["log"]
        )

    def test_server_end_in_dialup_mode_terminates(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            conn.sendall(b"END")

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--dialup",
                "--allstation",
                "BHZ",
                "--max-packets",
                "5",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_server_end_outside_dialup_mode_terminates(self):
        """A completed time window has nothing left to ask for again, so END
        must end the connection even without --dialup; otherwise the client
        reconnects, re-requests, receives END again, and repeats forever."""
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            conn.sendall(b"END")

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "5",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(attempts), 1, "expected no reconnect attempt after END")
        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE


class TestConfigurationErrorsAreFatal(ProtocolTestCase):
    """Negotiation failures caused by the caller's own configuration (an
    unparsable/oversized time string, an oversized selector) reproduce
    identically on every retry, since none of them depend on server state;
    sl_collect() must report SLTERMINATE and stop instead of reconnecting
    forever at netdly intervals."""

    def test_v3_uni_unparsable_start_time_is_fatal(self):
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            # negotiate_uni_v3() fails on the bad time string before
            # sending anything past HELLO -- nothing else to serve.

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--time-start",
                "not-a-time",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(attempts), 1, "expected no reconnect after a local configuration error")
        self.assertEqual(len(events["packets"]), 0, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_v4_unparsable_start_time_is_fatal(self):
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn)
            # negotiate_v4() fails on the bad time string before sending
            # STATION/SELECT/DATA; serve_precommands() blocks on the next
            # command, which never arrives, until the client closes --
            # ConnectionClosed is expected and swallowed by MockServer.
            serve_precommands(reader, conn)

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ",
                "--time-start",
                "not-a-time",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(attempts), 1, "expected no reconnect after a local configuration error")
        self.assertEqual(len(events["packets"]), 0, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_v4_oversized_selector_is_fatal(self):
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn)
            serve_precommands(reader, conn)

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:" + "A" * 40,
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(attempts), 1, "expected no reconnect after a local configuration error")
        self.assertEqual(len(events["packets"]), 0, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_server_rejection_is_not_treated_as_a_configuration_error(self):
        """A server-driven rejection (unlike the client-local failures
        above) leaves config_error unset and remains retryable -- confirms
        the two failure classes are not conflated. sl_collect() blocks
        internally across its whole reconnect loop and only returns control
        to the harness on a packet (or a fatal outcome), so the rejection
        has to resolve itself eventually or --timeout-seconds never gets a
        chance to fire; accept on the second attempt, same as the ERROR
        case in TestErrorAndEnd above."""
        attempts = []

        def handler(conn, reader, server, idx):
            attempts.append(idx)
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd, accept_selectors=(idx != 1))

            if idx != 1:
                conn.sendall(
                    mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
                )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "10",
            ],
        )

        self.assertGreaterEqual(len(attempts), 2, "expected the client to reconnect after rejection")
        self.assertEqual(len(events["packets"]), 1, events)


class TestEndOnlyTimeWindow(ProtocolTestCase):
    def test_end_time_without_start_time_is_warned_and_ignored(self):
        """An end-only time window is not expressible by either protocol's
        DATA/TIME command; it must not silently vanish, but the connection
        must otherwise proceed normally (falling back to "next available
        data")."""

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--time-end",
                "2030-01-01T00:00:00",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertTrue(
            any("end time" in line and "without a start time" in line for line in events["log"]),
            events["log"],
        )
        self.assertEqual(len(events["packets"]), 1, events)


class TestOversizedAndBadSignature(ProtocolTestCase):
    def test_oversized_payload_reports_sltoolarge(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)

            # Claim a payload far larger than SL_RECV_BUFFER_SIZE (16384).
            # frame_v4() sizes the length field from the actual payload, so
            # build the header by hand instead to lie about the length.
            header = bytearray(mseed.SLHEADSIZE_V4)
            header[0:2] = mseed.SIGNATURE_V4
            header[2] = mseed.SLPAYLOAD_MSEED3
            header[3] = 0
            struct.pack_into("<I", header, 4, 1_000_000)
            struct.pack_into("<Q", header, 8, 1)
            header[16] = len("XX_TEST")
            conn.sendall(bytes(header) + b"XX_TEST")

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(events["result"], "-2")  # SLTOOLARGE

    def test_bad_header_signature_is_reported(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)

            if idx == 1:
                conn.sendall(b"XX" + b"\x00" * 6)
            else:
                conn.sendall(
                    mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
                )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "10",
            ],
            subprocess_timeout=20,
        )

        self.assertTrue(
            any("unexpected" in line and "header" in line for line in events["log"]),
            events["log"],
        )
        self.assertEqual(len(events["packets"]), 1, events)


class TestChunkedDelivery(ProtocolTestCase):
    def test_packet_assembles_across_small_writes(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)

            packet = mseed.frame_v3_data(
                1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
            )
            for i in range(0, len(packet), 7):
                conn.sendall(packet[i : i + 7])
                time.sleep(0.005)

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["packets"][0]["station"], "XX_TEST")


class TestNonBlocking(ProtocolTestCase):
    def test_nopacket_when_idle(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            time.sleep(3)  # never sends data

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--nonblock",
                "--stop-on-nopacket",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(events["result"], str(-1))  # SLNOPACKET


class TestBatchMode(ProtocolTestCase):
    def test_batch_mode_suppresses_intermediate_acks(self):
        histories = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn, accept_batch=True)
            serve_v3_multi(reader, conn, cmd, batch=True)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            histories.extend(c for c, _ in reader.history)

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--batch",
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        # Confirms BATCH was actually negotiated, not just that one packet
        # arrived -- which alone wouldn't catch a regression that stopped
        # sending BATCH while still (correctly, coincidentally) not waiting
        # for acks.
        self.assertIn("BATCH", histories, histories)


class TestAuthentication(ProtocolTestCase):
    def test_auth_accepted(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn, auth_mode="accept")
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--auth-value",
                "USERPASS alice secret",
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)

    def test_auth_rejected_yields_slauthfail(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            serve_precommands(reader, conn, auth_mode="reject")

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--auth-value",
                "USERPASS alice wrong",
                "--station",
                "XX_TEST:BHZ",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(events["result"], "-3")  # SLAUTHFAIL


class TestStateFileAcrossRuns(ProtocolTestCase):
    def test_second_run_resumes_at_saved_sequence(self):
        received_data_commands = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands = serve_v3_uni(reader, conn, cmd)
            received_data_commands.append(commands[-1])
            conn.sendall(
                mseed.frame_v3_data(
                    0x10, mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
                )
            )

        with tempfile.TemporaryDirectory() as tmp:
            statefile = os.path.join(tmp, "state")

            events1 = self.run_scenario(
                handler,
                [
                    "--v3",
                    "--allstation",
                    "BHZ",
                    "--no-lastpkttime",  # keep the DATA command to a bare sequence number
                    "--statefile",
                    statefile,
                    "--max-packets",
                    "1",
                    "--timeout-seconds",
                    "8",
                ],
            )
            self.assertEqual(len(events1["packets"]), 1, events1)
            self.assertTrue(os.path.exists(statefile))

            events2 = self.run_scenario(
                handler,
                [
                    "--v3",
                    "--allstation",
                    "BHZ",
                    "--no-lastpkttime",  # keep the DATA command to a bare sequence number
                    "--statefile",
                    statefile,
                    "--max-packets",
                    "1",
                    "--timeout-seconds",
                    "8",
                ],
            )
            self.assertEqual(len(events2["packets"]), 1, events2)

        # First run had no prior sequence: uni-station has no SELECT-then-DATA
        # resumption text to inspect directly here, but the second run's DATA
        # command must have requested the next sequence after what was saved.
        self.assertEqual(received_data_commands[0], "DATA")
        self.assertEqual(received_data_commands[1], "DATA 000011")  # seq 0x10 + 1, six hex digits


class TestKeepaliveAndInfoRegression(ProtocolTestCase):
    """Regression coverage for fable-review finding 1 (fixed in commit
    e05030c): slconn->stat->query_state must be reset on reconnect and
    on any completed INFO response, or keepalives silently stop forever
    once query_state gets stuck at InfoQuery/KeepAliveQuery."""

    def test_keepalive_resumes_after_a_dropped_info_exchange(self):
        connections = []

        def handler(conn, reader, server, idx):
            connections.append(idx)
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)

            if idx == 1:
                # An INFO request arrives (either the client's own request or
                # the library's automatic keepalive); drop the connection
                # without responding, leaving query_state mid-query.
                cmd = reader.read_command()
                self.assertTrue(cmd.startswith("INFO"), cmd)
                raise ConnectionClosed()

            # Second connection: answer the keepalive INFO request that
            # sl_collect() must still be able to send after reconnecting.
            cmd = reader.read_command()
            self.assertTrue(cmd.startswith("INFO"), cmd)
            info_record = mseed.build_ms2_info(b"<seedlink/>")
            conn.sendall(mseed.frame_v3_info(info_record, terminated=True))

            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--keepalive",
                "1",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "12",
            ],
            subprocess_timeout=20,
        )

        self.assertGreaterEqual(len(connections), 2, "client must reconnect after the dropped INFO")
        self.assertEqual(len(events["packets"]), 1, events)
        # Confirms the delivered packet is the real data packet, not the
        # keepalive/INFO response itself -- INFO packets carry
        # SL_UNSETSEQUENCE, so a regression that stopped swallowing them
        # would still satisfy the count-only assertion above.
        self.assertEqual(events["packets"][0]["seq"], 1)


class TestAuthValueNullRegression(ProtocolTestCase):
    """Regression coverage for fable-review finding 3: sayhello_int() must
    treat a NULL auth_value() return as an authentication failure
    (SLAUTHFAIL) instead of dereferencing it."""

    def test_auth_value_returning_null_yields_slauthfail(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            try:
                serve_precommands(reader, conn)
            except ConnectionClosed:
                pass

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--auth-null",
                "--station",
                "XX_TEST:BHZ",
                "--timeout-seconds",
                "5",
            ],
            subprocess_timeout=15,
        )

        self.assertEqual(events["returncode"], 0, events)
        self.assertEqual(events["result"], "-3", events)  # SLAUTHFAIL


class TestUnterminatedResponseRegression(ProtocolTestCase):
    """Regression coverage for core-review-pass3 finding 1: sl_recvresp()
    must never leave its buffer completely unterminated. A response that
    fills the buffer with no '\\r' anywhere used to leave network.c's
    subsequent strchr()/strcspn() scans with no guaranteed stopping
    point -- an out-of-bounds read in sayhello_int(), and an
    out-of-bounds *write* in negotiate_v4() (strchr() finding a stray
    '\\r' past the buffer, then writing a NUL through it). Both are only
    reliably observable under a memory sanitizer; on a plain build the
    assertion here is just "the client didn't crash or hang"."""

    def test_hello_response_with_no_terminator_does_not_crash(self):
        def handler(conn, reader, server, idx):
            if idx == 1:
                cmd = reader.read_command()
                if cmd != "HELLO":
                    raise AssertionError("expected HELLO, got %r" % cmd)

                # servstr/sitestr are each 200 bytes; sl_recvresp() now
                # always reserves the last byte for a forced NUL, so
                # exactly 199 bytes with no '\r' fills each read
                # completely without blocking for more (nothing is left
                # over in the stream for the next read to trip over).
                conn.sendall(b"X" * 199)
                conn.sendall(b"Y" * 199)

                try:
                    serve_precommands(reader, conn)
                except ConnectionClosed:
                    pass
            else:
                # A malformed first connection must not wedge the client;
                # confirm it still reconnects and streams normally.
                serve_hello(reader, conn)
                cmd = serve_precommands(reader, conn)
                serve_v3_uni(reader, conn, cmd)
                conn.sendall(
                    mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
                )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "10",
            ],
            subprocess_timeout=20,
        )

        self.assertEqual(events["returncode"], 0, events)
        self.assertEqual(len(events["packets"]), 1, events)

    def test_v4_negotiation_response_with_no_terminator_does_not_crash(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)

            if idx == 1:
                # Drain the client's pipelined STATION/SELECT/DATA batch
                # and answer every one of them with a response that
                # completely fills negotiate_v4()'s own 200-byte readbuf
                # with no '\r' anywhere -- the exact trigger for the
                # out-of-bounds write this is regression coverage for.
                commands = [cmd]
                while True:
                    more = reader.try_read_command(0.3)
                    if more is None:
                        break
                    commands.append(more)

                for _ in commands:
                    conn.sendall(b"Z" * 199)
            else:
                # A malformed first connection must not wedge the client;
                # confirm it still reconnects and streams normally.
                serve_v4(reader, conn, cmd)
                record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0, numsamples=50)
                conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "10",
            ],
            subprocess_timeout=20,
        )

        self.assertEqual(events["returncode"], 0, events)
        self.assertEqual(len(events["packets"]), 1, events)


if __name__ == "__main__":
    unittest.main()
