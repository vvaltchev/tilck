#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""End-to-end integration tests for the configurator pipeline.

Exercises the FULL flow as a user would experience it through
scripts/run_config:

    JSONL sidecar
        |  gen_kconfig.py --out <dir>
        v
    Kconfig + seeded .config
        |  [fake menuconfig edits the .config]
        v
    edited .config
        |  apply_config.py .config CMakeCache.txt
        v
    -D<NAME>=<value> lines on stdout

Both gen_kconfig.py and apply_config.py are invoked as subprocesses
so argument parsing, file I/O, and exit codes are exercised the
same way scripts/run_config does.

The "fake menuconfig" is a Python helper (FakeMenuconfig below)
that edits .config the same way mconf would when the user
interactively toggles options — rewriting `CONFIG_NAME=value`
lines or flipping `# CONFIG_NAME is not set` to `CONFIG_NAME=y`
and back. This sidesteps mconf's TTY requirement while still
testing every byte of our own pipeline.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
CONFIGURATOR_DIR = HERE.parent
GEN_KCONFIG = CONFIGURATOR_DIR / "gen_kconfig.py"
APPLY_CONFIG = CONFIGURATOR_DIR / "apply_config.py"


# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

def _write_jsonl(path: Path, records: list[dict]) -> None:
    path.write_text("\n".join(json.dumps(r) for r in records) + "\n")


def _write_cmake_cache(path: Path, entries: list[tuple[str, str, str]]) -> None:
    """entries: list of (name, cmake_type, value) tuples."""
    lines = ["# CMakeCache.txt test fixture"]
    for name, type_, value in entries:
        lines.append(f"{name}:{type_}={value}")
    path.write_text("\n".join(lines) + "\n")


def _run_gen_kconfig(sidecar: Path, outdir: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["python3", str(GEN_KCONFIG),
         "--sidecar", str(sidecar), "--out", str(outdir)],
        capture_output=True, text=True,
    )


def _run_gen_validate(sidecar: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["python3", str(GEN_KCONFIG),
         "--sidecar", str(sidecar), "--validate"],
        capture_output=True, text=True,
    )


def _run_apply(dotconfig: Path, cache: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["python3", str(APPLY_CONFIG), str(dotconfig), str(cache)],
        capture_output=True, text=True,
    )


class FakeMenuconfig:
    """
    Stand-in for the mconf UI: edits a .config file in memory and
    writes it back, simulating the line-by-line changes mconf would
    make when a user interactively toggles options.
    """

    def __init__(self, dotconfig: Path):
        self.path = dotconfig
        self.lines: list[str] = dotconfig.read_text().splitlines()

    # Bool: flip `CONFIG_X=y` <-> `# CONFIG_X is not set`
    def set_bool(self, name: str, on: bool) -> None:
        tgt_y = f"CONFIG_{name}="
        tgt_n = f"# CONFIG_{name} is not set"
        new_line = f"CONFIG_{name}=y" if on else f"# CONFIG_{name} is not set"
        out: list[str] = []
        matched = False
        for line in self.lines:
            if line.startswith(tgt_y) or line == tgt_n:
                out.append(new_line)
                matched = True
            else:
                out.append(line)
        if not matched:
            raise KeyError(f"{name} not found in .config")
        self.lines = out

    # Int / uint / addr / string: rewrite `CONFIG_X=<old>` to
    # `CONFIG_X=<new>`. `quoted=True` for string-type values.
    def set_value(self, name: str, value: str, quoted: bool = False) -> None:
        tgt = f"CONFIG_{name}="
        v = f'"{value}"' if quoted else value
        out: list[str] = []
        matched = False
        for line in self.lines:
            if line.startswith(tgt):
                out.append(f"{tgt}{v}")
                matched = True
            else:
                out.append(line)
        if not matched:
            raise KeyError(f"{name} not found in .config")
        self.lines = out

    def save(self) -> None:
        self.path.write_text("\n".join(self.lines) + "\n")


def _rec(**kw) -> dict:
    """Build a sidecar record with sensible defaults, overridable via kwargs."""
    base = {
        "name": "FOO",
        "type": "bool",
        "category": "Kernel/Demo",
        "default": "ON",
        "current": "ON",
        "depends": [],
        "help": "Demo option",
    }
    base.update(kw)
    return base


# ---------------------------------------------------------------------
# Fixture: a realistic sidecar with every type we support
# ---------------------------------------------------------------------

