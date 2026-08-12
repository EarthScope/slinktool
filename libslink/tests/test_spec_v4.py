#!/usr/bin/env python3
"""SeedLink protocol v4 conformance tests.

Organizing document: https://docs.fdsn.org/projects/seedlink/en/latest/protocol.html

Unlike test_protocol.py (which exercises whatever paths the current
implementation happens to take), every test class here is named after a
section of the v4 spec and every test's docstring/comment quotes or
paraphrases the specific requirement it enforces. Where libslink diverges
from the spec, the test asserts the *spec's* behavior and fails today --
see README.md's "Spec-conformance deviations" table for the running list.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from slmock import mseed, spec
from slmock.server import (
    ConnectionClosed,
    error_v4,
    serve_hello,
    serve_precommands,
    serve_v3_uni,
    serve_v4,
)
from test_protocol import ProtocolTestCase


class TestPacketHeader(ProtocolTestCase):
    """protocol.html, "Data packet structure": the 17-byte 'SE' header,
    field-by-field."""

    def test_header_fields_are_read_correctly(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0, numsamples=5)
            conn.sendall(mseed.frame_v4_data(424242, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        pkt = events["packets"][0]
        self.assertEqual(pkt["seq"], 424242)
        self.assertEqual(pkt["format"], "3")
        self.assertEqual(pkt["subformat"], "D")
        self.assertEqual(pkt["station"], "XX_TEST")

    def test_sequence_number_is_64_bit(self):
        # "Sequence numbers of a single station within a single server MUST
        # be unique and strictly increasing" over a UINT64 field -- confirm
        # values well beyond 32 bits round-trip exactly.
        big_seq = (2**64) - 3

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(big_seq, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(events["packets"][0]["seq"], big_seq)

    def test_large_sequence_number_round_trips_through_statefile(self):
        big_seq = (2**63) + 12345

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(big_seq, "XX_TEST", record))

        with tempfile.TemporaryDirectory() as tmp:
            statefile = os.path.join(tmp, "state")
            events = self.run_scenario(
                handler,
                [
                    "--v4",
                    "--station",
                    "XX_TEST:BHZ",
                    "--statefile",
                    statefile,
                    "--max-packets",
                    "1",
                    "--timeout-seconds",
                    "8",
                ],
            )
            self.assertEqual(events["packets"][0]["seq"], big_seq)
            with open(statefile) as f:
                contents = f.read()
            self.assertIn(str(big_seq), contents)

    def test_station_id_of_21_bytes_is_accepted(self):
        # SL_MAX_STATIONID is 22 (libslink.h), which includes the
        # terminating NUL -- so the longest station ID the buffer can
        # hold is 21 bytes. The spec places no length restriction on
        # station identifiers at all; this pins down the boundary of what
        # currently works.
        # update_stream() (slutils.c) logs and drops a packet whose station
        # ID matches none of the configured streams, rather than
        # disconnecting, so the packet's station ID must still be one the
        # client actually asked for to be delivered here.
        stationid = "X" * 21

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            sid = "FDSN:%s_00_B_H_Z" % stationid
            record = mseed.build_ms3(sid=sid, samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, stationid, record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                stationid + ":BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(events["packets"][0]["station"], stationid)

    def test_station_id_over_21_bytes_should_not_hang_the_client(self):
        """protocol.html, "Station and stream identifiers": no length
        restriction is placed on station identifiers, but SL_MAX_STATIONID
        (libslink.h) is 22, so an identifier of 22 bytes or more cannot be
        represented in SLpacketinfo.stationid. sl_collect() reports this as
        a fatal, non-recoverable error -- disconnecting and returning
        SLTERMINATE -- rather than reconnect-looping on the same oversized
        ID forever."""
        stationid = "X" * 22

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, stationid, record))

        # subprocess_timeout well under what a hang would need; run_scenario
        # turns the resulting subprocess.TimeoutExpired into self.fail().
        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "5"],
            subprocess_timeout=8,
        )

        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_zero_length_payload_should_not_wedge_the_stream(self):
        """protocol.html, "Data packet structure": the payload-length
        field is a plain UINT32 with no stated minimum, so a 0-byte
        payload (an empty JSON error body, say) is legal. It must be
        delivered like any other packet, and must not withhold the
        packet sent behind it."""

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            conn.sendall(mseed.frame_v4_json_error(1, "XX_TEST", b""))
            # A perfectly ordinary packet right behind it -- if the
            # zero-length one were handled correctly this would also be
            # delivered and counted below.
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(2, "XX_TEST", record))

        # subprocess_timeout well under what a stall would need;
        # run_scenario turns the resulting subprocess.TimeoutExpired into
        # self.fail().
        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "2", "--timeout-seconds", "5"],
            subprocess_timeout=8,
        )

        self.assertEqual(len(events["packets"]), 2, events)
        self.assertEqual(events["packets"][0]["seq"], 1)
        self.assertEqual(events["packets"][0]["format"], "J")
        self.assertEqual(events["packets"][0]["subformat"], "E")
        self.assertEqual(events["packets"][0]["length"], 0)
        self.assertEqual(events["packets"][1]["seq"], 2)
        self.assertEqual(events["packets"][1]["format"], "3")


class TestReservedFormats(ProtocolTestCase):
    """protocol.html, "Data formats": the reserved format/subformat pairs."""

    def test_every_reserved_format_subformat_pair_passes_through(self):
        for fmt, sub in spec.V4_FORMATS:
            with self.subTest(format=fmt, subformat=sub):
                if (fmt, sub) == ("2", "D"):
                    payload = mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
                elif (fmt, sub) == ("3", "D"):
                    payload = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
                else:
                    payload = b'{"marker": "%s%s"}' % (fmt.encode(), sub.encode())

                def handler(conn, reader, server, idx, fmt=fmt, sub=sub, payload=payload):
                    serve_hello(reader, conn)
                    cmd = serve_precommands(reader, conn)
                    serve_v4(reader, conn, cmd)
                    conn.sendall(mseed.frame_v4(fmt, sub, 1, payload, stationid="XX_TEST"))

                events = self.run_scenario(
                    handler,
                    [
                        "--v4",
                        "--station",
                        "XX_TEST:BHZ",
                        "--max-packets",
                        "1",
                        "--timeout-seconds",
                        "8",
                    ],
                )

                self.assertEqual(len(events["packets"]), 1, (fmt, sub, events))
                pkt = events["packets"][0]
                self.assertEqual(pkt["format"], fmt)
                self.assertEqual(pkt["subformat"], sub)
                self.assertEqual(pkt["length"], len(payload))

    def test_json_error_packet_is_delivered_mid_stream(self):
        # A 'J'/'E' error packet is not a keepalive/INFO response (only
        # 'J'/'I' is), so it must reach the caller like any other packet.
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            text = b'{"error": {"code": "INTERNAL", "message": "boom"}}'
            conn.sendall(mseed.frame_v4_json_error(1, "XX_TEST", text))
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(2, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "2", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 2, events)
        self.assertEqual(events["packets"][0]["format"], "J")
        self.assertEqual(events["packets"][0]["subformat"], "E")
        self.assertIn(b"INTERNAL", events["packets"][0]["payload"])
        self.assertEqual(events["packets"][1]["format"], "3")


class TestErrorCodes(ProtocolTestCase):
    """protocol.html, "Error codes": UNSUPPORTED, UNEXPECTED, UNAUTHORIZED,
    LIMIT, ARGUMENTS, AUTH, INTERNAL."""

    def test_each_error_code_is_logged_verbatim_and_does_not_proceed(self):
        for code in spec.V4_ERROR_CODES:
            with self.subTest(code=code):
                def handler(conn, reader, server, idx, code=code):
                    serve_hello(reader, conn)
                    cmd = serve_precommands(reader, conn)
                    # negotiate_v4() pipelines STATION/SELECT/DATA for a
                    # stream before reading any response, so more than one
                    # command can already be sitting in the kernel receive
                    # buffer here. Drain the whole batch and answer each
                    # one before returning -- otherwise MockServer's close
                    # on a socket with unread received data can turn into
                    # a Windows RST, which can silently discard the ERROR
                    # response below before the client reads it.
                    # XX_TEST:BHZ is a single station with one selector, so
                    # exactly two more commands (SELECT, DATA) follow the
                    # STATION `cmd` already read -- deterministic, no
                    # quiet-period guess needed.
                    commands = [cmd, reader.read_command(), reader.read_command()]

                    for _ in commands:
                        error_v4(conn, code, "rejected for testing")

                # A rejected STATION is not fatal (only auth failures are),
                # so sl_collect() retries negotiation forever -- terminate
                # as soon as the rejection is logged (or after a few
                # retries if it somehow never is); the process never gets
                # to send a packet regardless of when we cut it off.
                events = self.run_scenario_bounded(
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
                        "5",
                    ],
                    until=lambda line: code in line and "not accepted" in line,
                )

                self.assertNotCrashed(events["returncode"], (code, events))
                self.assertEqual(events["packets"], [], (code, events))
                self.assertTrue(
                    any(code in line and "not accepted" in line for line in events["log"]),
                    (code, events["log"]),
                )

    def test_connection_closed_after_partial_negotiation_response_should_not_crash(self):
        """New finding (found while building this suite, not in
        fable-review.md): negotiate_v4() (network.c) sends every
        STATION/SELECT/DATA command for a stream up front, then reads
        one response per command in a second pass -- always passing
        `command=NULL` to sl_recvresp() for that second-pass read
        (network.c:2272). sl_recvresp() (network.c:848) unconditionally
        calls strcspn(command, "\\r\\n") in its own error-logging path
        when a read fails. If the server answers the first queued
        command and then closes the connection (or any single response
        in the middle of the pass fails to arrive) before answering the
        rest, that second-pass read fails and dereferences the NULL
        `command` argument -- segfaulting the client. Any v4 server that
        rejects a station and drops the connection immediately afterward
        (a normal, spec-legal thing to do) can crash a real client this
        way."""

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            # Read the STATION command sl_add_stream() builds, answer with
            # an ERROR, then close before the SELECT/DATA responses this
            # stream also queued can be read.
            self.assertTrue(cmd.startswith("STATION"), cmd)
            error_v4(conn, "LIMIT", "too many stations")

            # Drain (without answering) the pipelined SELECT/DATA commands
            # this stream also queued, so the kernel receive buffer is
            # empty before close -- otherwise Windows can send RST instead
            # of FIN, which can also discard the ERROR response above
            # before the client reads it. The point under test is the
            # client's second-pass read timing out on an unanswered
            # command, not losing the first response to a reset.
            # XX_TEST:BHZ is a single station with one selector, so exactly
            # two more commands (SELECT, DATA) follow the STATION `cmd`
            # already read -- deterministic, no quiet-period guess needed.
            reader.read_command()
            reader.read_command()

            raise ConnectionClosed()

        # The rejected STATION is not fatal, so sl_collect() retries
        # negotiation forever (by design) rather than exiting -- observe
        # a couple of reconnect cycles for a crash, then terminate.
        events = self.run_scenario_bounded(
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
                "5",
            ],
        )

        self.assertNotCrashed(
            events["returncode"],
            "sl_recvresp() dereferenced a NULL `command` argument (network.c:848, "
            "called from negotiate_v4() at network.c:2306): %r" % (events,),
        )

    def test_auth_error_response_yields_slauthfail(self):
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
        self.assertTrue(
            any("AUTH" in line and "invalid credentials" in line for line in events["log"]),
            events["log"],
        )

    def test_error_to_slproto_from_a_v4_advertising_server_is_fatal(self):
        """sayhello_int() (network.c:1184) only sends "SLPROTO 4.0" when the
        server's own HELLO capabilities advertised SLPROTO:4.x, or the
        caller explicitly forced v4 via sl_set_protocol(). So an ERROR
        response here means the server is contradicting the capabilities
        it just advertised -- not a server that merely prefers v3. A
        reasonable reading of the spec ("a SeedLink 4 server can also
        support SeedLink 3 protocol", protocol.html, "Differences ...
        version 3 and 4") might suggest falling back to v3 on the same
        connection, but silently downgrading past a server's self-
        contradiction would mask a broken server rather than report it.
        libslink instead logs the rejection and fails the connection
        attempt, which sl_collect() retries later with the same
        v4-preferring logic (by design, like any persistent negotiation
        failure) -- this is intentional, not a defect."""

        def handler(conn, reader, server, idx):
            cmd = reader.read_command()
            self.assertEqual(cmd, "HELLO")
            conn.sendall(b"SeedLink v3.1 (test) :: SLPROTO:3.1 SLPROTO:4.0\r\n")
            conn.sendall(b"Test Data Center\r\n")
            cmd = reader.read_command()
            self.assertEqual(cmd, "SLPROTO 4.0")
            error_v4(conn, "UNSUPPORTED", "SLPROTO not available")

            # SLPROTO is a single request/response with nothing pipelined
            # after it -- the client stops here on the rejection rather
            # than sending STATION/SELECT/DATA on this connection, so the
            # receive buffer is already empty; nothing to drain before close.
            raise ConnectionClosed()

        # The rejection is not fatal to sl_collect() itself, so it retries
        # negotiation forever (by design) rather than exiting -- terminate
        # as soon as the rejection is logged (or after a few retries if it
        # somehow never is).
        # --verbose: the SLPROTO rejection is logged at verbosity 2
        # (network.c:1215), unlike the always-logged command-level
        # rejections used elsewhere in this file.
        events = self.run_scenario_bounded(
            handler,
            [
                "--allstation",
                "BHZ",
                "--reconnectdelay",
                "1",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "5",
                "--verbose",
            ],
            until=lambda line: "SLPROTO 4.0" in line and "not accepted" in line,
        )

        self.assertNotCrashed(events["returncode"], events)
        self.assertEqual(events["packets"], [], events)
        self.assertTrue(
            any("SLPROTO 4.0" in line and "not accepted" in line for line in events["log"]),
            events["log"],
        )


class TestCommandSyntax(ProtocolTestCase):
    """protocol.html, "Commands": v4-specific syntax rules -- decimal
    sequence numbers, ISO 8601 times, DATA's five forms, command
    ordering, and line framing."""

    def test_every_command_is_legal_v4_syntax(self):
        def handler(conn, reader, server, idx):
            # Setting strict_protocol makes CommandReader validate each
            # command against spec.check_command() as it's read (raising
            # immediately on a v3-only verb -- CAPABILITIES/BATCH/FETCH/
            # TIME/CAT -- or other illegal syntax), so there's nothing
            # further to check once the scenario completes -- reaching the
            # packet count assertion below already proves every command
            # was legal v4 syntax.
            reader.strict_protocol = 4
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)

    def test_data_sequence_number_is_decimal_not_hex(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, _ = serve_v4(reader, conn, cmd)
            data_cmd = [c for c in commands if c.startswith("DATA")][0]
            # A 64-bit sequence number formatted in decimal, not hex, is
            # visibly different (no leading "0x", no alpha hex digits
            # beyond what's coincidentally a decimal digit already).
            arg = data_cmd.split(" ")[1]
            self.assertTrue(arg.isdigit() or arg == "ALL", data_cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ:99999999999",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )
        self.assertEqual(len(events["packets"]), 1, events)

    def test_data_all_with_time_window_is_iso8601(self):
        captured = {}

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, _ = serve_v4(reader, conn, cmd)
            captured["commands"] = commands
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ",
                "--time-start",
                "2024-01-01T00:00:00",
                "--time-end",
                "2024-01-02T00:00:00",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        data_cmd = [c for c in captured["commands"] if c.startswith("DATA")][0]
        self.assertEqual(data_cmd, "DATA ALL 2024-01-01T00:00:00Z 2024-01-02T00:00:00Z")
        self.assertEqual(len(events["packets"]), 1, events)

    def test_data_all_sequence_form(self):
        captured = {}

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, _ = serve_v4(reader, conn, cmd)
            captured["commands"] = commands
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--allstation",
                "BHZ",
                "--seq-all",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )
        # --allstation forces multi-station (sl_set_allstation_params()
        # goes through the same v4 negotiator as --station).
        self.assertEqual(len(events["packets"]), 1, events)
        data_cmd = [c for c in captured["commands"] if c.startswith("DATA")][0]
        self.assertEqual(data_cmd, "DATA ALL", captured["commands"])

    def test_endfetch_in_dialup_mode(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, end_cmd = serve_v4(reader, conn, cmd)
            self.assertEqual(end_cmd, "ENDFETCH")
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))
            conn.sendall(b"END")

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--dialup",
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "5",
                "--timeout-seconds",
                "8",
            ],
        )
        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["result"], "0")  # SLTERMINATE

    def test_slproto_precedes_station_select_data(self):
        seen = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            commands, _ = serve_v4(reader, conn, cmd)
            seen.extend(commands)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )
        self.assertEqual(len(events["packets"]), 1, events)
        # SLPROTO is consumed by serve_precommands before `seen` starts, so
        # its absence from `seen` and the presence of STATION first here
        # together confirm the ordering.
        self.assertTrue(seen[0].startswith("STATION"), seen)


class TestAsyncHandshaking(ProtocolTestCase):
    """protocol.html, "Handshaking": "Client may send asynchronous
    commands" -- responses need not arrive inline with each command
    sent."""

    def test_client_tolerates_all_responses_deferred_to_the_end(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            # XX_TEST:BHZ is a single station with one selector, so exactly
            # two more commands (SELECT, DATA) follow the STATION `cmd`
            # already read -- deterministic, no quiet-period guess needed.
            serve_v4(reader, conn, cmd, defer_responses=True, expected_commands=2)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--v4", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )
        self.assertEqual(len(events["packets"]), 1, events)


class TestCapabilities(ProtocolTestCase):
    """protocol.html, "Capabilities": SLPROTO:#.#, AUTH:type, TIME,
    SEQWILDCARD."""

    def test_hascapability_for_each_spec_capability(self):
        def handler(conn, reader, server, idx):
            serve_hello(
                reader,
                conn,
                server_id="SeedLink v4.0 (test) :: SLPROTO:4.0 TIME SEQWILDCARD AUTH:USERPASS",
            )
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
                "--cap",
                "SLPROTO:4.0",
                "--cap",
                "TIME",
                "--cap",
                "SEQWILDCARD",
                "--cap",
                "AUTH:USERPASS",
                "--cap",
                "NOTPRESENT",
            ],
        )

        self.assertEqual(
            events["caps"],
            {
                "SLPROTO:4.0": True,
                "TIME": True,
                "SEQWILDCARD": True,
                "AUTH:USERPASS": True,
                "NOTPRESENT": False,
            },
        )

    def test_server_advertising_only_a_future_major_version_falls_back_to_v3(self):
        # v4todo.txt asks (as an open question) what a client should do
        # when a server advertises e.g. "SLPROTO:5.0" and nothing else.
        # sayhello_int() (network.c) only recognizes major version 3 or 4
        # tokens; an unrecognized-only capability string leaves
        # server_protocols at 0, and the "no protocols advertised by
        # server are recognized" branch then defaults to 3.x -- this
        # pins that answer down as the expected, spec-reasonable
        # behavior (never request a version the server never offered).
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v5.0 (test) :: SLPROTO:5.0")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["packets"][0]["format"], "2")

    def test_server_advertising_a_higher_minor_version_is_accepted_as_4_0(self):
        # "requesting 4.0 is legal" against a server that only advertises
        # a newer minor version (protocol.html: SLPROTO carries a
        # <major>.<minor> pair; a client only implementing 4.0 can still
        # talk to a 4.1 server).
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v4.1 (test) :: SLPROTO:4.1")
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            record = mseed.build_ms3(sid="FDSN:XX_TEST_00_B_H_Z", samplerate=100.0)
            conn.sendall(mseed.frame_v4_data(1, "XX_TEST", record))

        events = self.run_scenario(
            handler,
            ["--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )
        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["packets"][0]["format"], "3")


class TestInfo(ProtocolTestCase):
    """protocol.html, "INFO": JSON-formatted, single-packet ('J'/'I')
    responses; required "software"/"organization" members."""

    def test_each_info_item_is_delivered_as_a_json_packet(self):
        items = ("ID", "FORMATS", "CAPABILITIES", "STATIONS", "STREAMS", "CONNECTIONS")

        for item in items:
            with self.subTest(item=item):
                def handler(conn, reader, server, idx, item=item):
                    serve_hello(reader, conn)
                    cmd = serve_precommands(reader, conn)
                    serve_v4(reader, conn, cmd)
                    cmd = reader.read_command()
                    self.assertEqual(cmd, "INFO " + item, cmd)
                    text = ('{"software": "mock/1.0", "organization": "test", '
                            '"item": "%s"}' % item).encode()
                    conn.sendall(mseed.frame_v4_info(1, text))

                events = self.run_scenario(
                    handler,
                    [
                        "--v4",
                        "--info",
                        item,
                        "--station",
                        "XX_TEST:BHZ",
                        "--max-packets",
                        "1",
                        "--timeout-seconds",
                        "8",
                    ],
                )

                self.assertEqual(len(events["packets"]), 1, (item, events))
                pkt = events["packets"][0]
                self.assertEqual(pkt["format"], "J")
                self.assertEqual(pkt["subformat"], "I")
                payload = pkt["payload"]
                self.assertIn(b'"software"', payload)
                self.assertIn(b'"organization"', payload)

    def test_zero_length_info_response_is_delivered(self):
        """A 0-byte 'J'/'I' response is as legal as a 0-byte 'J'/'E' one
        (see TestPacketHeader.test_zero_length_payload_should_not_wedge_the_stream)
        and must complete the pending INFO query rather than stall it."""

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn)
            cmd = serve_precommands(reader, conn)
            serve_v4(reader, conn, cmd)
            cmd = reader.read_command()
            self.assertEqual(cmd, "INFO ID", cmd)
            conn.sendall(mseed.frame_v4_info(1, b""))

        events = self.run_scenario(
            handler,
            [
                "--v4",
                "--info",
                "ID",
                "--station",
                "XX_TEST:BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "5",
            ],
            subprocess_timeout=8,
        )

        self.assertEqual(len(events["packets"]), 1, events)
        pkt = events["packets"][0]
        self.assertEqual(pkt["format"], "J")
        self.assertEqual(pkt["subformat"], "I")
        self.assertEqual(pkt["length"], 0)


if __name__ == "__main__":
    unittest.main()
