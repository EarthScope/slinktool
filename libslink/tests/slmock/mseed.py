"""Synthetic miniSEED 2/3 record builders and SeedLink wire framing.

Everything here is built little-endian, matching this test host, so the
byte-swap auto-detection path in the library (exercised separately by
test_payload.c) never needs to trigger for these protocol-level tests.
"""

import struct

MS2_FIXED_LENGTH = 48
MS2_B1000_LENGTH = 8
MS3_FIXED_LENGTH = 40

SLHEADSIZE_V3 = 8
SLHEADSIZE_V4 = 17
SIGNATURE_V3 = b"SL"
SIGNATURE_V4 = b"SE"
INFOSIGNATURE = b"SLINFO"

# Payload format bytes, mirroring libslink.h
SLPAYLOAD_MSEED2 = ord("2")
SLPAYLOAD_MSEED3 = ord("3")
SLPAYLOAD_JSON = ord("J")
SLPAYLOAD_JSON_INFO = ord("I")
SLPAYLOAD_JSON_ERROR = ord("E")


def _pad(s, length):
    b = s.encode("ascii") if isinstance(s, str) else s
    return (b + b" " * length)[:length]


def build_ms2(
    network="XX",
    station="TEST",
    location="",
    channel="BHZ",
    year=2024,
    day=1,
    hour=0,
    minute=0,
    sec=0,
    fsec=0,
    numsamples=0,
    samprate_fact=1,
    samprate_mult=1,
    reclen=512,
):
    """Build a full reclen-byte miniSEED2 record: fixed header + B1000."""
    buf = bytearray(reclen)

    buf[0:6] = b"000001"
    buf[6] = ord("D")
    buf[7] = ord(" ")
    buf[8:13] = _pad(station, 5)
    buf[13:15] = _pad(location, 2)
    buf[15:18] = _pad(channel, 3)
    buf[18:20] = _pad(network, 2)
    struct.pack_into("<H", buf, 20, year)
    struct.pack_into("<H", buf, 22, day)
    buf[24] = hour
    buf[25] = minute
    buf[26] = sec
    buf[27] = 0
    struct.pack_into("<H", buf, 28, fsec)
    struct.pack_into("<H", buf, 30, numsamples)
    struct.pack_into("<h", buf, 32, samprate_fact)
    struct.pack_into("<h", buf, 34, samprate_mult)
    buf[36] = 0
    buf[37] = 0
    buf[38] = 0
    buf[39] = 1  # numblockettes
    struct.pack_into("<i", buf, 40, 0)
    struct.pack_into("<H", buf, 44, 56)  # dataoffset (unused by the client)
    struct.pack_into("<H", buf, 46, MS2_FIXED_LENGTH)  # blocketteoffset

    reclen_pow2 = reclen.bit_length() - 1
    assert 1 << reclen_pow2 == reclen, "reclen must be a power of two"

    off = MS2_FIXED_LENGTH
    struct.pack_into("<H", buf, off + 0, 1000)  # blockette type
    struct.pack_into("<H", buf, off + 2, 0)  # next blockette offset
    buf[off + 4] = 0  # encoding
    buf[off + 5] = 0  # byte order
    buf[off + 6] = reclen_pow2
    buf[off + 7] = 0

    return bytes(buf)


def build_ms2_info(text=b"<info/>", reclen=512):
    """Build a reclen-byte pseudo-miniSEED2 record carrying INFO/keepalive
    text.  The wire format wraps INFO/keepalive responses in a record that
    looks like ordinary miniSEED2 (so the client's generic detect() can
    determine its length) with the text placed after the header+blockette."""
    buf = bytearray(build_ms2(year=2024, day=1, hour=0, minute=0, sec=0, reclen=reclen))
    off = MS2_FIXED_LENGTH + MS2_B1000_LENGTH
    payload = text[: reclen - off]
    buf[off : off + len(payload)] = payload
    return bytes(buf)


