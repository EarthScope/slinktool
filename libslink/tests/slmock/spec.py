"""Vocabulary drawn directly from the published SeedLink protocol specs,
so tests reference named constants instead of restating literals:

  v3 -- https://www.seiscomp.de/doc/apps/seedlink.html#seedlink
  v4 -- https://docs.fdsn.org/projects/seedlink/en/latest/protocol.html

`check_command()` is the one piece of behavior here: given a raw command
line as libslink sent it, decide whether it is legal for the negotiated
protocol version. Mock server handlers pass every line they read through
it in "strict" mode so a command the client should never send (a v3 verb
on a v4 connection, a line over the v4 length limit, ...) fails the test
that triggered it instead of silently being answered as if it were fine.
"""

# Error codes (v4 protocol, "Error codes" section).
V4_ERROR_CODES = (
    "UNSUPPORTED",
    "UNEXPECTED",
    "UNAUTHORIZED",
    "LIMIT",
    "ARGUMENTS",
    "AUTH",
    "INTERNAL",
)

# Reserved payload format/subformat pairs (v4 protocol, "Data formats").
V4_FORMATS = (
    ("2", "D"),  # miniSEED 2.x, data/generic
    ("3", "D"),  # miniSEED 3.x (FDSN), data/generic
    ("J", "I"),  # JSON, SeedLink info
    ("J", "E"),  # JSON, SeedLink error
    ("E", "C"),  # Event detection, calibration
    ("T", "L"),  # Timing exception, log
    ("O", "X"),  # Opaque, XML
)

# Capability tokens a HELLO response may advertise (v4 protocol,
# "Capabilities" section). SLPROTO and AUTH are parameterized; the
# literal token is followed by ":" and a value.
V4_CAPABILITY_PREFIXES = ("SLPROTO:", "AUTH:")
V4_CAPABILITY_FLAGS = ("TIME", "SEQWILDCARD")

# Command verbs each protocol version defines. A verb outside this set
# (for either version) is not part of that version's protocol at all.
V3_VERBS = {
    "HELLO",
    "CAT",
    "BYE",
    "STATION",
    "SELECT",
    "DATA",
    "FETCH",
    "TIME",
    "END",
    "INFO",
    # Not in the core v3 command list itself, but real v3-era commands:
    # the v4 spec's "Removed Commands" table names both, explaining
    # CAPABILITIES as superseded by SLPROTO and BATCH as superseded by
    # asynchronous handshaking; the "Versions" changelog dates BATCH to
    # v3.1. libslink sends both over v3 connections.
    "CAPABILITIES",
    "BATCH",
}

V4_VERBS = {
    "HELLO",
    "SLPROTO",
    "AUTH",
    "USERAGENT",
    "STATION",
    "SELECT",
    "DATA",
    "END",
    "ENDFETCH",
    "INFO",
    "BYE",
}

# "Maximum command line: 255 characters including terminator" (v4 protocol,
# "Handshaking" section).
V4_MAX_COMMAND_LEN = 255

# v4 commands terminate with CRLF, a bare CR, or a bare LF; v3 commands
# always use CRLF (mirrored from CommandReader's docstring in server.py,
# which reflects network.c's actual write sites).
V4_TERMINATORS = (b"\r\n", b"\r", b"\n")
V3_TERMINATORS = (b"\r\n",)


class SpecViolation(AssertionError):
    """Raised when a command read from the client does not conform to the
    negotiated protocol version's spec. Subclasses AssertionError so it
    reads as a normal test failure with a spec citation in its message."""


def check_command(cmd, terminator, protocol):
    """Validate one command line already split from its terminator.

    `cmd` is the decoded command text (no terminator). `terminator` is
    the raw bytes that ended the line. `protocol` is 3 or 4.
    """
    verb = cmd.split(" ", 1)[0] if cmd else ""

    if protocol == 4:
        if verb and verb not in V4_VERBS:
            raise SpecViolation(
                "%r is not a v4 command (protocol.html, Commands): %r" % (verb, cmd)
            )
        if terminator not in V4_TERMINATORS:
            raise SpecViolation(
                "v4 command terminator must be CRLF, CR, or LF (protocol.html, "
                "Handshaking), got %r for %r" % (terminator, cmd)
            )
        # +len(terminator): the limit is stated as inclusive of the terminator.
        if len(cmd) + len(terminator) > V4_MAX_COMMAND_LEN:
            raise SpecViolation(
                "v4 command line exceeds %d chars including terminator "
                "(protocol.html, Handshaking): %r" % (V4_MAX_COMMAND_LEN, cmd)
            )
    elif protocol == 3:
        if verb and verb not in V3_VERBS:
            raise SpecViolation(
                "%r is not a v3 command (seiscomp seedlink docs, Commands): %r"
                % (verb, cmd)
            )
        if terminator not in V3_TERMINATORS:
            raise SpecViolation(
                "v3 commands are CRLF-terminated: got %r for %r" % (terminator, cmd)
            )
    else:
        raise ValueError("protocol must be 3 or 4, got %r" % (protocol,))
