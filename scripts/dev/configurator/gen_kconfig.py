#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Generate Kconfig files from Tilck's tilck_options.json sidecar.

Two modes:

    --validate
        Schema-check the sidecar. Exit 0 if valid, nonzero (with
        diagnostics on stderr) otherwise. Called from CMake on every
        configure so bad metadata is caught at configure time, not at
        run_config time.

    --out <dir>
        Emit Kconfig files + a seed .config into <dir>. Called from
        scripts/run_config before launching mconf/nconf.

The sidecar is NDJSON, one option per line; see `tilck_option()` in
other/cmake/utils.cmake for the record format.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


VALID_TYPES = {"bool", "string", "enum", "int", "uint", "addr"}
REQUIRED_FIELDS = {
    "name", "type", "category", "default", "current", "help", "depends",
}
# Comment records ({"type":"comment", "category":..., "text":...})
# are rendered as Kconfig `comment "TEXT"` lines — non-interactive
# menu separators shown as "--- TEXT ---" in mconf. They have no
# cache variable and no name.
COMMENT_FIELDS = {"type", "category", "text"}

INT_RE = re.compile(r"^-?[0-9]+$")
UINT_RE = re.compile(r"^[0-9]+$")
ADDR_RE = re.compile(r"^0x[0-9a-fA-F]+$")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Upper bound for the UINT Kconfig `range` clause. Busybox/Linux
# kconfig's sym_get_range_val() returns `int` (32-bit) via strtol(),
# so a literal 2^64-1 silently overflows and wraps to -1 — every
# value then fails sym_validate_range() and gets rewritten to -1 in
# .config. Cap at INT_MAX (2^31-1) so the bound parses cleanly; any
# Tilck UINT option that legitimately needs larger values must be
# set through `cmake -D` directly. CMake's tilck_option() still
# validates DEFAULT against ^[0-9]+$ regardless of this cap.
UINT_RANGE_MAX = 2147483647


# ---------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------

def load_sidecar(path: Path) -> list[dict]:
    """Load NDJSON sidecar. Empty lines are ignored."""
    records = []
    text = path.read_text() if path.exists() else ""
    for lineno, line in enumerate(text.splitlines(), start=1):
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as e:
            raise ValueError(f"{path}:{lineno}: invalid JSON: {e}") from e
    return records


# ---------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------

def validate(records: list[dict]) -> list[str]:
    """Return a list of error messages; empty list = valid."""
    errors: list[str] = []
    names = {r["name"] for r in records if isinstance(r, dict) and "name" in r}
    seen: set[str] = set()

    for i, r in enumerate(records, start=1):
        if not isinstance(r, dict):
            errors.append(f"record #{i}: not a JSON object")
            continue

        # Comment records are validated separately: they have no
        # name, default, current, etc. — only category + text.
        if r.get("type") == "comment":
            missing = COMMENT_FIELDS - r.keys()
            if missing:
                errors.append(
                    f"<comment #{i}>: missing fields {sorted(missing)}"
                )
            continue

        name = r.get("name", f"<record #{i}>")
        missing = REQUIRED_FIELDS - r.keys()
        if missing:
            errors.append(f"{name}: missing fields {sorted(missing)}")
            continue

        if name in seen:
            errors.append(f"{name}: duplicate name")
        seen.add(name)

        t = r["type"]
        if t not in VALID_TYPES:
            errors.append(f"{name}: unknown type '{t}'")
            continue

        if t == "enum":
            strings = r.get("strings")
            if not strings:
                errors.append(f"{name}: enum requires non-empty 'strings'")
            elif r["default"] not in strings:
                errors.append(
                    f"{name}: default '{r['default']}' not in "
                    f"strings {strings}"
                )
        elif t == "int":
            if not INT_RE.match(r["default"]):
                errors.append(
                    f"{name}: int default '{r['default']}' is not a valid integer"
                )
        elif t == "uint":
            if not UINT_RE.match(r["default"]):
                errors.append(
                    f"{name}: uint default '{r['default']}' is not a valid "
                    f"unsigned integer"
                )
        elif t == "addr":
            if not ADDR_RE.match(r["default"]):
                errors.append(
                    f"{name}: addr default '{r['default']}' is not a valid "
                    f"0x-prefixed hex value"
                )

        for dep in r.get("depends", []):
            # Bare-name deps (optionally !-prefixed) are validated by
            # presence. Complex boolean expressions are passed through
            # to the generator unchecked — Kconfig/mconf handle them.
            expr = dep.lstrip("!").strip()
            if IDENT_RE.match(expr):
                if expr not in names:
                    errors.append(
                        f"{name}: depends '{dep}' references unknown option"
                    )

    return errors


# ---------------------------------------------------------------------
# Generation
# ---------------------------------------------------------------------

def _help_lines(raw: str) -> list[str]:
    """Split a help string on newlines (real or literal-\\n)."""
    # JSON decoding turns \n in the JSONL into a real newline, but
    # tolerate literal backslash-n too in case CMake's encoder ever
    # skips escaping.
    return raw.replace("\\n", "\n").splitlines() or [""]


def _truthy(val: str) -> bool:
    return str(val).upper() in ("Y", "YES", "ON", "TRUE", "1")