def build_ms3(
    sid="FDSN:XX_TEST",
    year=2024,
    day=1,
    hour=0,
    minute=0,
    sec=0,
    nsec=0,
    samplerate=0.0,
    numsamples=0,
    pubversion=1,
    data=b"",
):
    sidb = sid.encode("ascii")
    total = MS3_FIXED_LENGTH + len(sidb) + len(data)
    buf = bytearray(total)

    buf[0:2] = b"MS"
    buf[2] = 3
    buf[3] = 0
    struct.pack_into("<I", buf, 4, nsec)
    struct.pack_into("<H", buf, 8, year)
    struct.pack_into("<H", buf, 10, day)
    buf[12] = hour
    buf[13] = minute
    buf[14] = sec
    buf[15] = 0
    struct.pack_into("<d", buf, 16, samplerate)
    struct.pack_into("<I", buf, 24, numsamples)
    struct.pack_into("<I", buf, 28, 0)
    buf[32] = pubversion
    buf[33] = len(sidb)
    struct.pack_into("<H", buf, 34, 0)
    struct.pack_into("<I", buf, 36, len(data))
    buf[MS3_FIXED_LENGTH : MS3_FIXED_LENGTH + len(sidb)] = sidb
    if data:
        buf[MS3_FIXED_LENGTH + len(sidb) :] = data

    return bytes(buf)


def frame_v3_data(seqnum, payload):
    """8-byte 'SL' + 6 hex digit sequence header, followed by the record."""
    return SIGNATURE_V3 + ("%06X" % (seqnum & 0xFFFFFF)).encode("ascii") + payload


def frame_v3_info(payload, terminated=True):
    """8-byte INFO header (INFOSIGNATURE + filler + continuation flag)."""
    flag = b" " if terminated else b"*"
    return INFOSIGNATURE + b" " + flag + payload


def frame_v4(payloadformat, payloadsubformat, seqnum, payload, stationid=None):
    """17-byte 'SE' header, optional station id, then the payload."""
    stationidb = stationid.encode("ascii") if stationid else b""
    header = bytearray(SLHEADSIZE_V4)
    header[0:2] = SIGNATURE_V4
    header[2] = payloadformat if isinstance(payloadformat, int) else ord(payloadformat)
    header[3] = payloadsubformat if isinstance(payloadsubformat, int) else ord(payloadsubformat)
    struct.pack_into("<I", header, 4, len(payload))
    struct.pack_into("<Q", header, 8, seqnum & 0xFFFFFFFFFFFFFFFF)
    header[16] = len(stationidb)
    return bytes(header) + stationidb + payload


def frame_v4_data(seqnum, stationid, ms3_record):
    return frame_v4(SLPAYLOAD_MSEED3, ord("D"), seqnum, ms3_record, stationid=stationid)


def frame_v4_json_error(seqnum, stationid, text):
    """A 'J'/'E' JSON error packet (protocol.html, reserved format table),
    e.g. delivered mid-stream rather than as a direct command response."""
    return frame_v4(SLPAYLOAD_JSON, SLPAYLOAD_JSON_ERROR, seqnum, text, stationid=stationid)


def frame_v4_opaque(seqnum, stationid, subformat, payload):
    """A generic 'E'/'C' (event/calibration), 'T'/'L' (timing/log), or
    'O'/'X' (opaque/XML) packet -- the remaining reserved format/subformat
    pairs from the table in protocol.html, "Data formats"."""
    fmt, sub = subformat
    return frame_v4(fmt, sub, seqnum, payload, stationid=stationid)


def frame_v4_info(seqnum, text):
    """v4 INFO/keepalive responses are JSON payloads with subformat 'I'.
    Unlike v3, sl_collect() recognizes no "continued" JSON INFO state, so
    every v4 INFO response is inherently a single, terminated packet."""
    return frame_v4(SLPAYLOAD_JSON, SLPAYLOAD_JSON_INFO, seqnum, text)
