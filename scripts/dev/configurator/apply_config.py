#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Translate mconf's .config output into CMake -D args.

Usage: apply_config.py <dotconfig> <cmakecache>

Diffs the .config file produced by mconf/nconf against the current
CMakeCache.txt and emits one -D<NAME>=<value> argument per line (on
stdout) for each option whose value has changed. Bool values are
normalised to 1/0 (matching scripts/cmake_run's convention for
-D<BOOL>=<val>).

The caller (scripts/run_config) captures stdout into a bash array
via `mapfile -t` and re-runs cmake_run with those args. Lines are
newline-separated; values may contain spaces but not embedded
newlines (mconf doesn't produce them for Kconfig types we emit).

Only options that appear in BOTH .config (i.e. were declared via
tilck_option()) AND CMakeCache.txt are considered. Unmatched
.config entries (shouldn't happen in a healthy flow) are silently
skipped.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


CONFIG_LINE_RE = re.compile(r"^CONFIG_(\w+)=(.*)$")
UNSET_LINE_RE  = re.compile(r"^# CONFIG_(\w+) is not set$")
CACHE_LINE_RE  = re.compile(r"^(\w+):(\w+)=(.*)$")

BOOL_TRUE  = {"y", "yes", "on", "true", "1"}
BOOL_FALSE = {"n", "no", "off", "false", "0", ""}


# ---------------------------------------------------------------------
# Parsers
# ---------------------------------------------------------------------

def parse_dotconfig(path: Path) -> dict[str, str]:
    """Return {NAME: value} from a Kconfig .config file.

    Booleans are stored as 'y' / 'n'. Strings have their surrounding
    quotes stripped (and basic backslash escapes decoded: \\" -> ",
    \\\\ -> \\). Int / hex values are stored as-is.
    """
    out: dict[str, str] = {}
    for line in path.read_text().splitlines():
        s = line.strip()
        if not s:
            continue
        m = CONFIG_LINE_RE.match(s)
        if m:
            name, val = m.group(1), m.group(2)
            if val.startswith('"') and val.endswith('"') and len(val) >= 2:
                inner = val[1:-1]
                val = inner.replace(r'\"', '"').replace(r"\\", "\\")
            out[name] = val
            continue
        m = UNSET_LINE_RE.match(s)
        if m:
            out[m.group(1)] = "n"
    return out


def parse_cmakecache(path: Path) -> dict[str, tuple[str, str]]:
    """Return {NAME: (TYPE, value)} from a CMakeCache.txt."""
    out: dict[str, tuple[str, str]] = {}
    for line in path.read_text().splitlines():
        s = line.strip()
        if not s or s.startswith("#") or s.startswith("//"):
            continue
        m = CACHE_LINE_RE.match(s)
        if m:
            out[m.group(1)] = (m.group(2), m.group(3))
    return out


# ---------------------------------------------------------------------
# Comparison + translation
# ---------------------------------------------------------------------

def bool_canonical(val: str) -> str | None:
    """Canonicalise a bool-ish string to 'y' / 'n', or None if not a bool."""
    lc = val.lower().strip()
    if lc in BOOL_TRUE:
        return "y"
    if lc in BOOL_FALSE:
        return "n"
    return None


def values_match(dc_val: str, cc: tuple[str, str]) -> bool:
    """Does the .config value equal the CMakeCache entry (TYPE, value)?"""
    cc_type, cc_val = cc
    if cc_type == "BOOL":
        dc_b = bool_canonical(dc_val)
        cc_b = bool_canonical(cc_val)
        if dc_b is not None and cc_b is not None:
            return dc_b == cc_b
    return dc_val == cc_val


def dflag(name: str, dc_val: str, cc_type: str) -> str:
    """Format a -DNAME=value argument reflecting dc_val."""
    if cc_type == "BOOL":
        b = bool_canonical(dc_val)
        return f"-D{name}={1 if b == 'y' else 0}"
    return f"-D{name}={dc_val}"


def compute_dflags(
    dotconfig: dict[str, str],
    cmakecache: dict[str, tuple[str, str]],
) -> list[str]:
    """Return the list of -D args for options whose values changed."""
    out: list[str] = []
    for name, val in dotconfig.items():
        cc = cmakecache.get(name)
        if cc is None:
            continue
        if not values_match(val, cc):
            out.append(dflag(name, val, cc[0]))
    return out


# ---------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dotconfig", type=Path)
    ap.add_argument("cmakecache", type=Path)
    args = ap.parse_args()

    dc = parse_dotconfig(args.dotconfig)
    cc = parse_cmakecache(args.cmakecache)
    for d in compute_dflags(dc, cc):
        print(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