def _canonical_records() -> list[dict]:
    return [
        _rec(name="KRN_SHOW_LOGO", type="bool",
             category="Kernel/Appearance",
             default="ON", current="ON",
             help="Show Tilck's logo after boot"),
        _rec(name="KRN_DEBUG", type="bool",
             category="Kernel/Debug",
             default="OFF", current="OFF",
             help="Enable kernel debug printing"),
        _rec(name="KRN_TIMER_HZ", type="int",
             category="Kernel/Timer",
             default="250", current="250",
             help="Kernel timer frequency"),
        _rec(name="KRN_USER_STACK_PAGES", type="uint",
             category="Kernel/Memory",
             default="16", current="16",
             help="User stack size in pages"),
        _rec(name="KERNEL_BASE_VA", type="addr",
             category="Kernel/Memory",
             default="0xffffffff80000000",
             current="0xffffffff80000000",
             help="Kernel base virtual address"),
        _rec(name="KRN_HEAP_SIZE", type="enum",
             category="Kernel/Memory",
             default="auto", current="auto",
             strings=["auto", "64", "128", "256"],
             help="Size in KB of kmalloc's first heap"),
        _rec(name="KRN_BOOT_BANNER", type="string",
             category="Kernel/Appearance",
             default="Welcome to Tilck",
             current="Welcome to Tilck",
             help="Banner printed at boot"),
    ]


def _canonical_cache() -> list[tuple[str, str, str]]:
    # Values mirror the records' `current` field so the starting
    # state is "no changes" until a test edits .config.
    return [
        ("KRN_SHOW_LOGO",        "BOOL",   "ON"),
        ("KRN_DEBUG",            "BOOL",   "OFF"),
        ("KRN_TIMER_HZ",         "STRING", "250"),
        ("KRN_USER_STACK_PAGES", "STRING", "16"),
        ("KERNEL_BASE_VA",       "STRING", "0xffffffff80000000"),
        ("KRN_HEAP_SIZE",        "STRING", "auto"),
        ("KRN_BOOT_BANNER",      "STRING", "Welcome to Tilck"),
        # A few CMake-internal vars that should NEVER appear in -D
        # args (they have no corresponding sidecar entry).
        ("CMAKE_PROJECT_NAME",   "STRING", "tilck"),
        ("CMAKE_BUILD_TYPE",     "STRING", "Debug"),
    ]


# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------

