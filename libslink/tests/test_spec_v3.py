#!/usr/bin/env python3
"""SeedLink protocol v3 conformance tests.

Organizing document: https://www.seiscomp.de/doc/apps/seedlink.html#seedlink

Companion to test_spec_v4.py: every test class here is named after a
section of the v3 spec and every test's docstring/comment quotes or
paraphrases the specific requirement it enforces, rather than merely
exercising whatever path the current implementation happens to take
(that's what test_protocol.py is for). Where libslink diverges from the
spec, the test asserts the *spec's* behavior and fails today -- see
README.md's "Spec-conformance deviations" table for the running list.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))

from slmock import mseed
from slmock.server import serve_hello, serve_precommands, serve_v3_multi, serve_v3_uni
from test_protocol import ProtocolTestCase


class TestPacketFraming(ProtocolTestCase):
    """seiscomp docs, "SeedLink packet structure": the 8-byte 'SL' +
    six-digit hex sequence header, 512-byte miniSEED payload."""

    def test_header_is_8_bytes_signature_plus_six_hex_digits(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(0xABCDEF, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(events["packets"][0]["seq"], 0xABCDEF)
        self.assertEqual(events["packets"][0]["length"], 512)

    def test_sequence_number_wraps_at_ffffff(self):
        # "Wraparound occurs at FFFFFF (16,777,215)" -- the packet after
        # the maximum 6-hex-digit value wraps back to 0, not to a 7th digit.
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(0xFFFFFF, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            conn.sendall(
                mseed.frame_v3_data(0x000000, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "2", "--timeout-seconds", "8"],
        )

        self.assertEqual([p["seq"] for p in events["packets"]], [0xFFFFFF, 0])


class TestCommandSyntax(ProtocolTestCase):
    """seiscomp docs, "Commands": v3-specific syntax -- comma-delimited
    time, hex sequence numbers in DATA/FETCH, FETCH vs DATA, and command
    framing."""

    def test_every_command_is_legal_v3_syntax(self):
        def handler(conn, reader, server, idx):
            # Setting strict_protocol makes CommandReader validate each
            # command against spec.check_command() as it's read (raising
            # immediately on an illegal one), so there's nothing further to
            # check once the scenario completes -- reaching the packet
            # count assertion below already proves every command was legal.
            reader.strict_protocol = 3
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)

    def test_time_command_uses_comma_delimited_format(self):
        captured = {}

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            commands = serve_v3_uni(reader, conn, cmd)
            captured["commands"] = commands
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
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

        time_cmd = [c for c in captured["commands"] if c.startswith("TIME")][0]
        self.assertEqual(time_cmd, "TIME 2024,01,01,00,00,00 2024,01,02,00,00,00")
        self.assertEqual(len(events["packets"]), 1, events)

    def test_fetch_used_in_dialup_mode_data_in_realtime(self):
        for dialup, expect_verb in ((True, "FETCH"), (False, "DATA")):
            with self.subTest(dialup=dialup):
                captured = {}

                def handler(conn, reader, server, idx):
                    serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
                    cmd = serve_precommands(reader, conn)
                    commands = serve_v3_uni(reader, conn, cmd)
                    captured["commands"] = commands
                    conn.sendall(
                        mseed.frame_v3_data(
                            1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ")
                        )
                    )
                    if dialup:
                        conn.sendall(b"END")

                args = ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"]
                if dialup:
                    args.append("--dialup")

                events = self.run_scenario(handler, args)

                self.assertEqual(captured["commands"][-1], expect_verb, captured["commands"])
                self.assertEqual(len(events["packets"]), 1, (dialup, events))

    def test_data_sequence_number_stays_within_six_hex_digits_multi(self):
        """seiscomp docs, "SeedLink packet structure": the wire sequence
        field is exactly six hex digits, wrapping at FFFFFF/16,777,215.
        A resumption sequence one past that boundary must wrap into six
        digits ("000001"), not be sent as a 7-hex-digit argument
        ("1000001") -- a value the v3 wire format cannot represent."""
        history = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_multi(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            history.extend(c for c, _ in reader.history)

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--station",
                "XX_TEST:BHZ:16777216",  # one past the 6-hex-digit/24-bit boundary
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        data_cmd = [c for c in history if c.startswith("DATA")][0]
        self.assertEqual(data_cmd, "DATA 000001", history)

    def test_data_sequence_number_stays_within_six_hex_digits_uni(self):
        """Same six-hex-digit wire constraint as above, exercised through
        negotiate_uni_v3() (uni-station mode) instead of
        negotiate_multi_v3()."""
        captured = {}

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            captured["commands"] = serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--allstation-seq",
                "16777216",  # one past the 6-hex-digit/24-bit boundary
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(captured["commands"][-1], "DATA 000001", captured["commands"])

    def test_data_sequence_number_wraps_at_ffffff_boundary_uni(self):
        """A resumption sequence exactly at FFFFFF wraps to 000000, the
        same boundary the wire header itself wraps at (see
        test_sequence_number_wraps_at_ffffff above)."""
        captured = {}

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            captured["commands"] = serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(0, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--allstation",
                "BHZ",
                "--allstation-seq",
                "16777215",  # FFFFFF, the last representable sequence
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertEqual(captured["commands"][-1], "DATA 000000", captured["commands"])

    def test_alldata_sequence_requests_from_zero_multi(self):
        """v3 has no "all data" verb; SL_ALLDATASEQUENCE is libslink's
        own sentinel (not part of the wire protocol) and must still
        format to a valid six-hex-digit sequence rather than the raw
        sentinel value. Requesting from 000000 -- the oldest sequence a
        server can hold -- is the closest v3 equivalent to "all data"."""

        history = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_multi(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(0, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            history.extend(c for c, _ in reader.history)

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--station",
                "XX_TEST:BHZ:ALL",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        data_cmd = [c for c in history if c.startswith("DATA")][0]
        self.assertEqual(data_cmd, "DATA 000000", history)


class TestHandshakeOrdering(ProtocolTestCase):
    """seiscomp docs, "Handshaking": modifier commands (SELECT, STATION)
    are acknowledged OK/ERROR; action commands (DATA/FETCH/TIME/END) are
    not. Uni-station has no STATION/END; multi-station requires both."""

    def test_uni_station_has_no_station_or_end_command(self):
        seen = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            commands = serve_v3_uni(reader, conn, cmd)
            seen.extend(commands)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        self.assertFalse(any(c.startswith("STATION") or c == "END" for c in seen), seen)

    def test_multi_station_requires_station_and_final_end(self):
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            # serve_v3_multi() itself asserts the negotiation ends in a
            # bare, unacknowledged END -- raises if it doesn't.
            stations = serve_v3_multi(reader, conn, cmd)
            self.assertEqual(stations, ["STATION TEST XX"])
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )

        events = self.run_scenario(
            handler,
            ["--v3", "--station", "XX_TEST:BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )

        self.assertEqual(len(events["packets"]), 1, events)

    def test_modifier_commands_are_acknowledged_action_commands_are_not(self):
        # SELECT (a modifier) gets an explicit OK/ERROR; DATA (an action
        # command in uni-station mode) does not -- serve_v3_uni() already
        # only ever writes a response for SELECT lines, so a client that
        # incorrectly waited for one after DATA would simply hang here
        # until the scenario's own timeout.
        history = []

        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            conn.sendall(
                mseed.frame_v3_data(1, mseed.build_ms2(network="XX", station="TEST", channel="BHZ"))
            )
            history.extend(c for c, _ in reader.history)

        events = self.run_scenario(
            handler,
            ["--v3", "--allstation", "BHZ", "--max-packets", "1", "--timeout-seconds", "8"],
        )
        self.assertEqual(len(events["packets"]), 1, events)
        # One OK response was written for SELECT (see serve_v3_uni()); DATA
        # is the very next command sent with no response awaited in between,
        # which the client blocking on one would show up as a hang above
        # rather than here, but this pins the exact ack/no-ack shape down
        # explicitly instead of relying on that timeout as the only signal.
        self.assertIn("SELECT", [c.split(" ", 1)[0] for c in history])
        self.assertIn("DATA", [c.split(" ", 1)[0] for c in history])


class TestInfo(ProtocolTestCase):
    """seiscomp docs, "INFO": XML embedded in a pseudo-miniSEED2 record
    behind the SLINFO header; '*'-flagged continuation for multi-packet
    responses."""

    def test_each_info_level_is_requested_and_delivered(self):
        levels = ("ID", "CAPABILITIES", "STATIONS", "STREAMS", "GAPS", "CONNECTIONS", "ALL")

        for level in levels:
            with self.subTest(level=level):
                def handler(conn, reader, server, idx, level=level):
                    serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
                    cmd = serve_precommands(reader, conn)
                    serve_v3_uni(reader, conn, cmd)
                    cmd = reader.read_command()
                    self.assertEqual(cmd, "INFO " + level, cmd)
                    record = mseed.build_ms2_info(("<%s/>" % level).encode())
                    conn.sendall(mseed.frame_v3_info(record, terminated=True))

                events = self.run_scenario(
                    handler,
                    [
                        "--v3",
                        "--info",
                        level,
                        "--allstation",
                        "BHZ",
                        "--max-packets",
                        "1",
                        "--timeout-seconds",
                        "8",
                    ],
                )

                self.assertEqual(len(events["packets"]), 1, (level, events))
                self.assertIn(("<%s/>" % level).encode(), events["packets"][0]["payload"])

    def test_continued_response_is_reassembled_from_both_fragments(self):
        # The '*' continuation flag (byte 7 of the SLINFO header) marks a
        # non-final fragment; the caller is expected to concatenate
        # fragments up to the first non-'*' (terminated) one.
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            cmd = reader.read_command()
            self.assertEqual(cmd, "INFO ID", cmd)
            part1 = mseed.build_ms2_info(b"<seedlink><part1/>")
            part2 = mseed.build_ms2_info(b"<part2/></seedlink>")
            conn.sendall(mseed.frame_v3_info(part1, terminated=False))
            conn.sendall(mseed.frame_v3_info(part2, terminated=True))

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--info",
                "ID",
                "--allstation",
                "BHZ",
                "--max-packets",
                "2",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 2, events)
        combined = events["packets"][0]["payload"] + events["packets"][1]["payload"]
        self.assertIn(b"<seedlink><part1/>", combined)
        self.assertIn(b"<part2/></seedlink>", combined)

    def test_info_packets_carry_no_sequence_number(self):
        # The 8-byte v3 header for INFO responses is "SLINFO" + a filler
        # byte + the continuation flag -- there is no sequence number
        # field at all (contrast the 6 hex digits a data packet carries).
        def handler(conn, reader, server, idx):
            serve_hello(reader, conn, server_id="SeedLink v3.1 (test)")
            cmd = serve_precommands(reader, conn)
            serve_v3_uni(reader, conn, cmd)
            cmd = reader.read_command()
            self.assertEqual(cmd, "INFO ID", cmd)
            record = mseed.build_ms2_info(b"<seedlink/>")
            conn.sendall(mseed.frame_v3_info(record, terminated=True))

        events = self.run_scenario(
            handler,
            [
                "--v3",
                "--info",
                "ID",
                "--allstation",
                "BHZ",
                "--max-packets",
                "1",
                "--timeout-seconds",
                "8",
            ],
        )

        self.assertEqual(len(events["packets"]), 1, events)
        # SL_UNSETSEQUENCE (UINT64_MAX): no sequence number was ever set.
        self.assertEqual(events["packets"][0]["seq"], (2**64) - 1)


if __name__ == "__main__":
    unittest.main()
