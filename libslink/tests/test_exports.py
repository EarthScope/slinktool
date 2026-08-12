#!/usr/bin/env python3
"""Cross-check the public API declared in libslink.h against the two
platform export lists that are supposed to mirror it: libslink.def
(Windows) and libslink.map (the ELF/Darwin symbol-visibility script).

Also cross-checks libslink.def against the actual symbols produced by
the build (via `nm` on ../libslink.a), since a name can be spelled
correctly in the source but wrong in the export list, or vice versa.

Also cross-checks each public declaration's return type against its
own definition in the first-party .c sources, since matching *names*
on both sides says nothing about matching *signatures*.
"""

import glob
import os
import re
import subprocess
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "libslink.h")
DEF_FILE = os.path.join(ROOT, "libslink.def")
MAP_FILE = os.path.join(ROOT, "libslink.map")
STATIC_LIB = os.path.join(ROOT, "libslink.a")


def _normalize_type(t):
    """Canonicalize a C type so "const char *", "const char*", and
    "const  char  *" all compare equal."""
    t = t.replace("*", " * ")
    return " ".join(t.split())


def public_function_names():
    """Every function declared `extern ... name (...)` in libslink.h.
    This intentionally excludes the static inline sl_gswap2/4/8 helpers,
    which are declared without `extern` and have no external linkage."""
    with open(HEADER, "r", encoding="utf-8") as f:
        text = f.read()

    # Strip comments so a name mentioned only in a doc comment (e.g. the
    # @sa cross-references) is never mistaken for a declaration.
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)

    names = []
    for stmt in text.split(";"):
        if "extern" not in stmt:
            continue
        m = re.search(r"\bsl_[A-Za-z0-9_]*\s*\(", stmt)
        if m:
            names.append(m.group(0).split("(")[0].strip())
    return sorted(set(names))


def declared_signatures():
    """Map public function name -> normalized return type, as declared in
    libslink.h."""
    with open(HEADER, "r", encoding="utf-8") as f:
        text = f.read()

    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//.*", "", text)

    sigs = {}
    for stmt in text.split(";"):
        if "extern" not in stmt:
            continue
        flat = " ".join(stmt.split())
        m = re.search(r"extern\s+(.+?)\s+(sl_[A-Za-z0-9_]+)\s*\(", flat)
        if m:
            sigs[m.group(2)] = _normalize_type(m.group(1))
    return sigs


def defined_signatures():
    """Map function name -> normalized return type, from its definition in
    the first-party .c sources. Every public function here is written K&R
    style with the return type alone on the line above `name (args)` at
    column 0, so the preceding line is the return type."""
    sigs = {}
    for path in glob.glob(os.path.join(ROOT, "*.c")):
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        for i, line in enumerate(lines):
            m = re.match(r"(sl_[A-Za-z0-9_]+)\s*\(", line)
            if not m or i == 0:
                continue
            rtype = lines[i - 1].strip()
            if rtype and not rtype.endswith(("{", "}", ";", ")")):
                sigs[m.group(1)] = _normalize_type(rtype)
    return sigs


def def_entries():
    with open(DEF_FILE, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f]
    return sorted(
        line for line in lines if line and not line.startswith(("LIBRARY", "EXPORTS"))
    )


def built_library_symbols():
    """Global text symbols actually present in ../libslink.a, via nm.
    Handles both the Mach-O leading-underscore convention and the
    unprefixed ELF convention."""
    if not os.path.exists(STATIC_LIB):
        raise unittest.SkipTest("../libslink.a is not built; run `make` first")

    out = subprocess.run(
        ["nm", "-g", STATIC_LIB], capture_output=True, text=True, check=False
    ).stdout

    names = set()
    for line in out.splitlines():
        m = re.search(r"\bT\s+_?(sl_[A-Za-z0-9_]+)\s*$", line)
        if m:
            names.add(m.group(1))
    return names


class TestExportConsistency(unittest.TestCase):
    def setUp(self):
        self.public = public_function_names()
        self.defs = def_entries()

    def test_every_public_function_is_in_the_def_file(self):
        missing = sorted(set(self.public) - set(self.defs))
        self.assertEqual(
            missing,
            [],
            "libslink.def is missing exports for public functions declared in "
            "libslink.h: %r" % (missing,),
        )

    def test_every_def_entry_is_a_real_exported_symbol(self):
        real_symbols = built_library_symbols()
        bogus = sorted(name for name in self.defs if name not in real_symbols)
        self.assertEqual(
            bogus,
            [],
            "libslink.def lists names that are not real exported symbols of the "
            "built library (typos, or -- for sl_gswap2/4/8 -- static inline "
            "functions with no external linkage): %r" % (bogus,),
        )

    def test_map_file_exports_the_sl_prefix(self):
        with open(MAP_FILE, "r", encoding="utf-8") as f:
            content = f.read()
        self.assertIn("sl_*", content, "libslink.map should export the sl_ symbol prefix")

    def test_declared_return_types_match_their_definitions(self):
        """A name matching on both sides of the header/source split says
        nothing about the signature agreeing. Functions declared only via
        a static inline body in libslink.h itself (sl_gswap2/4/8) have no
        separate .c definition to compare against and are skipped."""
        declared = declared_signatures()
        defined = defined_signatures()
        mismatches = sorted(
            (name, dtype, defined[name])
            for name, dtype in declared.items()
            if name in defined and defined[name] != dtype
        )
        self.assertEqual(
            mismatches,
            [],
            "public functions whose libslink.h return type disagrees with "
            "their definition (name, declared, defined): %r" % (mismatches,),
        )


if __name__ == "__main__":
    unittest.main()