def _emit_record(lines: list[str], r: dict) -> None:
    """Append Kconfig lines for a single option or comment record."""
    # Comment records render as "comment \"TEXT\"" — a non-
    # interactive separator shown as "--- TEXT ---" in mconf. No
    # cache var, no DEPENDS, no help block needed.
    if r.get("type") == "comment":
        lines.append(f'comment "{r["text"]}"')
        lines.append("")
        return

    name = r["name"]
    t = r["type"]
    help_all = _help_lines(r["help"])
    summary = help_all[0] if help_all else name

    lines.append(f"config {name}")

    if t == "bool":
        lines.append(f'\tbool "{summary}"')
        lines.append(f'\tdefault {"y" if _truthy(r["default"]) else "n"}')
    elif t == "string":
        lines.append(f'\tstring "{summary}"')
        lines.append(f'\tdefault "{r["default"]}"')
    elif t == "enum":
        # Flat ENUM: emit as string with the valid value list in help.
        # A proper `choice` block would need synthetic per-value symbols
        # and a value-mapping layer; deferred per the plan.
        lines.append(f'\tstring "{summary}"')
        lines.append(f'\tdefault "{r["default"]}"')
    elif t == "int":
        lines.append(f'\tint "{summary}"')
        lines.append(f'\tdefault {r["default"]}')
    elif t == "uint":
        lines.append(f'\tint "{summary}"')
        lines.append(f'\tdefault {r["default"]}')
        lines.append(f"\trange 0 {UINT_RANGE_MAX}")
    elif t == "addr":
        lines.append(f'\thex "{summary}"')
        lines.append(f'\tdefault {r["default"]}')

    for dep in r.get("depends", []):
        lines.append(f"\tdepends on {dep}")

    # Kconfig `help` block. The first HELP element is already the
    # prompt (emitted above as `bool "..."`); repeat it as the
    # first help line so F1/? shows a consistent title, then add a
    # visually-blank separator, then the body.  CMake drops empty
    # list elements inside `cmake_parse_arguments`, so an in-source
    # `""` HELP separator never survives to the sidecar; inserting
    # the blank here keeps the author's CMake side clean.
    lines.append("\thelp")
    lines.append(f"\t  {help_all[0]}")
    if len(help_all) > 1:
        lines.append("\t  ")  # visual separator between summary and body
        for hl in help_all[1:]:
            lines.append(f"\t  {hl}")
    if t == "enum":
        lines.append("\t  ")
        lines.append(f"\t  Valid values: {', '.join(r['strings'])}")

    lines.append("")


def _build_tree(records: list[dict], skip_parts: int) -> dict:
    """
    Build a nested dict keyed by category-path components past the
    first `skip_parts` levels. Each node has 'records' (list) and
    'children' (dict).
    """
    root = {"records": [], "children": {}}
    for r in records:
        parts = r["category"].split("/")[skip_parts:]
        node = root
        for p in parts:
            node = node["children"].setdefault(p, {"records": [], "children": {}})
        node["records"].append(r)
    return root


def _emit_tree(lines: list[str], node: dict) -> None:
    for r in node["records"]:
        _emit_record(lines, r)
    for subname in sorted(node["children"].keys()):
        lines.append(f'menu "{subname}"')
        lines.append("")
        _emit_tree(lines, node["children"][subname])
        lines.append("endmenu")
        lines.append("")


def _seed_config_line(r: dict) -> str:
    """Produce one .config line reflecting the option's 'current' value."""
    name = r["name"]
    t = r["type"]
    val = r["current"]
    if t == "bool":
        return (
            f"CONFIG_{name}=y"
            if _truthy(val)
            else f"# CONFIG_{name} is not set"
        )
    if t in ("int", "uint", "addr"):
        return f"CONFIG_{name}={val}"
    # string / enum
    return f'CONFIG_{name}="{val}"'


def generate(records: list[dict], outdir: Path) -> None:
    """Emit Kconfig files + seed .config into outdir."""
    outdir.mkdir(parents=True, exist_ok=True)

    by_top: dict[str, list[dict]] = {}
    for r in records:
        top = r["category"].split("/")[0] if r["category"] else "Misc"
        by_top.setdefault(top, []).append(r)

    root_lines = ['mainmenu "Tilck Configuration"', ""]
    for top in sorted(by_top.keys()):
        # Sanitise filename: spaces → underscores, lowercase. The
        # in-menu title keeps the original casing + spacing; only
        # the backing Kconfig.<stem> filename is normalised.
        stem = top.lower().replace(" ", "_")
        sub_path = outdir / f"Kconfig.{stem}"
        sub_lines = [f'menu "{top}"', ""]
        tree = _build_tree(by_top[top], skip_parts=1)
        _emit_tree(sub_lines, tree)
        sub_lines.append("endmenu")
        sub_lines.append("")
        sub_path.write_text("\n".join(sub_lines))
        root_lines.append(f'source "Kconfig.{stem}"')
    root_lines.append("")
    (outdir / "Kconfig").write_text("\n".join(root_lines))

    # Seed .config from the option records' current values. Skip
    # comment records — they have no CONFIG_ symbol.
    dotconfig = [
        _seed_config_line(r) for r in records
        if r.get("type") != "comment"
    ]
    (outdir / ".config").write_text("\n".join(dotconfig) + "\n")


# ---------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--sidecar", required=True, type=Path,
                    help="Path to tilck_options.json (NDJSON)")
    grp = ap.add_mutually_exclusive_group(required=True)
    grp.add_argument("--validate", action="store_true",
                     help="Validate schema and exit")
    grp.add_argument("--out", type=Path,
                     help="Emit Kconfig files + seed .config into DIR")
    args = ap.parse_args()

    try:
        records = load_sidecar(args.sidecar)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    errors = validate(records)
    if errors:
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        return 1

    if args.validate:
        return 0

    generate(records, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