class TestPipelineEndToEnd(unittest.TestCase):

    def _setup_workspace(self) -> tuple[Path, Path, Path, Path]:
        """
        Return (workdir, sidecar, kcfg_dir, cache_path).
        Creates an ephemeral workspace, seeds sidecar + cache.
        """
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        work = Path(tmp.name)
        sidecar = work / "tilck_options.json"
        cache = work / "CMakeCache.txt"
        kcfg = work / "kconfig"
        _write_jsonl(sidecar, _canonical_records())
        _write_cmake_cache(cache, _canonical_cache())
        return work, sidecar, kcfg, cache

    # --- Generator side ---

    def test_validate_accepts_canonical_sidecar(self):
        _, sidecar, _, _ = self._setup_workspace()
        res = _run_gen_validate(sidecar)
        self.assertEqual(res.returncode, 0, msg=res.stderr)

    def test_gen_produces_expected_files(self):
        _, sidecar, kcfg, _ = self._setup_workspace()
        res = _run_gen_kconfig(sidecar, kcfg)
        self.assertEqual(res.returncode, 0, msg=res.stderr)
        self.assertTrue((kcfg / "Kconfig").is_file())
        self.assertTrue((kcfg / ".config").is_file())
        # Both top-level categories got their own per-category file.
        self.assertTrue((kcfg / "Kconfig.kernel").is_file())

    def test_gen_seeds_config_from_current_values(self):
        _, sidecar, kcfg, _ = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        cfg = (kcfg / ".config").read_text()
        # Bool defaults
        self.assertIn("CONFIG_KRN_SHOW_LOGO=y", cfg)
        self.assertIn("# CONFIG_KRN_DEBUG is not set", cfg)
        # Numeric
        self.assertIn("CONFIG_KRN_TIMER_HZ=250", cfg)
        self.assertIn("CONFIG_KRN_USER_STACK_PAGES=16", cfg)
        self.assertIn("CONFIG_KERNEL_BASE_VA=0xffffffff80000000", cfg)
        # Enum + string are quoted
        self.assertIn('CONFIG_KRN_HEAP_SIZE="auto"', cfg)
        self.assertIn('CONFIG_KRN_BOOT_BANNER="Welcome to Tilck"', cfg)

    def test_validate_rejects_bad_sidecar(self):
        # One record with a broken int default.
        _, sidecar, _, _ = self._setup_workspace()
        _write_jsonl(sidecar, [
            _rec(name="BAD_INT", type="int",
                 default="not_a_number", current="not_a_number"),
        ])
        res = _run_gen_validate(sidecar)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("not a valid integer", res.stderr)

    # --- Apply-side no-op (idle mconf run) ---

    def test_no_edits_produces_no_dflags(self):
        """A user opens mconf, looks around, exits without changes."""
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        # No FakeMenuconfig edits at all.
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout, "")

    # --- Single-type edit tests ---

    def test_bool_toggle_on_to_off(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_bool("KRN_SHOW_LOGO", False)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout.strip(), "-DKRN_SHOW_LOGO=0")

    def test_bool_toggle_off_to_on(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_bool("KRN_DEBUG", True)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.stdout.strip(), "-DKRN_DEBUG=1")

    def test_int_change(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_value("KRN_TIMER_HZ", "500")
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.stdout.strip(), "-DKRN_TIMER_HZ=500")

    def test_uint_change(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_value("KRN_USER_STACK_PAGES", "32")
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.stdout.strip(), "-DKRN_USER_STACK_PAGES=32")

    def test_addr_change(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_value("KERNEL_BASE_VA", "0xffff000000000000")
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(
            res.stdout.strip(), "-DKERNEL_BASE_VA=0xffff000000000000"
        )

    def test_enum_change(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_value("KRN_HEAP_SIZE", "256", quoted=True)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.stdout.strip(), "-DKRN_HEAP_SIZE=256")

    def test_string_change(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_value("KRN_BOOT_BANNER", "Hello World", quoted=True)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.stdout.strip(), "-DKRN_BOOT_BANNER=Hello World")

    # --- Multi-edit + regression checks ---

    def test_multiple_edits_emit_all_dflags(self):
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_bool("KRN_SHOW_LOGO", False)
        mc.set_bool("KRN_DEBUG", True)
        mc.set_value("KRN_TIMER_HZ", "1000")
        mc.set_value("KRN_HEAP_SIZE", "128", quoted=True)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertEqual(res.returncode, 0)
        lines = set(res.stdout.strip().splitlines())
        self.assertEqual(lines, {
            "-DKRN_SHOW_LOGO=0",
            "-DKRN_DEBUG=1",
            "-DKRN_TIMER_HZ=1000",
            "-DKRN_HEAP_SIZE=128",
        })

    def test_cmake_internal_vars_never_appear(self):
        """CMakeCache entries not in the sidecar must never produce -D args."""
        _, sidecar, kcfg, cache = self._setup_workspace()
        _run_gen_kconfig(sidecar, kcfg)
        # Make every sidecar-known option change so we get many -D's,
        # and verify none reference CMAKE_PROJECT_NAME / CMAKE_BUILD_TYPE.
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_bool("KRN_SHOW_LOGO", False)
        mc.set_bool("KRN_DEBUG", True)
        mc.set_value("KRN_TIMER_HZ", "999")
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertNotIn("CMAKE_PROJECT_NAME", res.stdout)
        self.assertNotIn("CMAKE_BUILD_TYPE", res.stdout)

    def test_sidecar_extra_entry_not_in_cache_is_skipped(self):
        """
        An option declared via tilck_option() but somehow missing from
        CMakeCache (shouldn't happen in practice, but test the guard)
        must not produce a -D arg even if edited in .config.
        """
        _, sidecar, kcfg, cache = self._setup_workspace()
        # Sidecar has an extra option with no matching cache entry.
        recs = _canonical_records() + [
            _rec(name="EXTRA_OPT", type="bool",
                 category="Misc", default="OFF", current="OFF",
                 help="extra"),
        ]
        _write_jsonl(sidecar, recs)
        _run_gen_kconfig(sidecar, kcfg)
        mc = FakeMenuconfig(kcfg / ".config")
        mc.set_bool("EXTRA_OPT", True)
        mc.save()
        res = _run_apply(kcfg / ".config", cache)
        self.assertNotIn("EXTRA_OPT", res.stdout)


if __name__ == "__main__":
    unittest.main()
