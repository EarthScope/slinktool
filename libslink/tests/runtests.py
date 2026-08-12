#!/usr/bin/env python3
"""Test runner for libslink: runs every compiled test_*.c binary (TAP
output, exit code decides pass/fail) and every test_*.py module (via
the stdlib unittest runner), and reports one combined summary.

Usage:
    python3 runtests.py [-v] [-k PATTERN]

-k PATTERN filters C binaries by substring match on their name, and is
forwarded as unittest's own -k to each Python module.
"""

import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# A hung binary or module should be reported as a timeout for its own
# named group, not left to hang the whole runner indefinitely.
SUBPROCESS_TIMEOUT = 120

# Kept as an explicit list rather than discovered by globbing so that a
# test_*.c source with no corresponding built binary (e.g. a fresh clone
# before `make`) is reported as "not built" instead of silently skipped.
C_BINARIES = [
    "test_genutils",
    "test_globmatch",
    "test_payload",
    "test_slcd",
    "test_streams",
    "test_statefile",
    "test_logging",
    "test_internals",
    "test_netprims",
    "test_network",
]

PYTHON_MODULES = [
    "test_protocol",
    "test_spec_v3",
    "test_spec_v4",
    "test_tls",
    "test_exports",
]

def run_c_binary(name, verbose):
    """Returns "PASS", "FAIL", or "SKIP" (binary not built)."""
    path = os.path.join(HERE, name)

    if not os.path.exists(path):
        print("SKIP %s (not built -- run `make` in tests/)" % name)
        return "SKIP"

    # A C test binary's stderr can legitimately contain raw bytes (e.g. a
    # test deliberately feeding non-ASCII/binary data through the
    # library's own %s-based error logging); decode leniently rather
    # than let a runner-level UnicodeDecodeError mask the actual result.
    try:
        proc = subprocess.run([path], capture_output=True, cwd=HERE, timeout=SUBPROCESS_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("FAIL %s (timed out after %ds)" % (name, SUBPROCESS_TIMEOUT))
        return "FAIL"

    proc_stdout = proc.stdout.decode("utf-8", "replace")
    proc_stderr = proc.stderr.decode("utf-8", "replace")
    status = "PASS" if proc.returncode == 0 else "FAIL"

    print("%s %s" % (status, name))
    if verbose or status == "FAIL":
        sys.stdout.write(proc_stdout)
        sys.stderr.write(proc_stderr)

    return status


def run_python_module(name, verbose, pattern):
    """Returns "PASS", "FAIL", or "SKIP" (every test in the module was
    skipped, e.g. test_tls.py without `trustme` installed). unittest
    itself exits 0 for a fully-skipped module -- a skip is not a
    failure -- so that alone can't tell "ran and passed" apart from
    "never actually ran"; the summary line's skipped= count can."""
    cmd = [sys.executable, "-m", "unittest", name]
    if verbose:
        cmd.append("-v")
    if pattern:
        cmd += ["-k", pattern]

    # Captured (rather than inherited) so the skipped= count can be
    # parsed out of it; still echoed below so behavior otherwise matches
    # letting the child write directly to the terminal.
    try:
        proc = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True, timeout=SUBPROCESS_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("FAIL %s (timed out after %ds)" % (name, SUBPROCESS_TIMEOUT))
        return "FAIL"

    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)

    ok = proc.returncode == 0
    ran_m = re.search(r"Ran (\d+) tests?", proc.stderr)
    skipped_m = re.search(r"skipped=(\d+)", proc.stderr)
    ran = int(ran_m.group(1)) if ran_m else 0
    skipped = int(skipped_m.group(1)) if skipped_m else 0

    # unittest exits 5 ("NO TESTS RAN") when -k matches nothing in this
    # module; that is not a failure of any test, so it's a SKIP regardless
    # of the exit code. A module that ran tests but skipped all of them
    # (e.g. test_tls.py without `trustme` installed) is also a SKIP.
    if ran == 0:
        status = "SKIP"
    elif ok and skipped == ran:
        status = "SKIP"
    elif ok:
        status = "PASS"
    else:
        status = "FAIL"

    print("%s %s" % (status, name))
    return status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("-k", dest="pattern", default=None, help="filter by substring/unittest -k pattern")
    args = ap.parse_args()

    results = {}

    for name in C_BINARIES:
        if args.pattern and args.pattern.lower() not in name.lower():
            continue
        results[name] = run_c_binary(name, args.verbose)

    for name in PYTHON_MODULES:
        results[name] = run_python_module(name, args.verbose, args.pattern)

    passed = sorted(n for n, s in results.items() if s == "PASS")
    failed = sorted(n for n, s in results.items() if s == "FAIL")
    skipped = sorted(n for n, s in results.items() if s == "SKIP")

    print()
    print("%d/%d test groups passed" % (len(passed), len(results)))
    if skipped:
        print("%d skipped: %s" % (len(skipped), ", ".join(skipped)))
    if failed:
        print("FAILED: %s" % ", ".join(failed))

    if not passed and not failed:
        print("no test groups actually ran")
        return 1

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
